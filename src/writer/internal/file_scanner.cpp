/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <concepts>
#include <latch>
#include <mutex>
#include <string_view>
#include <vector>

#include <folly/String.h>

#include <nlohmann/json.hpp>

#include <parallel_hashmap/phmap.h>

#include <range/v3/view/drop.hpp>

#include <dwarfs/checksum.h>
#include <dwarfs/file_view.h>
#include <dwarfs/format.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/file_scanner.h>
#include <dwarfs/writer/internal/inode.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer::internal {

using namespace dwarfs::internal;

namespace {

constexpr size_t const kLargeFileThreshold = 1024 * 1024;
constexpr size_t const kLargeFileStartHashSize = 4096;

} // namespace

template <typename LoggerPolicy>
class file_scanner_ final : public file_scanner::impl {
 public:
  file_scanner_(logger& lgr, worker_group& wg, os_access const& os,
                inode_manager& im, progress& prog,
                file_scanner::options const& opts);

  void scan(file* p) override;
  void finalize(uint32_t& inode_num) override;

  uint32_t num_unique() const override { return num_unique_; }

  void dump(std::ostream& os) const override;

 private:
  template <typename Key, typename Value>
  using fast_map_type = phmap::flat_hash_map<Key, Value>;

  void scan_dedupe(file* p);
  void hash_file(file* p);
  void add_inode(file* p, int lineno);

  template <typename Lookup>
  void finalize_hardlinks(Lookup const& lookup);

  template <bool UniqueOnly = false, typename KeyType>
  void finalize_files(fast_map_type<KeyType, inode::files_vector>& fmap,
                      uint32_t& inode_num, uint32_t& obj_num);

  template <bool Unique, typename KeyType>
  void
  finalize_inodes(std::vector<std::pair<KeyType, inode::files_vector>>& ent,
                  uint32_t& inode_num, uint32_t& obj_num);

  template <typename T>
  std::string format_key(T const& key) const {
    return fmt::format("{}", key);
  }

  template <typename T>
  std::string format_key(T const* key) const {
    return fmt::format("{}", reinterpret_cast<void const*>(key));
  }

  std::string format_key(std::string_view key) const {
    return fmt::format("{}", folly::hexlify(key));
  }

  void dump_value(std::ostream& os, std::integral auto val) const {
    os << fmt::format("{}", val);
  }

  void dump_value(std::ostream& os, std::shared_ptr<std::latch> const&) const {
    os << "null";
  }

  void dump_value(std::ostream& os, file const* p) const;
  void dump_value(std::ostream& os, inode::files_vector const& vec) const;

  void dump_inodes(std::ostream& os) const;
  void dump_inode_create_info(std::ostream& os) const;

  template <typename T>
  void dump_map(std::ostream& os, std::string_view name, T const& map) const;

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  os_access const& os_;
  inode_manager& im_;
  progress& prog_;
  file_scanner::options const opts_;
  uint32_t num_unique_{0};
  fast_map_type<uint64_t, inode::files_vector> hardlinks_;
  std::mutex mutable mx_;
  // The pair stores the file size and optionally a hash of the first
  // 4 KiB of the file. If there's a collision, the worst that can
  // happen is that we unnecessary hash a file that is not a duplicate.
  fast_map_type<std::pair<uint64_t, uint64_t>, inode::files_vector>
      unique_size_;
  // We need this lookup table to later find the unique_size_ entry
  // given just a file pointer.
  fast_map_type<file const*, uint64_t> file_start_hash_;
  fast_map_type<std::pair<uint64_t, uint64_t>, std::shared_ptr<std::latch>>
      first_file_hashed_;
  fast_map_type<uint64_t, inode::files_vector> by_raw_inode_;
  fast_map_type<std::string_view, inode::files_vector> by_hash_;

  struct inode_create_info {
    inode const* i;
    file const* f;
    int line;
  };
  std::vector<inode_create_info> debug_inode_create_;
};

// The `unique_size_` table holds an entry for each file size we
// discover, and optionally - for large files - an XXH3 hash of the
// first 4 KiB of the file.
//
// - When we first discover a new file size (+hash), we know for
//   sure that this file is *not* a duplicate of a file we've seen
//   before. Thus, we can immediately create a new inode, and we can
//   immediately start similarity scanning for this inode.
//
// - When we discover the second file of particular size (+hash), we
//   must fully hash both files (using the user-provided algorithm)
//   to see if they're identical. We already have an inode for the
//   first file, so we must delay the creation of a new inode until
//   we know that the second file is not a duplicate.
//
// - Exactly the same applies for subsequent files.
//
// - We must ensure that the presence of a hash is checked in
//   `by_hash_` for subsequent files only if the first file's
//   hash has been computed and stored. Otherwise, if a subsequent
//   file's hash computation finishes before the first file, we
//   assume (potentially wrongly) that the subsequent file is not
//   a duplicate.
//
// - So subsequent files must wait for the first file unless we
//   know up front that the first file's hash has already been
//   stored. As long as the first file's hash has not been stored,
//   it is still present in `unique_size_`. It will be removed
//   from `unique_size_` after its hash has been stored.
//
// - The optional hash value of the first 4 KiB of a large file is
//   useful if there are a lot of large files with the same size.
//   One potential scenario is uncompressed images which are very
//   likely to have the same size, but very unlikely to have the
//   same contents. The choice of 4 KiB is arbitrary, as is the
//   threshold of 1 MiB for "large files". The 4 KiB hash is computed
//   synchronously, so this could be a potential bottleneck; however,
//   it should happen rarely enough to not be a problem.

template <typename LoggerPolicy>
file_scanner_<LoggerPolicy>::file_scanner_(logger& lgr, worker_group& wg,
                                           os_access const& os,
                                           inode_manager& im, progress& prog,
                                           file_scanner::options const& opts)
    : LOG_PROXY_INIT(lgr)
    , wg_(wg)
    , os_(os)
    , im_(im)
    , prog_(prog)
    , opts_{opts} {}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan(file* p) {
  // This method is supposed to be called from a single thread only.

  if (p->num_hard_links() > 1) {
    auto& vec = hardlinks_[p->raw_inode_num()];
    vec.push_back(p);

    if (vec.size() > 1) {
      p->hardlink(vec[0], prog_);
      ++prog_.files_scanned;
      return;
    }
  }

  p->create_data();

  prog_.original_size += p->size();

  if (opts_.hash_algo) {
    scan_dedupe(p);
  } else {
    prog_.current.store(p);
    p->scan({}, prog_, opts_.hash_algo); // TODO

    by_raw_inode_[p->raw_inode_num()].push_back(p);

    add_inode(p, __LINE__);
  }
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::finalize(uint32_t& inode_num) {
  uint32_t obj_num = 0;

  assert(first_file_hashed_.empty());

  if (opts_.hash_algo) {
    finalize_hardlinks([this](file const* p) -> inode::files_vector& {
      if (auto it = by_hash_.find(p->hash()); it != by_hash_.end()) {
        return it->second;
      }
      auto const size = p->size();
      uint64_t hash{0};
      if (size >= kLargeFileThreshold) [[unlikely]] {
        auto it = file_start_hash_.find(p);
        DWARFS_CHECK(it != file_start_hash_.end(),
                     "internal error: missing start hash for large file");
        hash = it->second;
      }
      return unique_size_.at({size, hash});
    });
    finalize_files<true>(unique_size_, inode_num, obj_num);
    finalize_files(by_raw_inode_, inode_num, obj_num);
    finalize_files(by_hash_, inode_num, obj_num);
  } else {
    finalize_hardlinks([this](file const* p) -> inode::files_vector& {
      return by_raw_inode_.at(p->raw_inode_num());
    });
    finalize_files(by_raw_inode_, inode_num, obj_num);
  }
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan_dedupe(file* p) {
  // We need no lock yet, as `unique_size_` is only manipulated from
  // this thread.
  uint64_t size = p->size();
  uint64_t start_hash{0};

  LOG_TRACE << "scanning file " << p->path_as_string() << " [size=" << size
            << "]";

  if (size >= kLargeFileThreshold) {
    if (!p->is_invalid()) {
      try {
        auto seg =
            os_.map_file(p->fs_path()).segment_at(0, kLargeFileStartHashSize);
        checksum cs(checksum::xxh3_64);
        cs.update(seg.span());
        cs.finalize(&start_hash);
      } catch (...) {
        LOG_ERROR << "failed to map file " << p->path_as_string() << ": "
                  << exception_str(std::current_exception())
                  << ", creating empty file";
        ++prog_.errors;
        p->set_invalid();
      }
    }

    file_start_hash_.emplace(p, start_hash);
  }

  auto const unique_key = std::make_pair(size, start_hash);

  auto [it, is_new] = unique_size_.emplace(unique_key, inode::files_vector());

  if (is_new) {
    // A file (size, start_hash) that has never been seen before. We can safely
    // create a new inode and we'll keep track of the file.
    it->second.push_back(p);

    {
      std::lock_guard lock(mx_);
      add_inode(p, __LINE__);
    }
  } else {
    // This file (size, start_hash) has been seen before, so this is potentially
    // a duplicate.

    std::shared_ptr<std::latch> latch;

    if (it->second.empty()) {
      // This is any file of this (size, start_hash) after the second file
      std::lock_guard lock(mx_);

      if (auto ffi = first_file_hashed_.find(unique_key);
          ffi != first_file_hashed_.end()) {
        latch = ffi->second;
      }
    } else {
      // This is the second file of this (size, start_hash). We now need to
      // hash both the first and second file and ensure that the first file's
      // hash is stored to `by_hash_` first. We set up a latch to synchronize
      // insertion into `by_hash_`.

      latch = std::make_shared<std::latch>(1);

      {
        std::lock_guard lock(mx_);
        DWARFS_CHECK(first_file_hashed_.emplace(unique_key, latch).second,
                     "internal error: first file hashed latch already exists");
      }

      // Add a job for the first file
      wg_.add_job([this, p = it->second.front(), latch, unique_key] {
        hash_file(p);

        {
          std::lock_guard lock(mx_);

          assert(p->get_inode());

          if (p->is_invalid()) [[unlikely]] {
            by_raw_inode_[p->raw_inode_num()].push_back(p);
          } else {
            auto& ref = by_hash_[p->hash()];
            DWARFS_CHECK(ref.empty(),
                         "internal error: unexpected existing hash");
            ref.push_back(p);
          }

          latch->count_down();

          DWARFS_CHECK(first_file_hashed_.erase(unique_key) > 0,
                       "internal error: missing first file hashed latch");
        }
      });

      // Clear files vector, but don't delete the hash table entry, to indicate
      // that files of this (size, start_hash) *must* be hashed.
      it->second.clear();
    }

    // Add a job for any subsequent files
    wg_.add_job([this, p, latch] {
      hash_file(p);

      if (latch) {
        // Wait until the first file of this (size, start_hash) has been added
        // to `by_hash_`.
        latch->wait();
      }

      {
        std::unique_lock lock(mx_);

        if (p->is_invalid()) [[unlikely]] {
          add_inode(p, __LINE__);
          by_raw_inode_[p->raw_inode_num()].push_back(p);
        } else {
          auto& ref = by_hash_[p->hash()];

          if (ref.empty()) {
            // This is *not* a duplicate. We must allocate a new inode.
            add_inode(p, __LINE__);
          } else {
            auto inode = ref.front()->get_inode();
            assert(inode);
            p->set_inode(inode);
            ++prog_.files_scanned;
            ++prog_.duplicate_files;
            prog_.saved_by_deduplication += p->size();
          }

          ref.push_back(p);
        }
      }
    });
  }
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::hash_file(file* p) {
  if (p->is_invalid()) {
    return;
  }

  auto const size = p->size();
  file_view mm;

  if (size > 0) {
    // TODO: use exception-less variant once provided
    try {
      mm = os_.map_file(p->fs_path(), size);
    } catch (...) {
      LOG_ERROR << "failed to map file " << p->path_as_string() << ": "
                << exception_str(std::current_exception())
                << ", creating empty file";
      ++prog_.errors;
      p->set_invalid();
      return;
    }
  }

  prog_.current.store(p);
  p->scan(mm, prog_, opts_.hash_algo);
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::add_inode(file* p, int lineno) {
  assert(!p->get_inode());

  auto inode = im_.create_inode();

  p->set_inode(inode);

  if (opts_.debug_inode_create) {
    debug_inode_create_.push_back({inode.get(), p, lineno});
  }

  im_.scan_background(wg_, os_, std::move(inode), p);
}

template <typename LoggerPolicy>
template <typename Lookup>
void file_scanner_<LoggerPolicy>::finalize_hardlinks(Lookup const& lookup) {
  auto tv = LOG_TIMED_VERBOSE;

  for (auto& kv : hardlinks_) {
    auto& hlv = kv.second;
    if (hlv.size() > 1) {
      auto& fv = lookup(hlv.front());
      for (auto p : ranges::views::drop(hlv, 1)) {
        p->set_inode(fv.front()->get_inode());
        fv.push_back(p);
      }
    }
  }

  hardlinks_.clear();

  tv << "finalized " << hardlinks_.size() << " hardlinks";
}

template <typename LoggerPolicy>
template <bool UniqueOnly, typename KeyType>
void file_scanner_<LoggerPolicy>::finalize_files(
    fast_map_type<KeyType, inode::files_vector>& fmap, uint32_t& inode_num,
    uint32_t& obj_num) {
  std::vector<std::pair<KeyType, inode::files_vector>> ent;

  auto tv = LOG_TIMED_VERBOSE;

  ent.reserve(fmap.size());
  for (auto& [k, fv] : fmap) {
    if (!fv.empty()) {
      if constexpr (UniqueOnly) {
        DWARFS_CHECK(fv.size() == fv.front()->refcount(), "internal error");
      }
      ent.emplace_back(std::move(k), std::move(fv));
    }
  }
  fmap.clear();

  std::sort(ent.begin(), ent.end(),
            [](auto& left, auto& right) { return left.first < right.first; });

  DWARFS_CHECK(fmap.empty(), "expected file map to be empty");

  finalize_inodes<true>(ent, inode_num, obj_num);
  if constexpr (!UniqueOnly) {
    finalize_inodes<false>(ent, inode_num, obj_num);
  }

  tv << "finalized " << ent.size() << (UniqueOnly ? " unique" : "") << " files";
}

template <typename LoggerPolicy>
template <bool Unique, typename KeyType>
void file_scanner_<LoggerPolicy>::finalize_inodes(
    std::vector<std::pair<KeyType, inode::files_vector>>& ent,
    uint32_t& inode_num, uint32_t& obj_num) {
  int const obj_num_before = obj_num;

  auto tv = LOG_TIMED_VERBOSE;

  for (auto& p : ent) {
    auto& files = p.second;

    if constexpr (Unique) {
      DWARFS_CHECK(!files.empty(),
                   fmt::format("internal error in finalize_inodes: empty files "
                               "vector for key {}",
                               p.first));

      // this is true regardless of how the files are ordered
      if (files.size() > files.front()->refcount()) {
        continue;
      }

      ++num_unique_;
    } else {
      if (files.empty()) {
        // This is fine: the !Unique version is *always* called after the Unique
        // version, which will have moved the unique file vectors.
        continue;
      }

      DWARFS_CHECK(files.size() > 1, "unexpected non-duplicate file");

      // needed for reproducibility
      std::sort(files.begin(), files.end(), [](file const* a, file const* b) {
        return a->less_revpath(*b);
      });
    }

    for (auto fp : files) {
      // need to check because hardlinks share the same number
      if (!fp->inode_num()) {
        fp->set_inode_num(inode_num);
        ++inode_num;
      }
    }

    auto fp = files.front();
    auto inode = fp->get_inode();
    assert(inode);
    inode->set_num(obj_num);
    inode->set_files(std::move(files));

    ++obj_num;
  }

  tv << "finalized " << (obj_num - obj_num_before) << (Unique ? " " : " non-")
     << "unique inodes";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_value(std::ostream& os,
                                             file const* p) const {
  std::shared_ptr<inode const> ino = p->get_inode();
  auto ino_num = p->inode_num();

  os << "{\n"
     << R"(        "ptr": ")"
     << fmt::format("{}", reinterpret_cast<void const*>(p)) << "\",\n"
     << R"(        "path": )" << nlohmann::json{p->path_as_string()}.dump()
     << ",\n"
     << R"(        "size": )" << fmt::format("{}", p->size()) << ",\n"
     << R"(        "refcnt": )" << fmt::format("{}", p->refcount()) << ",\n"
     << R"(        "hash": ")" << folly::hexlify(p->hash()) << "\",\n"
     << R"(        "invalid": )" << (p->is_invalid() ? "true" : "false")
     << ",\n"
     << R"(        "inode_num": )"
     << (ino_num ? fmt::format("{}", *ino_num) : "null") << ",\n"
     << R"(        "inode": ")"
     << fmt::format("{}", reinterpret_cast<void const*>(ino.get())) << "\"\n"
     << "      }";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_value(
    std::ostream& os, inode::files_vector const& vec) const {
  os << "[\n";
  bool first = true;
  for (auto p : vec) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    os << "      ";
    dump_value(os, p);
  }
  os << "\n    ]";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_inodes(std::ostream& os) const {
  os << "  \"inodes\": [\n";
  auto inodes = im_.sortable_span();
  inodes.all();
  bool first = true;
  for (auto const& ino : inodes) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    os << "    {\n"
       << R"(      "ptr": ")"
       << fmt::format("{}", reinterpret_cast<void const*>(ino.get())) << "\",\n"
       << R"(      "files": )";
    dump_value(os, ino->all());
    os << "\n    }";
  }
  os << "\n  ]";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_inode_create_info(
    std::ostream& os) const {
  os << "  \"inode_create_info\": [\n";
  bool first = true;
  for (auto const& ici : debug_inode_create_) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    os << "    {\n"
       << R"(      "inode": ")"
       << fmt::format("{}", reinterpret_cast<void const*>(ici.i)) << "\",\n"
       << R"(      "file": )";
    dump_value(os, ici.f);
    os << ",\n"
       << R"(      "line": )" << fmt::format("{}", ici.line) << "\n"
       << "    }";
  }
  os << "\n  ]";
}

template <typename LoggerPolicy>
template <typename T>
void file_scanner_<LoggerPolicy>::dump_map(std::ostream& os,
                                           std::string_view name,
                                           T const& map) const {
  os << "  \"" << name << "\": {\n";

  bool first = true;

  for (auto const& [k, v] : map) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    os << "    \"" << format_key(k) << "\": ";
    dump_value(os, v);
  }

  os << "\n  }";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump(std::ostream& os) const {
  std::lock_guard lock(mx_);

  os << "{\n";
  dump_map(os, "hardlinks", hardlinks_);
  os << ",\n";
  dump_map(os, "unique_size", unique_size_);
  os << ",\n";
  dump_map(os, "file_start_hash", file_start_hash_);
  os << ",\n";
  dump_map(os, "first_file_hashed", first_file_hashed_);
  os << ",\n";
  dump_map(os, "by_raw_inode", by_raw_inode_);
  os << ",\n";
  dump_map(os, "by_hash", by_hash_);
  os << ",\n";
  dump_inode_create_info(os);
  os << ",\n";
  dump_inodes(os);
  os << "\n}\n";
}

file_scanner::file_scanner(logger& lgr, worker_group& wg, os_access const& os,
                           inode_manager& im, progress& prog,
                           options const& opts)
    : impl_{make_unique_logging_object<impl, file_scanner_, logger_policies>(
          lgr, wg, os, im, prog, opts)} {}

} // namespace dwarfs::writer::internal

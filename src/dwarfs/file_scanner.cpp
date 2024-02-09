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
 */

#include <mutex>
#include <string_view>
#include <vector>

#include <folly/String.h>
#include <folly/container/F14Map.h>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/entry.h"
#include "dwarfs/file_scanner.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/worker_group.h"

namespace dwarfs::detail {

namespace {

constexpr size_t const kLargeFileThreshold = 1024 * 1024;
constexpr size_t const kLargeFileStartHashSize = 4096;

template <typename LoggerPolicy>
class file_scanner_ final : public file_scanner::impl {
 public:
  file_scanner_(logger& lgr, worker_group& wg, os_access const& os,
                inode_manager& im, std::optional<std::string> const& hash_algo,
                progress& prog);

  void scan(file* p) override;
  void finalize(uint32_t& inode_num) override;

  uint32_t num_unique() const override { return num_unique_; }

 private:
  class condition_barrier {
   public:
    void set() { ready_ = true; }

    void notify() { cv_.notify_all(); }

    void wait(std::unique_lock<std::mutex>& lock) {
      cv_.wait(lock, [this] { return ready_; });
    }

   private:
    std::condition_variable cv_;
    bool ready_{false};
  };

  void scan_dedupe(file* p);
  void hash_file(file* p);
  void add_inode(file* p);

  template <typename Lookup>
  void finalize_hardlinks(Lookup&& lookup);

  template <bool UniqueOnly = false, typename KeyType>
  void finalize_files(folly::F14FastMap<KeyType, inode::files_vector>& fmap,
                      uint32_t& inode_num, uint32_t& obj_num);

  template <bool Unique, typename KeyType>
  void
  finalize_inodes(std::vector<std::pair<KeyType, inode::files_vector>>& ent,
                  uint32_t& inode_num, uint32_t& obj_num);

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  os_access const& os_;
  inode_manager& im_;
  std::optional<std::string> const hash_algo_;
  progress& prog_;
  uint32_t num_unique_{0};
  folly::F14FastMap<uint64_t, inode::files_vector> hardlinks_;
  std::mutex mx_;
  // The pair stores the file size and optionally a hash of the first
  // 4 KiB of the file. If there's a collision, the worst that can
  // happen is that we unnecessary hash a file that is not a duplicate.
  folly::F14FastMap<std::pair<uint64_t, uint64_t>, inode::files_vector>
      unique_size_;
  // We need this lookup table to later find the unique_size_ entry
  // given just a file pointer.
  folly::F14FastMap<file const*, uint64_t> file_start_hash_;
  folly::F14FastMap<uint64_t, std::shared_ptr<condition_barrier>>
      first_file_hashed_;
  folly::F14FastMap<uint64_t, inode::files_vector> by_raw_inode_;
  folly::F14FastMap<std::string_view, inode::files_vector> by_hash_;
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
file_scanner_<LoggerPolicy>::file_scanner_(
    logger& lgr, worker_group& wg, os_access const& os, inode_manager& im,
    std::optional<std::string> const& hash_algo, progress& prog)
    : LOG_PROXY_INIT(lgr)
    , wg_(wg)
    , os_(os)
    , im_(im)
    , hash_algo_{hash_algo}
    , prog_(prog) {}

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

  if (hash_algo_) {
    scan_dedupe(p);
  } else {
    prog_.current.store(p);
    p->scan(nullptr, prog_, hash_algo_); // TODO

    by_raw_inode_[p->raw_inode_num()].push_back(p);

    add_inode(p);
  }
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::finalize(uint32_t& inode_num) {
  uint32_t obj_num = 0;

  assert(first_file_hashed_.empty());

  if (hash_algo_) {
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

  if (size >= kLargeFileThreshold && !p->is_invalid()) {
    try {
      auto mm = os_.map_file(p->fs_path(), kLargeFileStartHashSize);
      checksum cs(checksum::algorithm::XXH3_64);
      cs.update(mm->addr(), kLargeFileStartHashSize);
      cs.finalize(&start_hash);
      file_start_hash_.emplace(p, start_hash);
    } catch (...) {
      LOG_ERROR << "failed to map file " << p->path_as_string() << ": "
                << folly::exceptionStr(std::current_exception())
                << ", creating empty file";
      ++prog_.errors;
      p->set_invalid();
    }
  }

  auto [it, is_new] = unique_size_.emplace(std::make_pair(size, start_hash),
                                           inode::files_vector());

  if (is_new) {
    // A file size that has never been seen before. We can safely
    // create a new inode and we'll keep track of the file.
    it->second.push_back(p);

    {
      std::lock_guard lock(mx_);
      add_inode(p);
    }
  } else {
    // This file size has been seen before, so this is potentially
    // a duplicate.

    std::shared_ptr<condition_barrier> cv;

    if (it->second.empty()) {
      // This is any file of this size after the second file
      std::lock_guard lock(mx_);

      if (auto ffi = first_file_hashed_.find(size);
          ffi != first_file_hashed_.end()) {
        cv = ffi->second;
      }
    } else {
      // This is the second file of this size. We now need to hash
      // both the first and second file and ensure that the first
      // file's hash is stored to `by_hash_` first. We set up a
      // condition variable to synchronize insertion into `by_hash_`.

      cv = std::make_shared<condition_barrier>();

      {
        std::lock_guard lock(mx_);
        first_file_hashed_.emplace(size, cv);
      }

      // Add a job for the first file
      wg_.add_job([this, p = it->second.front(), cv] {
        hash_file(p);

        {
          std::lock_guard lock(mx_);

          assert(p->get_inode());

          if (p->is_invalid()) [[unlikely]] {
            by_raw_inode_[p->raw_inode_num()].push_back(p);
          } else {
            auto& ref = by_hash_[p->hash()];
            assert(ref.empty());
            ref.push_back(p);
          }

          cv->set();

          first_file_hashed_.erase(p->size());
        }

        cv->notify();
      });

      // Clear files vector, but don't delete the hash table entry,
      // to indicate that files of this size *must* be hashed.
      it->second.clear();
    }

    // Add a job for any subsequent files
    wg_.add_job([this, p, cv] {
      hash_file(p);

      {
        std::unique_lock lock(mx_);

        if (cv) {
          // Wait until the first file of this size has been added to
          // `by_hash_`.
          cv->wait(lock);
        }

        if (p->is_invalid()) [[unlikely]] {
          add_inode(p);
          by_raw_inode_[p->raw_inode_num()].push_back(p);
        } else {
          auto& ref = by_hash_[p->hash()];

          if (ref.empty()) {
            // This is *not* a duplicate. We must allocate a new inode.
            add_inode(p);
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
  std::unique_ptr<mmif> mm;

  if (size > 0) {
    try {
      mm = os_.map_file(p->fs_path(), size);
    } catch (...) {
      LOG_ERROR << "failed to map file " << p->path_as_string() << ": "
                << folly::exceptionStr(std::current_exception())
                << ", creating empty file";
      ++prog_.errors;
      p->set_invalid();
      return;
    }
  }

  prog_.current.store(p);
  p->scan(mm.get(), prog_, hash_algo_);
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::add_inode(file* p) {
  assert(!p->get_inode());

  auto inode = im_.create_inode();

  p->set_inode(inode);

  im_.scan_background(wg_, os_, std::move(inode), p);
}

template <typename LoggerPolicy>
template <typename Lookup>
void file_scanner_<LoggerPolicy>::finalize_hardlinks(Lookup&& lookup) {
  auto tv = LOG_TIMED_VERBOSE;

  for (auto& kv : hardlinks_) {
    auto& hlv = kv.second;
    if (hlv.size() > 1) {
      auto& fv = lookup(hlv.front());
      // TODO: for (auto p : hlv | std::views::drop(1))
      std::for_each(hlv.begin() + 1, hlv.end(), [&fv](auto p) {
        p->set_inode(fv.front()->get_inode());
        fv.push_back(p);
      });
    }
  }

  hardlinks_.clear();

  tv << "finalized " << hardlinks_.size() << " hardlinks";
}

template <typename LoggerPolicy>
template <bool UniqueOnly, typename KeyType>
void file_scanner_<LoggerPolicy>::finalize_files(
    folly::F14FastMap<KeyType, inode::files_vector>& fmap, uint32_t& inode_num,
    uint32_t& obj_num) {
  std::vector<std::pair<KeyType, inode::files_vector>> ent;

  auto tv = LOG_TIMED_VERBOSE;

  ent.reserve(fmap.size());
  fmap.eraseInto(
      fmap.begin(), fmap.end(), [&ent](KeyType&& k, inode::files_vector&& fv) {
        if (!fv.empty()) {
          if constexpr (UniqueOnly) {
            DWARFS_CHECK(fv.size() == fv.front()->refcount(), "internal error");
          }
          ent.emplace_back(std::move(k), std::move(fv));
        }
      });

  std::sort(ent.begin(), ent.end(),
            [](auto& left, auto& right) { return left.first < right.first; });

  DWARFS_CHECK(fmap.empty(), "expected file map to be empty");

  finalize_inodes<true>(ent, inode_num, obj_num);
  if constexpr (!UniqueOnly) {
    finalize_inodes<false>(ent, inode_num, obj_num);
  }

  tv << "finalized " << ent.size() << (UniqueOnly ? " " : " non-")
     << "unique files";
}

template <typename LoggerPolicy>
template <bool Unique, typename KeyType>
void file_scanner_<LoggerPolicy>::finalize_inodes(
    std::vector<std::pair<KeyType, inode::files_vector>>& ent,
    uint32_t& inode_num, uint32_t& obj_num) {
  for (auto& p : ent) {
    auto& files = p.second;

    if constexpr (Unique) {
      // this is true regardless of how the files are ordered
      if (files.size() > files.front()->refcount()) {
        continue;
      }

      ++num_unique_;
    } else {
      if (files.empty()) {
        continue;
      }

      DWARFS_CHECK(files.size() > 1, "unexpected non-duplicate file");
    }

    // needed for reproducibility
    std::sort(files.begin(), files.end(),
              [](file const* a, file const* b) { return a->less_revpath(*b); });

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
}
} // namespace

file_scanner::file_scanner(logger& lgr, worker_group& wg, os_access const& os,
                           inode_manager& im,
                           std::optional<std::string> const& hash_algo,
                           progress& prog)
    : impl_{make_unique_logging_object<impl, file_scanner_, logger_policies>(
          lgr, wg, os, im, hash_algo, prog)} {}

} // namespace dwarfs::detail

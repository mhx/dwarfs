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

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <parallel_hashmap/phmap.h>

#include <range/v3/view/drop.hpp>

#include <dwarfs/checksum.h>
#include <dwarfs/container/chunked_append_only_vector.h>
#include <dwarfs/dense_map_index.h>
#include <dwarfs/file_view.h>
#include <dwarfs/format.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/types.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/entry_id_vector.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/file_scanner.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer::internal {

using namespace dwarfs::internal;

namespace {

constexpr file_size_t const kLargeFileThreshold = 1024 * 1024;
constexpr file_size_t const kLargeFileStartHashSize = 4096;

template <typename T>
struct flat_cao_map_index_policy {
  using key_type = typename T::first_type;
  using store_type = container::chunked_append_only_vector<T>;
  using hash_type = default_value_hash<key_type>;
  using equal_type = std::equal_to<>;
  template <typename Hash, typename Equal>
  using index_type = phmap::parallel_flat_hash_set<std::uint32_t, Hash, Equal>;
};

class pair_inode_accessor {
 public:
  template <typename T>
  auto key(T const& p) const {
    return p.first;
  }

  template <typename T>
  file_id_vector& files(T& p) const {
    return p.second;
  }
};

class pair_ptr_inode_accessor {
 public:
  explicit pair_ptr_inode_accessor(std::size_t digest_size)
      : digest_size_{digest_size} {}

  template <typename T>
  std::string_view key(T const& p) const {
    return {p.first, digest_size_};
  }

  template <typename T>
  file_id_vector& files(T& p) const {
    return *p.second;
  }

 private:
  std::size_t digest_size_{0};
};

} // namespace

template <typename LoggerPolicy>
class file_scanner_ final : public file_scanner::impl {
 public:
  file_scanner_(logger& lgr, entry_storage& storage, worker_group& wg,
                os_access const& os, inode_manager& im, progress& prog,
                file_scanner::options const& opts);

  void scan(file_handle p) override;
  void finalize(uint32_t& inode_num) override;

  uint32_t num_unique() const override { return num_unique_; }

  void dump(std::ostream& os) const override;

 private:
  template <typename Key, typename Value>
  using fast_map_type = phmap::parallel_flat_hash_map<Key, Value>;

  using start_hash_key = std::pair<uint64_t, uint64_t>;

  using by_digest_vec = container::chunked_append_only_vector<file_id_vector>;

  template <typename Key>
  using dense_cao_map_index =
      basic_dense_map_index<Key, file_id_vector, flat_cao_map_index_policy>;

  void scan_dedupe(file_handle p, file_size_info size_info);
  void scan_dedupe_after_start_hash(file_handle p, file_size_info size_info,
                                    uint64_t start_hash);
  uint64_t compute_start_hash(file_handle p);
  void hash_file(file_handle p, file_size_t size);
  inode_handle add_inode(file_handle p, int lineno);
  void
  scan_inode(file_handle p, inode_handle ino, bool background = false) const;

  template <typename Lookup>
  void finalize_hardlinks(Lookup const& lookup);

  file_id_vector& hardlink_group(const_file_handle p);

  template <typename KeyType>
  void finalize_files(
      container::chunked_append_only_vector<std::pair<KeyType, file_id_vector>>
          ent,
      uint32_t& inode_num, uint32_t& obj_num, std::string_view name);

  void
  finalize_files(by_digest_vec fmap, uint32_t& inode_num, uint32_t& obj_num);

  template <bool Unique, typename SourceVecType, typename Accessor>
  void
  finalize_inodes(SourceVecType& ent, Accessor const& access,
                  uint32_t& inode_num, uint32_t& obj_num, std::string_view key);

  file_id_vector& get_by_digest_autovivify(file_handle p) {
    auto const index = p.digest_index().value();

    if (index >= by_digest_.size()) {
      by_digest_.resize(index + 1);
    }

    return by_digest_.at(index);
  }

  template <typename T>
  std::string format_key(T const& key) const {
    return fmt::format("{}", key);
  }

  template <typename T>
  std::string format_key(T const* key) const {
    return fmt::format("{}", reinterpret_cast<void const*>(key));
  }

  std::string format_key(file_id key) const {
    return std::to_string(key.index());
  }

  std::string format_key(std::span<std::byte const> key) const {
    return fmt::format("{}", hexlify(key));
  }

  void dump_value(std::ostream& os, std::integral auto val) const {
    os << std::to_string(val);
  }

  void dump_value(std::ostream& os, std::shared_ptr<std::latch> const&) const {
    os << "null";
  }

  void dump_value(std::ostream& os, const_file_handle p) const;
  void dump_value(std::ostream& os, file_id_vector const& vec) const;

  void dump_inodes(std::ostream& os) const;
  void dump_inode_create_info(std::ostream& os) const;

  template <typename T>
  void dump_map(std::ostream& os, std::string_view name, T const& map) const;

  void dump_map(std::ostream& os, std::string_view name,
                by_digest_vec const& map) const;

  LOG_PROXY_DECL(LoggerPolicy);
  entry_storage& storage_;
  worker_group& wg_;
  os_access const& os_;
  inode_manager& im_;
  progress& prog_;
  file_scanner::options const opts_;
  uint32_t num_unique_{0};
  std::mutex mutable mx_;

  // Hardlinked files
  container::chunked_append_only_vector<
      std::pair<unique_inode_id, file_id_vector>>
      hardlinks_store_;
  std::optional<dense_cao_map_index<unique_inode_id>> hardlinks_{
      hardlinks_store_};

  // Large files that have been seen exactly once by size only.
  //
  // For these files we have not computed a start hash yet. If another file with
  // the same size appears, this entry is promoted into start_hash_buckets_.
  container::chunked_append_only_vector<std::pair<uint64_t, file_id_vector>>
      size_buckets_store_;
  std::optional<dense_cao_map_index<uint64_t>> size_buckets_{
      size_buckets_store_};

  // Files classified by size and, for large files with a size collision, by the
  // hash of the first kLargeFileStartHashSize bytes.
  //
  // Small files use start_hash == 0.
  container::chunked_append_only_vector<
      std::pair<start_hash_key, file_id_vector>>
      start_hash_buckets_store_;
  std::optional<dense_cao_map_index<start_hash_key>> start_hash_buckets_{
      start_hash_buckets_store_};

  // For large files whose start hash was actually computed.
  //
  // A large file that remained unique by size will not appear here.
  fast_map_type<file_id, uint64_t> file_start_hash_;

  fast_map_type<start_hash_key, std::shared_ptr<std::latch>> first_file_hashed_;

  container::chunked_append_only_vector<
      std::pair<unique_inode_id, file_id_vector>>
      by_inode_id_store_;
  std::optional<dense_cao_map_index<unique_inode_id>> by_inode_id_{
      by_inode_id_store_};

  // There is likely no point in trying to store a digest pointer in this vector
  // to keep for sorting later. It would just use memory while we're scanning,
  // and at the time when we create the temporary vector for sorting, we've
  // already freed up lots of memory for the indices + hardlinks + basically all
  // other stores.
  by_digest_vec by_digest_;

  struct inode_create_info {
    inode_id i;
    file_id f; // TODO: file_id?
    int line;
  };
  std::vector<inode_create_info> debug_inode_create_;
};

// Files are classified in stages, so that we read as little file content as
// possible before we know that a file can be a duplicate at all.
//
// Stage 1: `size_buckets_`, keyed by file size (large files only)
//
//   Holds large files that we have seen exactly once, and of which we have not
//   read a single byte. The first large file of a given size cannot be a
//   duplicate of anything seen so far, so we can immediately create an inode
//   and immediately start similarity scanning for that inode.
//
//   Small files skip this stage: reading their first few KiB is not worth the
//   saving, so they enter stage 2 directly with a start hash of 0.
//
// Stage 2: `start_hash_buckets_`, keyed by (size, start hash)
//
//   As soon as a second large file of the same size shows up, size alone is no
//   longer a useful discriminator. Both the file parked in stage 1 and the new
//   one are start-hashed - an XXH3-64 over the first kLargeFileStartHashSize
//   bytes - and enter stage 2. Every file that gets start-hashed is recorded
//   in `file_start_hash_`, which is how `finalize_hardlinks` can later tell
//   which bucket a file ended up in.
//
//   The start hash is useful if there are a lot of large files with the same
//   size. One potential scenario is uncompressed images, which are very likely
//   to have the same size, but very unlikely to have the same contents. The
//   choice of 4 KiB is arbitrary, as is the threshold of 1 MiB for "large
//   files". The start hash is computed synchronously, so this could be a
//   potential bottleneck; however, it is only computed on a size collision. A
//   collision on (size, start hash) is harmless: the worst that can happen is
//   that we unnecessarily hash a file that is not a duplicate.
//
//   Again, the first file of a bucket cannot be a duplicate, so it gets an
//   inode right away - unless it already got one back in stage 1.
//
// Stage 3: `by_digest_`, keyed by the digest of the full file contents
//
//   When a second file lands in the same stage 2 bucket, both files must be
//   fully hashed, using the user-provided algorithm, to see if they are
//   identical. We already have an inode for the first file, so we must delay
//   the creation of a new inode for the second file until we know that it is
//   not a duplicate. Exactly the same applies to subsequent files.
//
// Ordering between the first file of a bucket and the rest
//
//   We must ensure that the presence of a digest is checked in `by_digest_`
//   for subsequent files only if the first file's digest has been computed and
//   stored. Otherwise, if a subsequent file's hash computation finishes before
//   the first file, we assume (potentially wrongly) that the subsequent file is
//   not a duplicate.
//
//   `first_file_hashed_` holds a latch per (size, start hash) for exactly as
//   long as that window is open: it is inserted when the second file of a
//   bucket arrives, and erased once the first file's digest has been stored.
//   A file that finds no latch for its bucket knows that the first file is
//   already in `by_digest_` and does not have to wait.
//
//   A bucket whose file vector has been cleared, but whose key is still
//   present, means "files of this key must be fully hashed". This is how the
//   scanning thread tells the second file of a bucket from all later ones.
//
// Files that are not deduplicated
//
//   A file that is already invalid, or that becomes invalid while being read,
//   never takes part in deduplication. It is collected in `by_inode_id_` and
//   always gets an inode of its own. The same is true for every file if no
//   hash algorithm is configured, in which case deduplication is disabled and
//   `by_inode_id_` is the only table that is populated.

template <typename LoggerPolicy>
file_scanner_<LoggerPolicy>::file_scanner_(logger& lgr, entry_storage& storage,
                                           worker_group& wg,
                                           os_access const& os,
                                           inode_manager& im, progress& prog,
                                           file_scanner::options const& opts)
    : LOG_PROXY_INIT(lgr)
    , storage_(storage)
    , wg_(wg)
    , os_(os)
    , im_(im)
    , prog_(prog)
    , opts_{opts} {}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan(file_handle p) {
  // This method is supposed to be called from a single thread only.

  if (p.num_hard_links() > 1) {
    auto& vec = hardlinks_->mapped(p.get_unique_inode_id());
    vec.push_back(p.id());

    if (vec.size() > 1) {
      p.hardlink(storage_.handle(vec[0]), prog_);
      ++prog_.files_scanned;
      return;
    }
  }

  p.create_data();

  auto const size_info = p.size_info();

  prog_.original_size += size_info.total;
  prog_.allocated_original_size += size_info.allocated;

  if (opts_.hash_files) {
    scan_dedupe(p, size_info);
  } else {
    prog_.current.store(p);
    by_inode_id_->mapped(p.get_unique_inode_id()).push_back(p.id());
    scan_inode(p, add_inode(p, __LINE__), true);
  }
}

template <typename LoggerPolicy>
uint64_t file_scanner_<LoggerPolicy>::compute_start_hash(file_handle p) {
  uint64_t start_hash{0};

  if (!p.is_invalid()) {
    try {
      auto seg =
          os_.open_file(p.fs_path()).segment_at(0, kLargeFileStartHashSize);

      checksum cs(checksum::xxh3_64);
      cs.update(seg.span());
      cs.finalize(&start_hash);
    } catch (...) {
      LOG_ERROR << "failed to map file " << p.path_as_string() << ": "
                << exception_str(std::current_exception())
                << ", creating empty file";

      ++prog_.errors;
      p.set_invalid();
    }
  }

  return start_hash;
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::finalize(uint32_t& inode_num) {
  uint32_t obj_num = 0;

  assert(first_file_hashed_.empty());

  auto table_stats = [](auto const& tbl) {
    constexpr auto entry_size =
        sizeof(typename std::decay_t<decltype(tbl)>::value_type);
    return fmt::format("{} ({}/{} entries, {} bytes per entry)",
                       size_with_unit(entry_size * tbl.capacity()), tbl.size(),
                       tbl.capacity(), entry_size);
  };

  auto index_stats = [](auto const& idx) {
    return fmt::format("{}", size_with_unit(idx.index_size_in_bytes()));
  };

  LOG_VERBOSE << "file scanner table stats:"
              << "\n  hardlinks store: " << table_stats(hardlinks_store_)
              << "\n  hardlinks index: " << index_stats(hardlinks_.value())
              << "\n  size-buckets store: " << table_stats(size_buckets_store_)
              << "\n  size-buckets index: "
              << index_stats(size_buckets_.value())
              << "\n  start-hash-buckets store: "
              << table_stats(start_hash_buckets_store_)
              << "\n  start-hash-buckets index: "
              << index_stats(start_hash_buckets_.value())
              << "\n  file-start-hash: " << table_stats(file_start_hash_)
              << "\n  first-file-hashed: " << table_stats(first_file_hashed_)
              << "\n  by-inode-id store: " << table_stats(by_inode_id_store_)
              << "\n  by-inode-id index: " << index_stats(by_inode_id_.value())
              << "\n  by-digest: " << table_stats(by_digest_);

  hardlinks_.reset();

  if (opts_.hash_files) {
    finalize_hardlinks([this](const_file_handle p) -> file_id_vector& {
      return hardlink_group(p);
    });

    // `file_start_hash_` is only needed to resolve hardlink groups above
    file_start_hash_.clear();

    // the indices are also no longer needed
    size_buckets_.reset();
    start_hash_buckets_.reset();
    by_inode_id_.reset();

    // Only `by_digest_` can hold groups of more than one distinct file; every
    // group in the other maps is a single file plus its own hardlinks.
    finalize_files(std::move(size_buckets_store_), inode_num, obj_num, "size");
    finalize_files(std::move(start_hash_buckets_store_), inode_num, obj_num,
                   "start hash");
    finalize_files(std::move(by_inode_id_store_), inode_num, obj_num, "inode");
    finalize_files(std::move(by_digest_), inode_num, obj_num);
  } else {
    finalize_hardlinks([this](const_file_handle p) -> file_id_vector& {
      return by_inode_id_->mapped_at(p.get_unique_inode_id());
    });

    by_inode_id_.reset();

    finalize_files(std::move(by_inode_id_store_), inode_num, obj_num, "inode");
  }
}

template <typename LoggerPolicy>
file_id_vector&
file_scanner_<LoggerPolicy>::hardlink_group(const_file_handle p) {
  // Invalid files never take part in deduplication.
  if (p.is_invalid()) {
    if (auto ix = by_inode_id_->index_of(p.get_unique_inode_id())) {
      return by_inode_id_store_[*ix].second;
    }
  }

  // Files that were fully hashed are in `by_digest_`.
  if (auto ix = p.digest_index()) {
    return by_digest_.at(*ix);
  }

  auto const size = p.size();

  // Large files that were never fully hashed are still in one of the bucket
  // maps; which one depends on whether they were start-hashed.
  if (size >= kLargeFileThreshold) [[unlikely]] {
    if (auto it = file_start_hash_.find(p.id()); it != file_start_hash_.end()) {
      return start_hash_buckets_->mapped_at(start_hash_key{size, it->second});
    }

    return size_buckets_->mapped_at(size);
  }

  return start_hash_buckets_->mapped_at(start_hash_key{size, 0});
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan_dedupe(file_handle p,
                                              file_size_info const size_info) {
  uint64_t const size = size_info.total;

  LOG_TRACE << "scanning file " << p.path_as_string() << " [size=" << size
            << "]";

  // Small files are classified directly by size. We do not compute a start hash
  // for them.
  if (size < kLargeFileThreshold || p.is_invalid()) {
    scan_dedupe_after_start_hash(p, size_info, 0);
    return;
  }

  auto [ix, is_new] = size_buckets_->emplace(size, file_id_vector());
  auto& files = size_buckets_store_[ix].second;

  if (is_new) {
    // First large file of this size. It is unique with respect to all files
    // seen so far, so create an inode now and do not read file contents.
    files.push_back(p.id());

    inode_handle inode;

    {
      std::lock_guard lock(mx_);
      inode = add_inode(p, __LINE__);
    }

    scan_inode(p, inode, true);

    return;
  }

  if (!files.empty()) {
    // This is the second large file of this size. Promote the previously
    // unique-by-size file into the size+start-hash stage.
    auto first = storage_.handle(files.front());

    // Clear but keep the size entry. An empty vector means: this size is no
    // longer unique-by-size; future files of this size must be start-hashed.
    files.clear();

    auto const first_start_hash = compute_start_hash(first);
    file_start_hash_.emplace(first.id(), first_start_hash);

    scan_dedupe_after_start_hash(first, first.size_info(), first_start_hash);
  }

  // This is either the second file of this size, or a later one. From now on,
  // all files of this large size need the start-hash discriminator.
  auto const start_hash = compute_start_hash(p);
  file_start_hash_.emplace(p.id(), start_hash);

  scan_dedupe_after_start_hash(p, size_info, start_hash);
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan_dedupe_after_start_hash(
    file_handle p, file_size_info const size_info, uint64_t const start_hash) {
  uint64_t const size = size_info.total;
  auto const unique_key = std::make_pair(size, start_hash);

  auto [ix, is_new] =
      start_hash_buckets_->emplace(unique_key, file_id_vector());
  auto& files = start_hash_buckets_store_[ix].second;

  if (is_new) {
    // A file (size, start_hash) that has never been seen before. We can safely
    // create a new inode, unless this file already got one when it was first
    // classified as unique-by-size.
    files.push_back(p.id());

    if (!p.get_inode()) {
      inode_handle inode;

      {
        std::lock_guard lock(mx_);
        inode = add_inode(p, __LINE__);
      }

      scan_inode(p, inode, true);
    }

    return;
  }

  // This file (size, start_hash) has been seen before, so this is potentially
  // a duplicate.

  std::shared_ptr<std::latch> latch;

  if (files.empty()) {
    // This is any file of this (size, start_hash) after the second file.
    std::lock_guard lock(mx_);

    if (auto ffi = first_file_hashed_.find(unique_key);
        ffi != first_file_hashed_.end()) {
      latch = ffi->second;
    }
  } else {
    // This is the second file of this (size, start_hash). We now need to hash
    // both files and ensure that the first file's digest is stored to
    // by_digest_ before later files test for it.

    latch = std::make_shared<std::latch>(1);

    {
      std::lock_guard lock(mx_);
      DWARFS_CHECK(first_file_hashed_.emplace(unique_key, latch).second,
                   "internal error: first file hashed latch already exists");
    }

    // Add a job for the first file.
    wg_.add_job(
        [this, p = storage_.handle(files.front()), latch, unique_key, size] {
          hash_file(p, size);

          {
            std::lock_guard lock(mx_);

            assert(p.get_inode());

            if (p.is_invalid()) [[unlikely]] {
              by_inode_id_->mapped(p.get_unique_inode_id()).push_back(p.id());
            } else {
              auto& ref = get_by_digest_autovivify(p);
              ref.push_back(p.id());
            }

            latch->count_down();

            DWARFS_CHECK(first_file_hashed_.erase(unique_key) > 0,
                         "internal error: missing first file hashed latch");
          }
        });

    // Clear files vector, but keep the hash table entry to indicate that files
    // of this (size, start_hash) must be fully hashed.
    files.clear();
  }

  // Add a job for the current file.
  wg_.add_job([this, p, latch, size_info] mutable {
    hash_file(p, size_info.total);

    if (latch) {
      // Wait until the first file of this (size, start_hash) has been added to
      // by_digest_.
      latch->wait();
    }

    std::optional<inode_handle> inode_to_scan;

    {
      std::unique_lock lock(mx_);

      if (p.is_invalid()) [[unlikely]] {
        inode_to_scan = add_inode(p, __LINE__);
        by_inode_id_->mapped(p.get_unique_inode_id()).push_back(p.id());
      } else {
        auto& ref = get_by_digest_autovivify(p);

        if (ref.empty()) {
          // This is not a duplicate. We must allocate a new inode.
          inode_to_scan = add_inode(p, __LINE__);
        } else {
          auto inode = storage_.handle(ref.front()).get_inode();
          assert(inode);

          p.set_inode(inode);
          ++prog_.files_scanned;
          ++prog_.duplicate_files;
          prog_.saved_by_deduplication += size_info.total;
          prog_.allocated_saved_by_deduplication += size_info.allocated;
        }

        ref.push_back(p.id());
      }
    }

    if (inode_to_scan) {
      scan_inode(p, *inode_to_scan);
    }
  });
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::hash_file(file_handle p,
                                            file_size_t const size) {
  if (p.is_invalid()) {
    return;
  }

  file_view mm;

  if (size > 0) {
    // TODO: use exception-less variant once provided
    try {
      mm = os_.open_file(p.fs_path());
    } catch (...) {
      LOG_ERROR << "failed to map file " << p.path_as_string() << ": "
                << exception_str(std::current_exception())
                << ", creating empty file";
      ++prog_.errors;
      p.set_invalid();
      return;
    }

    if (mm.size() != size) {
      LOG_ERROR << "file size changed for " << p.path_as_string()
                << ", creating empty file";
      ++prog_.errors;
      p.set_invalid();
      return;
    }
  }

  prog_.current.store(p);
  p.scan(mm, prog_, opts_.hash_files);
}

template <typename LoggerPolicy>
inode_handle file_scanner_<LoggerPolicy>::add_inode(file_handle p, int lineno) {
  assert(!p.get_inode());

  auto inode = storage_.create_inode();

  p.set_inode(inode.id());

  if (opts_.debug_inode_create) {
    debug_inode_create_.push_back({inode.id(), p.id(), lineno});
  }

  return inode;
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::scan_inode(file_handle p, inode_handle inode,
                                             bool background) const {
  im_.scan(os_, inode, p, background ? &wg_ : nullptr);
}

template <typename LoggerPolicy>
template <typename Lookup>
void file_scanner_<LoggerPolicy>::finalize_hardlinks(Lookup const& lookup) {
  auto tv = LOG_TIMED_VERBOSE;

  auto hardlinks = std::move(hardlinks_store_);

  size_t groups_finalized = 0;
  size_t links_finalized = 0;

  for (auto& kv : hardlinks) {
    auto& hlv = kv.second;

    if (hlv.size() > 1) {
      auto& fv = lookup(storage_.handle(hlv.front()));

      for (auto p : ranges::views::drop(hlv, 1)) {
        auto handle = storage_.handle(p);
        handle.set_inode(storage_.handle(fv.front()).get_inode());
        fv.push_back(p);

        ++links_finalized;
      }

      ++groups_finalized;
    }
  }

  tv << "finalized " << links_finalized << " hardlinks in " << groups_finalized
     << " groups";
}

template <typename LoggerPolicy>
template <typename KeyType>
void file_scanner_<LoggerPolicy>::finalize_files(
    container::chunked_append_only_vector<std::pair<KeyType, file_id_vector>>
        ent,
    uint32_t& inode_num, uint32_t& obj_num, std::string_view name) {
  auto tv = LOG_TIMED_VERBOSE;

  auto const tail = std::ranges::partition(
      ent, [](auto const& pair) { return !pair.second.empty(); });
  ent.resize(std::distance(ent.begin(), tail.begin()));

  std::ranges::sort(
      ent, [](auto& left, auto& right) { return left.first < right.first; });

  for (auto const& [k, fv] : ent) {
    auto const expected = storage_.handle(fv.front()).hardlink_count();
    DWARFS_CHECK(fv.size() == expected,
                 fmt::format("internal error while finalizing {} files: "
                             "hardlink count mismatch ({} != {})",
                             name, fv.size(), expected));
  }

  finalize_inodes<true>(ent, pair_inode_accessor{}, inode_num, obj_num, name);

  tv << "finalized " << ent.size() << " unique files (by " << name << ")";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::finalize_files(by_digest_vec fmap,
                                                 uint32_t& inode_num,
                                                 uint32_t& obj_num) {
  auto tv = LOG_TIMED_VERBOSE;

  std::vector<std::pair<char const*, file_id_vector*>> ent;
  ent.reserve(fmap.size());
  size_t digest_size{0};

  for (auto&& fv : fmap) {
    if (!fv.empty()) {
      auto const digest = storage_.handle(fv.front()).digest();
      ent.emplace_back(reinterpret_cast<char const*>(digest.data()), &fv);
      digest_size = digest.size();
    }
  }

  std::ranges::sort(ent, [digest_size](auto& left, auto& right) {
    return std::memcmp(left.first, right.first, digest_size) < 0;
  });

  pair_ptr_inode_accessor accessor{digest_size};

  finalize_inodes<true>(ent, accessor, inode_num, obj_num, "digest");
  finalize_inodes<false>(ent, accessor, inode_num, obj_num, "digest");

  tv << "finalized " << ent.size() << " files (by digest)";
}

template <typename LoggerPolicy>
template <bool Unique, typename SourceVecType, typename Accessor>
void file_scanner_<LoggerPolicy>::finalize_inodes(SourceVecType& ent,
                                                  Accessor const& access,
                                                  uint32_t& inode_num,
                                                  uint32_t& obj_num,
                                                  std::string_view key) {
  int const obj_num_before = obj_num;

  auto tv = LOG_TIMED_VERBOSE;

  for (auto& p : ent) {
    auto& files = access.files(p);

    if constexpr (Unique) {
      DWARFS_CHECK(!files.empty(),
                   fmt::format("internal error while finalizing {} inodes: "
                               "empty files vector for key {}",
                               key, access.key(p)));

      // this is true regardless of how the files are ordered
      if (files.size() > storage_.handle(files.front()).hardlink_count()) {
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
      storage_.sort_file_id_vector(files);
    }

    for (auto fp : files) {
      auto fh = storage_.handle(fp);
      // need to check because hardlinks share the same number
      if (!fh.inode_num()) {
        fh.set_inode_num(inode_num);
        ++inode_num;
      }
    }

    auto fh = storage_.handle(files.front());
    auto inode = storage_.handle(fh.get_inode());
    assert(inode);
    inode.set_num(obj_num);
    inode.set_files(files);
    files.clear();

    ++obj_num;
  }

  tv << "finalized " << (obj_num - obj_num_before) << (Unique ? " " : " non-")
     << "unique inodes (by " << key << ")";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_value(std::ostream& os,
                                             const_file_handle p) const {
  auto ino = p.get_inode();
  auto ino_num = p.inode_num();

  os << "{\n"
     << R"(        "index": )" << std::to_string(p.id().index()) << ",\n"
     << R"(        "path": )" << nlohmann::json{p.path_as_string()}.dump()
     << ",\n"
     << R"(        "size": )" << std::to_string(p.size()) << ",\n"
     << R"(        "nlink": )" << std::to_string(p.hardlink_count()) << ",\n"
     << R"(        "digest": ")" << hexlify(p.digest()) << "\",\n"
     << R"(        "invalid": )" << (p.is_invalid() ? "true" : "false") << ",\n"
     << R"(        "inode_num": )"
     << (ino_num ? std::to_string(*ino_num) : "null") << ",\n"
     << R"(        "inode": )" << std::to_string(ino.index()) << "\n"
     << "      }";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump_value(std::ostream& os,
                                             file_id_vector const& vec) const {
  os << "[\n";
  bool first = true;
  for (auto p : vec) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    os << "      ";
    dump_value(os, storage_.handle(p));
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
       << R"(      "index": )" << std::to_string(ino.id().index()) << ",\n"
       << R"(      "files": )";
    dump_value(os, ino.all_file_ids());
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
       << R"(      "inode": )" << std::to_string(ici.i.index()) << ",\n"
       << R"(      "file": )";
    dump_value(os, storage_.handle(ici.f));
    os << ",\n"
       << R"(      "line": )" << std::to_string(ici.line) << "\n"
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
void file_scanner_<LoggerPolicy>::dump_map(std::ostream& os,
                                           std::string_view name,
                                           by_digest_vec const& map) const {
  os << "  \"" << name << "\": {\n";

  bool first = true;

  for (auto const& v : map) {
    if (!first) {
      os << ",\n";
    }
    first = false;
    auto fh = storage_.handle(v.front());
    os << "    \"" << format_key(fh.digest()) << "\": ";
    dump_value(os, v);
  }

  os << "\n  }";
}

template <typename LoggerPolicy>
void file_scanner_<LoggerPolicy>::dump(std::ostream& os) const {
  std::lock_guard lock(mx_);

  os << "{\n";
  dump_map(os, "hardlinks", hardlinks_store_);
  os << ",\n";
  dump_map(os, "size_buckets", size_buckets_store_);
  os << ",\n";
  dump_map(os, "start_hash_buckets", start_hash_buckets_store_);
  os << ",\n";
  dump_map(os, "file_start_hash", file_start_hash_);
  os << ",\n";
  dump_map(os, "first_file_hashed", first_file_hashed_);
  os << ",\n";
  dump_map(os, "by_inode_id", by_inode_id_store_);
  os << ",\n";
  dump_map(os, "by_digest", by_digest_);
  os << ",\n";
  dump_inode_create_info(os);
  os << ",\n";
  dump_inodes(os);
  os << "\n}\n";
}

file_scanner::file_scanner(logger& lgr, entry_storage& storage,
                           worker_group& wg, os_access const& os,
                           inode_manager& im, progress& prog,
                           options const& opts)
    : impl_{make_unique_logging_object<impl, file_scanner_,
                                       default_logger_policy>(
          lgr, storage, wg, os, im, prog, opts)} {}

} // namespace dwarfs::writer::internal

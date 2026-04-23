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

#include <cassert>
#include <numeric>
#include <ostream>
#include <sstream>
#include <utility>

#include <boost/container_hash/hash.hpp>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/config.h>
#include <dwarfs/container/chunked_append_only_vector.h>
#include <dwarfs/container/compact_packed_int_vector.h>
#include <dwarfs/container/map_utils.h>
#include <dwarfs/container/packed_value_traits_optional.h>
#include <dwarfs/container/pinned_byte_span_store.h>
#include <dwarfs/container/segmented_packed_int_vector.h>
#include <dwarfs/conv.h>
#include <dwarfs/dense_value_index.h>
#include <dwarfs/error.h>
#include <dwarfs/match.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/inode_fragments.h>
#include <dwarfs/writer/metadata_options.h>

#include <dwarfs/internal/synchronized.h>
#include <dwarfs/writer/internal/entry_id_vector.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/progress.h>

// #define DWARFS_TRACE_ENTRY_STORAGE_CALLS

#ifndef DWARFS_STACKTRACE_ENABLED
#undef DWARFS_TRACE_ENTRY_STORAGE_CALLS
#endif

#ifdef DWARFS_TRACE_ENTRY_STORAGE_CALLS
#include <dwarfs/internal/event_tracer.h>
#define TRACE_CALL ev_.trace(std::source_location::current().function_name())
#else
#define TRACE_CALL                                                             \
  do {                                                                         \
  } while (0)
#endif

#ifdef _WIN32
#define DWARFS_KEEP_FS_PATHS 1
#endif

namespace fs = std::filesystem;

namespace dwarfs::writer::internal {

namespace {

// TODO: rethink if we still need visitors - it might make more sense
//       to just have some `for_each_device` etc. methods and those
//       might play better with the storage idea (but then again, they
//       may not, because we don't know upfront which fields are going
//       to be accessed) - still, the visitors seem a bit overkill

template <dwarfs::container::packed_vector_value T,
          std::size_t SegmentSize = 4096>
using segtor = dwarfs::container::segmented_packed_int_vector<T, SegmentSize>;

template <typename T>
bool uses_inline_buffer(T const& s) {
  auto const p = reinterpret_cast<std::uintptr_t>(s.data());
  auto const b = reinterpret_cast<std::uintptr_t>(&s);
  auto const e = b + sizeof(s);
  return b <= p && p < e;
}

} // namespace

class path_component {
 public:
  path_component() = default;
  path_component(fs::path const& path, bool is_root)
#if DWARFS_KEEP_FS_PATHS
      : path_{is_root ? path : path.filename()}
      , name_{path_to_utf8_string_sanitized(path_)}
#else
      : name_{path_to_utf8_string_sanitized(is_root ? path : path.filename())}
#endif
  {
  }

  friend bool
  operator==(path_component const&, path_component const&) = default;

  fs::path path() const {
#if DWARFS_KEEP_FS_PATHS
    return path_;
#else
    return {name_};
#endif
  }

  std::string_view name() const { return name_; }

  std::size_t size_in_bytes() const {
#if DWARFS_KEEP_FS_PATHS
    return sizeof(path_component) +
           path_.native().size() * sizeof(fs::path::value_type) + name_.size();
#else
    return sizeof(path_component) +
           (uses_inline_buffer(name_) ? 0 : name_.capacity());
#endif
  }

 private:
  friend struct std::hash<path_component>;

#if DWARFS_KEEP_FS_PATHS
  fs::path path_;
#endif
  std::string name_;
};

} // namespace dwarfs::writer::internal

template <>
struct std::hash<dwarfs::writer::internal::path_component> {
  std::size_t operator()(
      dwarfs::writer::internal::path_component const& pc) const noexcept {
    std::size_t seed = 0;
#if DWARFS_KEEP_FS_PATHS
    boost::hash_combine(seed, pc.path_);
#endif
    boost::hash_combine(seed, pc.name_);
    return seed;
  }
};

namespace dwarfs::writer::internal {
namespace {

constexpr char kLocalPathSeparator{
    static_cast<char>(fs::path::preferred_separator)};

using inode_scan_error = std::pair<file_id, std::exception_ptr>;

bool is_root_path(std::string_view path) {
#ifdef _WIN32
  return path == "/" || path == "\\";
#else
  return path == "/";
#endif
}

template <typename T>
using cao_vector = dwarfs::container::chunked_append_only_vector<T>;

template <typename T>
struct flat_cao_dense_value_index_policy {
  using store_type = cao_vector<T>;
  using hash_type = default_value_hash<T>;
  using equal_type = std::equal_to<>;
  template <typename Hash, typename Equal>
  using index_type = phmap::flat_hash_set<std::size_t, Hash, Equal>;
};

template <typename T>
using flat_cao_index =
    dwarfs::basic_dense_value_index<T, flat_cao_dense_value_index_policy>;

template <typename T>
std::uint64_t total_cao_id_vec_bytes(cao_vector<T> const& vec) {
  return std::accumulate(vec.begin(), vec.end(), 0ULL,
                         [](std::size_t acc, auto const& de) {
                           return acc +
                                  (de.is_inline() ? 0 : de.size_in_bytes());
                         }) +
         sizeof(vec[0]) * vec.size();
}

struct shared_entry_data {
 public:
  void drop_indices() {
    path_index_.reset();
    device_index_.reset();
    mode_index_.reset();
    uid_index_.reset();
    gid_index_.reset();
    link_target_index_.reset();
  }

  auto add_path_component(fs::path const& component, bool is_root) {
    return path_index_->add(component, is_root);
  }

  auto add_device(file_stat::dev_type dev) { return device_index_->add(dev); }

  auto add_mode(file_stat::mode_type mode) { return mode_index_->add(mode); }

  auto add_uid(file_stat::uid_type uid) { return uid_index_->add(uid); }

  auto add_gid(file_stat::gid_type gid) { return gid_index_->add(gid); }

  auto add_link_target(std::string link) {
    return link_target_index_->add(std::move(link));
  }

  void add_dir_entry_vec() { dir_entries_.emplace_back(); }

  void add_dir_entry(dir_id parent, entry_type type, std::uint64_t entry_ix);

  void dump(std::ostream& os) const;

  auto get_path_component(size_t index) const -> path_component const& {
    return path_components_.at(index);
  }

  auto get_mode(size_t index) const -> file_stat::mode_type {
    return modes_.at(index);
  }

  auto get_uid(size_t index) const -> file_stat::uid_type {
    return uids_.at(index);
  }

  auto get_gid(size_t index) const -> file_stat::gid_type {
    return gids_.at(index);
  }

  auto get_device(size_t index) const -> file_stat::dev_type {
    return devices_.at(index);
  }

  auto get_link_target(size_t index) const -> std::string_view {
    return link_targets_.at(index);
  }

  auto get_dir_entries(dir_id dir) -> entry_id_vector& {
    assert(dir.valid());
    return dir_entries_.at(dir.index());
  }

  auto get_dir_entries(dir_id dir) const -> entry_id_vector const& {
    assert(dir.valid());
    return dir_entries_.at(dir.index());
  }

  template <typename Predicate>
  void sort_all_dir_entries(Predicate const& pred) {
    for (auto& de : dir_entries_) {
      std::ranges::sort(de, pred);
    }
  }

 private:
  cao_vector<path_component> path_components_;
  std::optional<flat_cao_index<path_component>> path_index_{path_components_};

  cao_vector<file_stat::dev_type> devices_;
  std::optional<flat_cao_index<file_stat::dev_type>> device_index_{devices_};

  cao_vector<file_stat::mode_type> modes_;
  std::optional<flat_cao_index<file_stat::mode_type>> mode_index_{modes_};

  cao_vector<file_stat::uid_type> uids_;
  std::optional<flat_cao_index<file_stat::uid_type>> uid_index_{uids_};

  cao_vector<file_stat::gid_type> gids_;
  std::optional<flat_cao_index<file_stat::gid_type>> gid_index_{gids_};

  cao_vector<std::string> link_targets_;
  std::optional<flat_cao_index<std::string>> link_target_index_{link_targets_};

  // indexed by dir index, contains all entry ids of the directory
  cao_vector<entry_id_vector> dir_entries_;
};

class packed_entry_data {
 public:
  struct add_entry_result {
    std::size_t entry_index;
    std::size_t path_index;
  };

  explicit packed_entry_data(entry_type t)
      : this_type_{t} {}

  packed_entry_data(entry_type t, metadata_options const& options)
      : this_type_{t}
      , keep_all_times_{options.keep_all_times}
      , keep_subsecond_{options.time_resolution.value_or(std::chrono::seconds(
                            1)) < std::chrono::seconds(1)} {}

  static constexpr std::size_t kNlinkMinusOneField = 0;
  static constexpr std::size_t kModeIndexField = 1;
  static constexpr std::size_t kUidIndexField = 2;
  static constexpr std::size_t kGidIndexField = 3;
  static constexpr std::size_t kAccessTimeSecondField = 4;
  static constexpr std::size_t kAccessTimeSubsecondField = 5;
  static constexpr std::size_t kModificationTimeSecondField = 6;
  static constexpr std::size_t kModificationTimeSubsecondField = 7;
  static constexpr std::size_t kStatusChangeTimeSecondField = 8;
  static constexpr std::size_t kStatusChangeTimeSubsecondField = 9;
  static constexpr std::size_t kInodeField = 10;
  static constexpr std::size_t kDeviceIndexField = 11;
  using stat_common_tuple =
      std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                 std::int64_t, std::uint64_t, std::int64_t, std::uint64_t,
                 std::int64_t, std::uint64_t, std::uint64_t, std::uint64_t>;

  bool empty() const { return path_name_index_.empty(); }

  void dump(std::ostream& os, std::string_view name) const;

  add_entry_result
  add_entry_common(shared_entry_data& shared, entry_type type,
                   fs::path const& path, file_stat const& st, dir_id parent);

  void add_file_specific() {
    assert(this_type_ == entry_type::E_FILE);
    file_data_index_.push_back(std::nullopt);
    file_inode_id_.push_back(inode_id{});
    file_order_index_.push_back(0);
  }

  void add_link_specific() {
    assert(this_type_ == entry_type::E_LINK);
    link_target_index_.push_back(std::nullopt);
  }

  void add_device_specific(file_stat const& st) {
    assert(this_type_ == entry_type::E_DEVICE);
    represented_device_.push_back(st.rdev_unchecked());
  }

  dir_id get_parent(uint64_t const index) const {
    return parent_dir_id_.at(index);
  }

  fs::path
  get_path(shared_entry_data const& shared, uint64_t const index) const {
    auto const path_ix = path_name_index_.at(index);
    return shared.get_path_component(path_ix).path();
  }

  std::string_view
  get_path_string(shared_entry_data const& shared, uint64_t const index) const {
    auto const path_ix = path_name_index_.at(index);
    return shared.get_path_component(path_ix).name();
  }

  void update_global_entry_data(shared_entry_data const& shared,
                                uint64_t const index,
                                global_entry_data& data) const {
    auto const& stat = stat_common_.at(index);

    data.add_mode(shared.get_mode(get<kModeIndexField>(stat)));
    data.add_uid(shared.get_uid(get<kUidIndexField>(stat)));
    data.add_gid(shared.get_gid(get<kGidIndexField>(stat)));
    data.add_mtime(get<kModificationTimeSecondField>(stat));
    if (keep_all_times_) {
      data.add_atime(get<kAccessTimeSecondField>(stat));
      data.add_ctime(get<kStatusChangeTimeSecondField>(stat));
    }
  }

  void
  pack_entry(shared_entry_data const& shared, uint64_t const index,
             thrift::metadata::metadata::inodes_member_type::reference entry_v2,
             global_entry_data const& data,
             time_resolution_converter const& timeres) const {
    auto const& stat = stat_common_.at(index);
    file_stat out{};

    out.set_mode(shared.get_mode(get<kModeIndexField>(stat)));
    out.set_uid(shared.get_uid(get<kUidIndexField>(stat)));
    out.set_gid(shared.get_gid(get<kGidIndexField>(stat)));

    out.set_mtimespec(get<kModificationTimeSecondField>(stat),
                      get<kModificationTimeSubsecondField>(stat));

    if (keep_all_times_) {
      out.set_atimespec(get<kAccessTimeSecondField>(stat),
                        get<kAccessTimeSubsecondField>(stat));
      out.set_ctimespec(get<kStatusChangeTimeSecondField>(stat),
                        get<kStatusChangeTimeSubsecondField>(stat));
    } else {
      out.set_atimespec(0, 0);
      out.set_ctimespec(0, 0);
    }

    data.pack_inode_stat(entry_v2, out, timeres);
  }

  unique_inode_id get_unique_inode_id(shared_entry_data const& shared,
                                      uint64_t const index) const {
    auto const& stat = stat_common_.at(index);
    return unique_inode_id{shared.get_device(get<kDeviceIndexField>(stat)),
                           get<kInodeField>(stat)};
  }

  file_stat::nlink_type get_nlink(uint64_t const index) const {
    auto const& stat = stat_common_.at(index);
    return get<kNlinkMinusOneField>(stat) + 1;
  }

  void create_hardlink(file_id target, file_id source, progress& prog) {
    assert(this_type_ == entry_type::E_FILE);
    auto target_fdi = file_data_index_.at(target.index());
    auto const& source_fdi = file_data_index_.at(source.index());
    assert(!target_fdi.has_value());
    assert(source_fdi.has_value());
    auto const [total, allocated] = get_size_info(source.index());

    prog.hardlink_size += total;
    prog.allocated_hardlink_size += allocated;
    ++prog.hardlinks;

    auto const fdi = source_fdi.value();
    target_fdi = fdi;
    ++get<kHardlinkCountMinusOneField>(file_data_vec_.at(fdi));
  }

  void create_file_data(file_id id) {
    auto const index = file_data_vec_.size();
    file_data_vec_.push_back({std::nullopt, 0, std::nullopt});
    file_data_index_.at(id.index()) = index;
    file_invalid_vec_.emplace_back(false);
  }

  size_t get_file_data_index(std::uint64_t const index) const {
    assert(this_type_ == entry_type::E_FILE);
    auto fdi = file_data_index_.at(index);
    DWARFS_CHECK(fdi.has_value(), "file data unset");
    return *fdi;
  }

  std::span<std::byte>
  get_file_hash_buffer(file_id id, std::size_t buffer_size) {
    if (!file_hashes_.has_value()) {
      file_hashes_.emplace(buffer_size);
    } else if (file_hashes_->span_size() != buffer_size) {
      DWARFS_PANIC(fmt::format("hash buffer size mismatch: expected {}, got {}",
                               file_hashes_->span_size(), buffer_size));
    }

    auto& hashes = *file_hashes_;
    auto const index = hashes.size();
    set_file_hash_index(id, index);

    return hashes.emplace_back();
  }

  std::string_view get_file_hash(file_id id) const {
    if (file_hashes_.has_value()) {
      auto const& hashes = *file_hashes_;
      auto const index = get_file_hash_index(id);

      if (index.has_value()) {
        auto const span = hashes.at(*index);
        return {reinterpret_cast<char const*>(span.data()), span.size()};
      }
    }

    return {};
  }

  std::size_t hardlink_count(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return get<kHardlinkCountMinusOneField>(file_data_vec_.at(fdi)) + 1;
  }

  void set_file_invalid(file_id id) {
    auto const fdi = get_file_data_index(id.index());
    file_invalid_vec_.at(fdi).store(true);
  }

  bool is_file_invalid(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return file_invalid_vec_.at(fdi).load();
  }

  void set_entry_index(std::uint64_t const index, std::size_t const ix) {
    auto ei = final_entry_index_.at(index);
    DWARFS_CHECK(!ei.has_value(), "attempt to set entry index more than once");
    ei = ix;
  }

  std::optional<std::size_t> get_entry_index(std::uint64_t const index) const {
    return final_entry_index_.at(index);
  }

  void set_file_order_index(file_id id, std::size_t index) {
    assert(this_type_ == entry_type::E_FILE);
    file_order_index_.at(id.index()) = index;
  }

  std::size_t get_file_order_index(file_id id) const {
    assert(this_type_ == entry_type::E_FILE);
    return file_order_index_.at(id.index());
  }

  void set_file_hash_index(file_id id, std::size_t index) {
    auto const fdi = get_file_data_index(id.index());
    auto hash_index = get<kFileHashIndexField>(file_data_vec_.at(fdi));
    DWARFS_CHECK(!hash_index.has_value(),
                 "attempt to set file hash index more than once");
    hash_index = index;
  }

  std::optional<std::size_t> get_file_hash_index(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return get<kFileHashIndexField>(file_data_vec_.at(fdi));
  }

  file_size_t get_size(uint64_t const index) const {
    return entry_size_.at(index);
  }

  file_size_info get_size_info(uint64_t const index) const {
    auto const size = get_size(index);
    auto const alloc_size =
        container::get_optional(entry_allocated_size_, index).value_or(size);
    return {size, alloc_size};
  }

  void set_empty(uint64_t const index) {
    entry_size_.at(index) = 0;
    entry_allocated_size_.erase(index);
  }

  void set_inode_num(uint64_t const index, uint64_t ino) {
    if (this_type_ == entry_type::E_FILE) {
      auto const fdi = get_file_data_index(index);
      auto file_inode = get<kInodeNumberField>(file_data_vec_.at(fdi));
      DWARFS_CHECK(!file_inode.has_value(),
                   "attempt to set inode number more than once");
      file_inode = ino;
    } else {
      DWARFS_CHECK(!inode_num_.at(index).has_value(),
                   "attempt to set inode number more than once");
      inode_num_.at(index) = ino;
    }
  }

  std::optional<uint64_t> get_inode_num(uint64_t const index) const {
    if (this_type_ == entry_type::E_FILE) {
      auto const fdi = get_file_data_index(index);
      return get<kInodeNumberField>(file_data_vec_.at(fdi));
    }
    return inode_num_.at(index);
  }

  void set_inode_id(file_id fid, inode_id iid) {
    assert(this_type_ == entry_type::E_FILE);
    auto inode = file_inode_id_.at(fid.index());
    DWARFS_CHECK(!inode.load().valid(), "inode already set for file");
    inode = iid;
  }

  inode_id get_inode_id(file_id fid) const {
    assert(this_type_ == entry_type::E_FILE);
    return file_inode_id_.at(fid.index());
  }

  void set_link_target_index(link_id lid, size_t index) {
    assert(this_type_ == entry_type::E_LINK);
    auto link_target = link_target_index_.at(lid.index());
    DWARFS_CHECK(!link_target.has_value(),
                 "attempt to set link target index more than once");
    link_target = index;
  }

  std::size_t get_link_target_index(link_id lid) const {
    assert(this_type_ == entry_type::E_LINK);
    auto const link_target = link_target_index_.at(lid.index());
    DWARFS_CHECK(link_target.has_value(), "link target index not set");
    return *link_target;
  }

  file_stat::dev_type get_represented_device(device_id id) const {
    return represented_device_.at(id.index());
  }

 private:
  entry_type this_type_;

  bool const keep_all_times_{false};
  bool const keep_subsecond_{false};

  // index into `shared_entry_data::path_components_`
  segtor<size_t> path_name_index_;

  // parent directory id (invalid for root)
  segtor<dir_id> parent_dir_id_;

  // final index of this entry assigned during packing
  segtor<std::optional<size_t>> final_entry_index_;

  // inode number for non-file entries, assigned after scanning is complete
  segtor<std::optional<std::uint64_t>> inode_num_;

  // file `stat()` data common to all entry types
  segtor<stat_common_tuple> stat_common_;

  // size for files and symlinks
  segtor<file_stat::off_type> entry_size_;

  // allocated size for files, only stored if different from `entry_size_`
  phmap::flat_hash_map<uint64_t, file_stat::off_type> entry_allocated_size_;

  // file-specific
  segtor<size_t> file_order_index_;
  segtor<inode_id> file_inode_id_;
  segtor<std::optional<size_t>> file_data_index_; // index into `file_data_vec_`
  std::optional<dwarfs::container::pinned_byte_span_store<512>> file_hashes_;

  static constexpr std::size_t kFileHashIndexField{0};
  static constexpr std::size_t kHardlinkCountMinusOneField{1};
  static constexpr std::size_t kInodeNumberField{2};
  using file_data_tuple =
      std::tuple<std::optional<std::uint64_t>, std::uint64_t,
                 std::optional<std::uint64_t>>;
  segtor<file_data_tuple> file_data_vec_;
  cao_vector<std::atomic<bool>> file_invalid_vec_;

  // link-specific
  segtor<std::optional<size_t>> link_target_index_;

  // device-specific:
  segtor<file_stat::dev_type> represented_device_;
};

class packed_inode_data {
 public:
  enum class fragment_kind : std::uint64_t {
    data = 0,
    hole = 1,
  };

  static constexpr std::size_t kFragmentCategoryField = 0;
  static constexpr std::size_t kFragmentSubcategoryField = 1;
  static constexpr std::size_t kFragmentKindField = 2;
  static constexpr std::size_t kFragmentSizeField = 3;

  using inode_fragment_tuple =
      std::tuple<std::uint64_t, std::optional<std::uint64_t>, fragment_kind,
                 std::uint64_t>;

  static constexpr std::size_t kFragmentInfoOffsetField = 0;
  static constexpr std::size_t kFragmentInfoCountField = 1;

  using inode_fragment_info_tuple = std::tuple<std::uint64_t, std::uint64_t>;

  static constexpr std::size_t kSimilarityInfoOffsetField = 0;
  static constexpr std::size_t kSimilarityInfoCountField = 1;

  using inode_similarity_info_tuple = std::tuple<std::uint64_t, std::uint64_t>;

  enum class similarity_hash_type : std::uint64_t {
    similarity = 0,
    nilsimsa = 1,
  };

  static constexpr std::size_t kSimilarityHashCategoryField = 0;
  static constexpr std::size_t kSimilarityHashSubcategoryField = 1;
  static constexpr std::size_t kSimilarityHashTypeField = 2;
  static constexpr std::size_t kSimilarityHashIndexField = 3;

  using inode_similarity_hash_tuple =
      std::tuple<std::uint64_t, std::optional<std::uint64_t>,
                 similarity_hash_type, std::uint64_t>;

  inode_id create() {
    auto const id = inode_num_.size();
    files_for_inode_.emplace_back();
    inode_num_.push_back(std::nullopt);
    fragment_info_.push_back({0, 0});
    similarity_info_.push_back({0, 0});
    return inode_id{id};
  }

  std::size_t size() const { return inode_num_.size(); }

  file_id_vector const& get_files(inode_id id) const {
    return files_for_inode_.at(id.index());
  }

  void set_files(inode_id id, file_id_vector files) {
    auto& vec = files_for_inode_.at(id.index());
    DWARFS_CHECK(vec.empty(), "files already set for inode");
    vec = std::move(files);
  }

  void set_scan_error(inode_id id, file_id fid, std::exception_ptr ep) {
    inode_scan_errors_.emplace(id.index(), std::make_pair(fid, std::move(ep)));
  }

  std::optional<inode_scan_error> get_scan_error(inode_id id) const {
    return container::get_optional(inode_scan_errors_, id.index());
  }

  void set_inode_num(inode_id id, std::uint64_t num) {
    auto ino_num = inode_num_.at(id.index());
    DWARFS_CHECK(!ino_num.has_value(),
                 "attempt to set inode number multiple times");
    ino_num = num;
  }

  std::optional<std::uint64_t> get_inode_num(inode_id id) const {
    return inode_num_.at(id.index());
  }

  void set_fragments(inode_id id, inode_fragments const& fragments) {
    auto info = fragment_info_.at(id.index());
    get<kFragmentInfoOffsetField>(info) = fragment_data_.size();
    get<kFragmentInfoCountField>(info) = fragments.size();
    for (auto const& frag : fragments) {
      fragment_chunks_.emplace_back();

      auto const cat = frag.category();

      inode_fragment_tuple data{};

      get<kFragmentCategoryField>(data) = cat.value();

      if (cat.has_subcategory()) {
        get<kFragmentSubcategoryField>(data) = cat.subcategory();
      }

      get<kFragmentKindField>(data) =
          frag.is_hole() ? fragment_kind::hole : fragment_kind::data;
      get<kFragmentSizeField>(data) = frag.size();

      fragment_data_.push_back(data);
    }
  }

  void fragment_add_data_chunk(inode_id id, std::size_t fragment_index,
                               size_t block, size_t offset, size_t size) {
    auto& chunks = get_fragment_chunks(id, fragment_index);

    if (!chunks.empty()) {
      auto last = chunks.back();

      if (get<kChunkKindField>(last) == chunk_kind::data &&
          std::cmp_equal(get<kChunkBlockField>(last).load(), block) &&
          std::cmp_equal(get<kChunkOffsetField>(last) +
                             get<kChunkSizeField>(last),
                         offset)) [[unlikely]] {
        // merge chunks
        get<kChunkSizeField>(last) += size;
        return;
      }
    }

    chunks.push_back(packed_chunk_tuple{
        block,
        offset,
        size,
        chunk_kind::data,
    });
  }

  void fragment_add_hole_chunk(inode_id id, std::size_t fragment_index,
                               file_size_t size) {
    auto& chunks = get_fragment_chunks(id, fragment_index);

    chunks.push_back(packed_chunk_tuple{
        0,
        0,
        size,
        chunk_kind::hole,
    });
  }

  std::size_t get_fragment_count(inode_id id) const {
    auto const info = fragment_info_.at(id.index());
    return get<kFragmentInfoCountField>(info);
  }

  fragment_category
  get_fragment_category(inode_id id, std::size_t fragment_index) const {
    auto const& data = get_fragment_data(id, fragment_index);
    auto cat = fragment_category(get<kFragmentCategoryField>(data));
    if (auto subcat = get<kFragmentSubcategoryField>(data);
        subcat.has_value()) {
      cat.set_subcategory(*subcat);
    }
    return cat;
  }

  file_size_t get_fragment_size(inode_id id, std::size_t fragment_index) const {
    auto const& data = get_fragment_data(id, fragment_index);
    return get<kFragmentSizeField>(data);
  }

  packed_chunk_vector const&
  get_fragment_packed_chunks(inode_id id, std::size_t index) const {
    auto const info = fragment_info_.at(id.index());
    auto const offset = get<kFragmentInfoOffsetField>(info);
    return fragment_chunks_.at(offset + index);
  }

  void set_similarity(inode_id id,
                      std::span<inode_similarity_hash_data const> data) {
    auto info = similarity_info_.at(id.index());
    auto offset = similarity_hash_.size();

    get<kSimilarityInfoOffsetField>(info) = offset;
    get<kSimilarityInfoCountField>(info) = data.size();

    for (auto const& row : data) {
      inode_similarity_hash_tuple hash_data{};

      get<kSimilarityHashCategoryField>(hash_data) = row.category.value();

      if (row.category.has_subcategory()) {
        get<kSimilarityHashSubcategoryField>(hash_data) =
            row.category.subcategory();
      }

      row.hash | match{
                     [&](std::uint32_t h) {
                       get<kSimilarityHashTypeField>(hash_data) =
                           similarity_hash_type::similarity;
                       get<kSimilarityHashIndexField>(hash_data) =
                           similarity_hash_data_.size();
                       similarity_hash_data_.emplace_back(h);
                     },
                     [&](nilsimsa::hash_type const& h) {
                       get<kSimilarityHashTypeField>(hash_data) =
                           similarity_hash_type::nilsimsa;
                       get<kSimilarityHashIndexField>(hash_data) =
                           nilsimsa_hash_data_.size();
                       nilsimsa_hash_data_.emplace_back(h);
                     },
                 };

      similarity_hash_.push_back(hash_data);
    }
  }

  std::optional<std::uint32_t>
  get_similarity_hash(inode_id id, fragment_category cat) const {
    auto const index =
        find_similarity_hash_index(id, cat, similarity_hash_type::similarity);
    if (index.has_value()) {
      return similarity_hash_data_.at(*index);
    }
    return std::nullopt;
  }

  nilsimsa::hash_type const*
  get_nilsimsa_hash(inode_id id, fragment_category cat) const {
    auto const index =
        find_similarity_hash_index(id, cat, similarity_hash_type::nilsimsa);
    if (index.has_value()) {
      return &nilsimsa_hash_data_.at(*index);
    }
    return nullptr;
  }

  void dump_similarity(
      inode_id id, std::ostream& os,
      std::function<std::string(fragment_category)> const& catlabel) const {
    auto const info = similarity_info_.at(id.index());
    std::size_t const count = get<kSimilarityInfoCountField>(info);

    if (count == 0) {
      os << "  no similarity hashes\n";
      return;
    }

    std::size_t const offset = get<kSimilarityInfoOffsetField>(info);

    os << "  similarity hashes:\n";

    for (std::size_t i = 0; i < count; ++i) {
      auto const hash_data = similarity_hash_.at(offset + i);
      auto cat =
          fragment_category(get<kSimilarityHashCategoryField>(hash_data));

      if (auto subcat = get<kSimilarityHashSubcategoryField>(hash_data);
          subcat.has_value()) {
        cat.set_subcategory(*subcat);
      }

      auto type = get<kSimilarityHashTypeField>(hash_data);
      auto index = get<kSimilarityHashIndexField>(hash_data);

      os << "    " << catlabel(cat);

      switch (type) {
      case similarity_hash_type::similarity:
        os << fmt::format("basic ({0:08x})\n", similarity_hash_data_.at(index));
        break;
      case similarity_hash_type::nilsimsa: {
        auto const& nh = nilsimsa_hash_data_.at(index);
        os << fmt::format("nilsimsa ({0:016x}{1:016x}{2:016x}{3:016x})\n",
                          nh[0], nh[1], nh[2], nh[3]);
        break;
      }
      }
    }
  }

  void dump(std::ostream& os) const;

 private:
  [[nodiscard]] std::optional<std::size_t>
  find_similarity_hash_index(inode_id id, fragment_category cat,
                             similarity_hash_type type) const {
    auto const info = similarity_info_.at(id.index());
    std::size_t const offset = get<kSimilarityInfoOffsetField>(info);
    std::size_t const count = get<kSimilarityInfoCountField>(info);

    for (std::size_t i = 0; i < count; ++i) {
      auto const hash_data = similarity_hash_.at(offset + i);

      if (get<kSimilarityHashTypeField>(hash_data) == type) {
        fragment_category hash_cat(
            get<kSimilarityHashCategoryField>(hash_data));

        if (auto const subcat =
                get<kSimilarityHashSubcategoryField>(hash_data)) {
          hash_cat.set_subcategory(*subcat);
        }

        if (hash_cat == cat) {
          return get<kSimilarityHashIndexField>(hash_data);
        }
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] auto
  get_fragment_data(inode_id id, std::size_t fragment_index) const
      -> segtor<inode_fragment_tuple>::const_reference {
    auto const info = fragment_info_.at(id.index());
    auto const offset = get<kFragmentInfoOffsetField>(info);
    return fragment_data_.at(offset + fragment_index);
  }

  [[nodiscard]] auto
  get_fragment_chunks(inode_id id, std::size_t fragment_index)
      -> packed_chunk_vector& {
    auto const info = fragment_info_.at(id.index());
    auto const offset = get<kFragmentInfoOffsetField>(info);
    assert(fragment_index < get<kFragmentInfoCountField>(info));
    return fragment_chunks_.at(offset + fragment_index);
  }

  cao_vector<file_id_vector> files_for_inode_;
  phmap::flat_hash_map<std::uint64_t, inode_scan_error> inode_scan_errors_;
  segtor<std::optional<std::uint64_t>> inode_num_;

  segtor<inode_fragment_info_tuple> fragment_info_;
  segtor<inode_fragment_tuple> fragment_data_;
  cao_vector<packed_chunk_vector> fragment_chunks_;

  segtor<inode_similarity_info_tuple> similarity_info_;
  segtor<inode_similarity_hash_tuple> similarity_hash_;
  cao_vector<std::uint32_t> similarity_hash_data_;
  cao_vector<nilsimsa::hash_type> nilsimsa_hash_data_;
};

void shared_entry_data::add_dir_entry(dir_id parent, entry_type type,
                                      std::uint64_t entry_ix) {
  assert(parent.valid());
  assert(parent.index() < dir_entries_.size());
  dir_entries_.at(parent.index()).push_back({type, entry_ix});
}

void shared_entry_data::dump(std::ostream& os) const {
  auto const total_path_bytes =
      std::accumulate(path_components_.begin(), path_components_.end(), 0ULL,
                      [](std::size_t acc, path_component const& pc) {
                        return acc + pc.size_in_bytes();
                      });
  auto const total_link_bytes =
      std::accumulate(link_targets_.begin(), link_targets_.end(), 0ULL,
                      [](std::size_t acc, std::string const& link) {
                        return acc + sizeof(std::string) + link.size();
                      });

  os << "shared entry data:\n";
  os << "  path components: " << path_components_.size() << " ("
     << size_with_unit(total_path_bytes) << ")\n";
  os << "  devices: " << devices_.size() << " ("
     << size_with_unit(devices_.size() * sizeof(devices_[0])) << ")\n";
  os << "  modes: " << modes_.size() << " ("
     << size_with_unit(modes_.size() * sizeof(modes_[0])) << ")\n";
  os << "  uids: " << uids_.size() << " ("
     << size_with_unit(uids_.size() * sizeof(uids_[0])) << ")\n";
  os << "  gids: " << gids_.size() << " ("
     << size_with_unit(gids_.size() * sizeof(gids_[0])) << ")\n";
  os << "  link targets: " << link_targets_.size() << " ("
     << size_with_unit(total_link_bytes) << ")\n";
  os << "  dir entries: " << dir_entries_.size() << " ("
     << size_with_unit(total_cao_id_vec_bytes(dir_entries_)) << ")\n";
}

void packed_entry_data::dump(std::ostream& os, std::string_view name) const {
  if (path_name_index_.empty()) {
    os << "no " << name << " entries\n";
    return;
  }

  std::vector<std::pair<std::string_view, std::size_t>> sizes;

  sizes.emplace_back("path name index", path_name_index_.size_in_bytes());
  sizes.emplace_back("stat common", stat_common_.size_in_bytes());
  sizes.emplace_back("size", entry_size_.size_in_bytes());
  sizes.emplace_back("allocated size",
                     entry_allocated_size_.capacity() *
                         sizeof(decltype(entry_allocated_size_)::value_type));
  sizes.emplace_back("parent dir index", parent_dir_id_.size_in_bytes());
  sizes.emplace_back("link target index", link_target_index_.size_in_bytes());
  sizes.emplace_back("represented device", represented_device_.size_in_bytes());
  sizes.emplace_back("inode number", inode_num_.size_in_bytes());
  sizes.emplace_back("final entry index", final_entry_index_.size_in_bytes());
  sizes.emplace_back("file data vec", file_data_vec_.size_in_bytes());
  sizes.emplace_back("file invalid vec",
                     file_invalid_vec_.size() * sizeof(file_invalid_vec_[0]));
  sizes.emplace_back("file hashes",
                     file_hashes_ ? file_hashes_->size_in_bytes() : 0);
  sizes.emplace_back("file inode id", file_inode_id_.size_in_bytes());
  sizes.emplace_back("file data index", file_data_index_.size_in_bytes());
  sizes.emplace_back("file order index", file_order_index_.size_in_bytes());

  auto const total_bytes = std::accumulate(
      sizes.begin(), sizes.end(), 0ULL,
      [](std::size_t acc, auto const& pair) { return acc + pair.second; });

  os << path_name_index_.size() << " " << name << " entries ("
     << size_with_unit(total_bytes) << "):\n";

  for (auto const& [label, bytes] : sizes) {
    if (bytes > 0) {
      os << "  " << label << ": " << size_with_unit(bytes) << "\n";
    }
  }
}

void packed_inode_data::dump(std::ostream& os) const {
  std::vector<std::pair<std::string_view, std::size_t>> sizes;

  sizes.emplace_back("hardlinks", total_cao_id_vec_bytes(files_for_inode_));
  sizes.emplace_back("inode numbers", inode_num_.size_in_bytes());
  sizes.emplace_back("scan errors",
                     inode_scan_errors_.capacity() * sizeof(inode_scan_error));
  sizes.emplace_back("fragment info", fragment_info_.size_in_bytes());
  sizes.emplace_back("fragment data", fragment_data_.size_in_bytes());
  sizes.emplace_back("fragment chunks",
                     total_cao_id_vec_bytes(fragment_chunks_));
  sizes.emplace_back("similarity info", similarity_info_.size_in_bytes());
  sizes.emplace_back("similarity hashes", similarity_hash_.size_in_bytes());
  sizes.emplace_back("similarity hash data",
                     similarity_hash_data_.size() *
                         sizeof(similarity_hash_data_[0]));
  sizes.emplace_back("nilsimsa hash data", nilsimsa_hash_data_.size() *
                                               sizeof(nilsimsa_hash_data_[0]));

  auto const total_bytes = std::accumulate(
      sizes.begin(), sizes.end(), 0ULL,
      [](std::size_t acc, auto const& pair) { return acc + pair.second; });

  os << inode_num_.size() << " inodes (" << size_with_unit(total_bytes)
     << "):\n";

  for (auto const& [label, bytes] : sizes) {
    if (bytes > 0) {
      os << "  " << label << ": " << size_with_unit(bytes) << "\n";
    }
  }
}

auto packed_entry_data::add_entry_common(shared_entry_data& shared,
                                         entry_type type, fs::path const& path,
                                         file_stat const& st,
                                         dir_id const parent)
    -> add_entry_result {
  st.ensure_valid(
      file_stat::nlink_valid | file_stat::mode_valid | file_stat::uid_valid |
      file_stat::gid_valid | file_stat::atime_valid | file_stat::mtime_valid |
      file_stat::ctime_valid | file_stat::dev_valid | file_stat::ino_valid |
      file_stat::size_valid | file_stat::allocated_size_valid);

  bool const is_root = !parent.valid();
  auto const path_ix = shared.add_path_component(path, is_root);
  auto const entry_ix = path_name_index_.size();
  path_name_index_.push_back(path_ix);
  parent_dir_id_.push_back(parent);
  final_entry_index_.push_back(std::nullopt);

  auto const nlink = st.nlink_unchecked();
  assert(nlink > 0);

  stat_common_tuple tmp{};
  std::get<kNlinkMinusOneField>(tmp) = nlink - 1;
  std::get<kModeIndexField>(tmp) = shared.add_mode(st.mode_unchecked());
  std::get<kUidIndexField>(tmp) = shared.add_uid(st.uid_unchecked());
  std::get<kGidIndexField>(tmp) = shared.add_gid(st.gid_unchecked());

  std::get<kModificationTimeSecondField>(tmp) = st.mtime_unchecked();

  if (keep_subsecond_) {
    std::get<kModificationTimeSubsecondField>(tmp) = st.mtime_nsec_unchecked();
  }

  if (keep_all_times_) {
    std::get<kAccessTimeSecondField>(tmp) = st.atime_unchecked();
    std::get<kStatusChangeTimeSecondField>(tmp) = st.ctime_unchecked();

    if (keep_subsecond_) {
      std::get<kAccessTimeSubsecondField>(tmp) = st.atime_nsec_unchecked();
      std::get<kStatusChangeTimeSubsecondField>(tmp) =
          st.ctime_nsec_unchecked();
    }
  }

  std::get<kInodeField>(tmp) = st.ino_unchecked();
  std::get<kDeviceIndexField>(tmp) = shared.add_device(st.dev_unchecked());

  stat_common_.push_back(tmp);

  if (type == entry_type::E_FILE || type == entry_type::E_LINK) {
    auto const size = st.size_unchecked();
    auto const allocated_size = st.allocated_size_unchecked();

    auto const index = entry_size_.size();
    entry_size_.push_back(size);

    if (size != allocated_size) {
      entry_allocated_size_.emplace(index, st.allocated_size_unchecked());
    }
  }

  if (type != entry_type::E_FILE) {
    inode_num_.push_back(std::nullopt);
  }

  return {entry_ix, path_ix};
}

[[noreturn]] void frozen_panic() { DWARFS_PANIC("entry_storage is frozen"); }

} // namespace

template <bool Frozen>
class entry_storage_ final : public entry_storage::entry_impl {
 public:
  static constexpr bool is_mutable = !Frozen;

  friend class entry_storage_<true>;

  entry_storage_()
    requires is_mutable
  = default;

  explicit entry_storage_(metadata_options const& options)
    requires is_mutable
      : files_{entry_type::E_FILE, options}
      , dirs_{entry_type::E_DIR, options}
      , links_{entry_type::E_LINK, options}
      , devices_{entry_type::E_DEVICE, options}
      , others_{entry_type::E_OTHER, options} {}

  entry_storage_(entry_storage_<false>& other) noexcept
    requires Frozen
      : shared_{std::move(other.shared_)}
      , files_{std::move(other.files_)}
      , dirs_{std::move(other.dirs_)}
      , links_{std::move(other.links_)}
      , devices_{std::move(other.devices_)}
      , others_{std::move(other.others_)} {}

  std::unique_ptr<entry_impl> freeze() override {
    if constexpr (is_mutable) {
      sort_all_directory_entries();
      shared_.drop_indices();
      return std::make_unique<entry_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  entry_id
  make_obj_(entry_type const type, packed_entry_data& data,
            fs::path const& path, file_stat const& st, dir_id const parent) {
    if constexpr (is_mutable) {
      auto const [entry_ix, path_ix] =
          data.add_entry_common(shared_, type, path, st, parent);

      if (parent) {
        shared_.add_dir_entry(parent, type, entry_ix);

        if (auto const it = dir_entry_lookup_.find(parent.index());
            it != dir_entry_lookup_.end()) {
          auto& lookup = it->second;
          auto const inserted [[maybe_unused]] =
              lookup
                  .emplace(shared_.get_path_component(path_ix).name(),
                           entry_id{type, entry_ix})
                  .second;
          assert(inserted);
        }
      }

      switch (type) {
      case entry_type::E_FILE:
        data.add_file_specific();
        break;
      case entry_type::E_DIR:
        shared_.add_dir_entry_vec();
        break;
      case entry_type::E_LINK:
        data.add_link_specific();
        break;
      case entry_type::E_DEVICE:
        data.add_device_specific(st);
        break;
      case entry_type::E_OTHER:
        break;
      }

      return {type, entry_ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_file(fs::path const& path, file_stat const& st,
                     dir_id const parent) override {
    return make_obj_(entry_type::E_FILE, files_, path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    dir_id const parent) override {
    return make_obj_(entry_type::E_DIR, dirs_, path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     dir_id const parent) override {
    return make_obj_(entry_type::E_LINK, links_, path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       dir_id const parent) override {
    return make_obj_(entry_type::E_DEVICE, devices_, path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      dir_id const parent) override {
    return make_obj_(entry_type::E_OTHER, others_, path, st, parent);
  }

  bool empty() const noexcept override {
    TRACE_CALL;
    return dirs_.empty();
  }

  void create_packed_file_data(file_id id) override {
    if constexpr (is_mutable) {
      files_.create_file_data(id);
    } else {
      frozen_panic();
    }
  }

  void set_file_inode(file_id id, inode_id ino) override {
    TRACE_CALL;
    // this is safe even on frozen storage if it's single-threaded
    files_.set_inode_id(id, ino);
  }

  inode_id get_file_inode(file_id id) const override {
    TRACE_CALL;
    return files_.get_inode_id(id);
  }

  void set_entry_index(entry_id id, std::size_t index) override {
    TRACE_CALL;
    // this is safe even on frozen storage if it's single-threaded
    dispatch_(&packed_entry_data::set_entry_index, id, index);
  }

  std::optional<std::size_t> get_entry_index(entry_id id) const override {
    TRACE_CALL;
    return dispatch_(&packed_entry_data::get_entry_index, id);
  }

  void set_file_order_index(file_id id, std::size_t index) override {
    TRACE_CALL;
    // this is safe even on frozen storage if it's single-threaded
    files_.set_file_order_index(id, index);
  }

  std::size_t get_file_order_index(file_id id) const override {
    TRACE_CALL;
    return files_.get_file_order_index(id);
  }

  void set_link_target(link_id id, std::string link_target,
                       progress& prog) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      auto const index = shared_.add_link_target(std::move(link_target));
      links_.set_link_target_index(id, index);
      auto const [total, allocated] = links_.get_size_info(id.index());
      prog.original_size += total;
      prog.allocated_original_size += allocated;
      prog.symlink_size += total;
    } else {
      frozen_panic();
    }
  }

  std::string_view get_link_target(link_id id) const override {
    TRACE_CALL;
    return shared_.get_link_target(links_.get_link_target_index(id));
  }

  dir_id get_parent(entry_id const id) const override {
    TRACE_CALL;
    return get_parent_impl(id);
  }

  fs::path get_path(entry_id id) const override {
    TRACE_CALL;

    fs::path p = get_path_impl(id);

    while ((id = get_parent_impl(id))) {
      p = get_path_impl(id) / p;
    }

    return p;
  }

  std::string get_unix_dpath(entry_id id) const override {
    TRACE_CALL;

    std::string p;

    for (;;) {
      auto const name = get_path_string_impl(id);
      bool const is_root = is_root_path(name);

      if (is_root || id.is_dir()) {
        p.insert(0, std::string_view{"/"});
      }

      if (!is_root) {
        p.insert(0, name);
      }

      id = get_parent_impl(id);

      if (!id.valid()) {
        if constexpr (kLocalPathSeparator != '/') {
          std::replace(p.begin(), p.begin() + name.size(), kLocalPathSeparator,
                       '/');
        }

        break;
      }
    }

    return p;
  }

  std::string_view get_name(entry_id const id) const override {
    TRACE_CALL;
    return get_path_string_impl(id);
  }

  void remove_empty_dirs(progress& prog) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      remove_empty_dirs_impl(prog, dir_id{{entry_id{entry_type::E_DIR, 0}}});
    } else {
      frozen_panic();
    }
  }

  void
  for_each_entry_in_dir(dir_id id,
                        std::function<void(entry_id)> const& f) const override {
    TRACE_CALL;
    for (auto const eid : shared_.get_dir_entries(id)) {
      f(eid);
    }
  }

  static constexpr std::size_t kMinDirEntriesForLookupTable = 16;

  entry_id find_in_dir(dir_id id, std::string_view name) const override {
    TRACE_CALL;

    if constexpr (is_mutable) {
      auto const& de = shared_.get_dir_entries(id);

      if (de.size() < kMinDirEntriesForLookupTable) {
        auto const it = std::ranges::find_if(de, [&](entry_id const id) {
          return get_path_string_impl(id) == name;
        });

        if (it != de.end()) {
          return *it;
        }
      } else {
        auto [lit, created] = dir_entry_lookup_.try_emplace(id.index());

        if (created) {
          for (auto const eid : de) {
            auto const inserted [[maybe_unused]] =
                lit->second.emplace(get_path_string_impl(eid), eid).second;
            assert(inserted);
          }
        }

        auto const& lookup = lit->second;
        assert(lookup.size() == de.size());

        auto const it = lookup.find(name);

        if (it != lookup.end()) {
          return it->second;
        }
      }
    } else {
      // If we ever need this, we can do a binary search here since frozen
      // entries are sorted by name.
      DWARFS_PANIC("find_in_dir not (yet) supported for frozen entry_storage");
    }

    return {};
  }

  bool entry_less_revpath(entry_id lhs, entry_id rhs) const override {
    TRACE_CALL;

    while (lhs.valid() && rhs.valid()) {
      auto const lname = get_path_string_impl(lhs);
      auto const rname = get_path_string_impl(rhs);

      if (lname != rname) {
        return lname < rname;
      }

      lhs = get_parent_impl(lhs);
      rhs = get_parent_impl(rhs);
    }

    return rhs.valid();
  }

  void update_global_entry_data(entry_id id,
                                global_entry_data& data) const override {
    TRACE_CALL;
    update_global_entry_data_impl(id, data);
  }

  void
  pack_entry(entry_id id,
             thrift::metadata::metadata::inodes_member_type::reference entry_v2,
             global_entry_data const& data,
             time_resolution_converter const& timeres) const override {
    TRACE_CALL;
    pack_entry_impl(id, entry_v2, data, timeres);
  }

  unique_inode_id get_unique_inode_id(entry_id id) const override {
    TRACE_CALL;
    return get_unique_inode_id_impl(id);
  }

  file_stat::nlink_type get_nlink(entry_id id) const override {
    TRACE_CALL;
    return get_nlink_impl(id);
  }

  void
  create_hardlink(file_id target, file_id source, progress& prog) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      files_.create_hardlink(target, source, prog);
    } else {
      frozen_panic();
    }
  }

  std::size_t hardlink_count(file_id id) const override {
    TRACE_CALL;
    return files_.hardlink_count(id);
  }

  void set_file_invalid(file_id id) override {
    TRACE_CALL;
    files_.set_file_invalid(id);
  }

  bool is_file_invalid(file_id id) const override {
    TRACE_CALL;
    return files_.is_file_invalid(id);
  }

  std::span<std::byte>
  get_file_hash_buffer(file_id id, std::size_t buffer_size) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      return files_.get_file_hash_buffer(id, buffer_size);
    } else {
      frozen_panic();
    }
  }

  std::string_view get_file_hash(file_id id) const override {
    TRACE_CALL;
    return files_.get_file_hash(id);
  }

  file_size_t get_entry_size(entry_id id) const override {
    TRACE_CALL;
    return dispatch_(&packed_entry_data::get_size, id);
  }

  file_size_info get_entry_size_info(entry_id id) const override {
    TRACE_CALL;
    return dispatch_(&packed_entry_data::get_size_info, id);
  }

  void set_entry_empty(entry_id id) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      dispatch_(&packed_entry_data::set_empty, id);
    } else {
      frozen_panic();
    }
  }

  void set_inode_num_for_entry(entry_id id, std::uint64_t ino) override {
    TRACE_CALL;
    dispatch_(&packed_entry_data::set_inode_num, id, ino);
  }

  std::optional<std::uint64_t>
  get_inode_num_for_entry(entry_id id) const override {
    TRACE_CALL;
    return dispatch_(&packed_entry_data::get_inode_num, id);
  }

  file_stat::dev_type get_represented_device(device_id id) const override {
    TRACE_CALL;
    return devices_.get_represented_device(id);
  }

  void dump(std::ostream& os) const override;
  void dump_events(std::ostream& os) const override;

 private:
  void sort_all_directory_entries()
    requires is_mutable
  {
    shared_.sort_all_dir_entries(
        [this](entry_id const aid, entry_id const bid) {
          return get_path_string_impl(aid) < get_path_string_impl(bid);
        });
  }

  void remove_empty_dirs_impl(progress& prog, dir_id dir)
    requires is_mutable
  {
    auto& de = shared_.get_dir_entries(dir);

    auto last = std::remove_if(de.begin(), de.end(), [&](entry_id const id) {
      if (auto const did = dir_id{id}) {
        remove_empty_dirs_impl(prog, did);
        return shared_.get_dir_entries(did).empty();
      }
      return false;
    });

    if (last != de.end()) {
      auto num = std::distance(last, de.end());
      prog.dirs_scanned -= num;
      prog.dirs_found -= num;
      de.erase(last, de.end());
    }
  }

  template <typename Self, typename Method, typename... Args>
  static decltype(auto) dispatch_impl_(Self&& self, Method method,
                                       entry_id const id, Args&&... args) {
    auto&& me = std::forward<Self>(self);
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return (me.files_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (me.dirs_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (me.links_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (me.devices_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (me.others_.*method)(id.index(), std::forward<Args>(args)...);
    default:
      DWARFS_PANIC("invalid entry type");
    }
  }

  // TODO: workaround while too many compilers don't support deducing this
  template <typename Method, typename... Args>
  decltype(auto) dispatch_(Method method, entry_id const id, Args&&... args) {
    return dispatch_impl_(*this, method, id, std::forward<Args>(args)...);
  }

  // TODO: workaround while too many compilers don't support deducing this
  template <typename Method, typename... Args>
  decltype(auto)
  dispatch_(Method method, entry_id const id, Args&&... args) const {
    return dispatch_impl_(*this, method, id, std::forward<Args>(args)...);
  }

  template <typename Self, typename Method, typename... Args>
  static decltype(auto)
  dispatch_shared_impl_(Self&& self, Method method, entry_id const id,
                        Args&&... args) {
    auto&& me = std::forward<Self>(self);
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return (me.files_.*method)(me.shared_, id.index(),
                                 std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (me.dirs_.*method)(me.shared_, id.index(),
                                std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (me.links_.*method)(me.shared_, id.index(),
                                 std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (me.devices_.*method)(me.shared_, id.index(),
                                   std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (me.others_.*method)(me.shared_, id.index(),
                                  std::forward<Args>(args)...);
    default:
      DWARFS_PANIC("invalid entry type");
    }
  }

  // TODO: workaround while too many compilers don't support deducing this
  template <typename Method, typename... Args>
  decltype(auto)
  dispatch_shared_(Method method, entry_id const id, Args&&... args) {
    return dispatch_shared_impl_(*this, method, id,
                                 std::forward<Args>(args)...);
  }

  // TODO: workaround while too many compilers don't support deducing this
  template <typename Method, typename... Args>
  decltype(auto)
  dispatch_shared_(Method method, entry_id const id, Args&&... args) const {
    return dispatch_shared_impl_(*this, method, id,
                                 std::forward<Args>(args)...);
  }

  dir_id get_parent_impl(entry_id const id) const {
    return dispatch_(&packed_entry_data::get_parent, id);
  }

  fs::path get_path_impl(entry_id const id) const {
    return dispatch_shared_(&packed_entry_data::get_path, id);
  }

  std::string_view get_path_string_impl(entry_id const id) const {
    return dispatch_shared_(&packed_entry_data::get_path_string, id);
  }

  void update_global_entry_data_impl(entry_id const id,
                                     global_entry_data& data) const {
    dispatch_shared_(&packed_entry_data::update_global_entry_data, id, data);
  }

  void pack_entry_impl(
      entry_id const id,
      thrift::metadata::metadata::inodes_member_type::reference entry_v2,
      global_entry_data const& data,
      time_resolution_converter const& timeres) const {
    dispatch_shared_(&packed_entry_data::pack_entry, id, entry_v2, data,
                     timeres);
  }

  unique_inode_id get_unique_inode_id_impl(entry_id id) const {
    return dispatch_shared_(&packed_entry_data::get_unique_inode_id, id);
  }

  file_stat::nlink_type get_nlink_impl(entry_id id) const {
    return dispatch_(&packed_entry_data::get_nlink, id);
  }

  shared_entry_data shared_;

  packed_entry_data files_{entry_type::E_FILE};
  packed_entry_data dirs_{entry_type::E_DIR};
  packed_entry_data links_{entry_type::E_LINK};
  packed_entry_data devices_{entry_type::E_DEVICE};
  packed_entry_data others_{entry_type::E_OTHER};

  using dir_entry_lookup_table =
      phmap::flat_hash_map<std::string_view, entry_id>;
  phmap::flat_hash_map<uint64_t,
                       dir_entry_lookup_table> mutable dir_entry_lookup_;

#ifdef DWARFS_TRACE_ENTRY_STORAGE_CALLS
  dwarfs::internal::event_tracer mutable ev_;
#endif
};

template <bool Frozen>
class inode_storage_ final : public entry_storage::inode_impl {
 public:
  static constexpr bool is_mutable = !Frozen;

  friend class inode_storage_<true>;

  inode_storage_()
    requires is_mutable
  = default;

  inode_storage_(inode_storage_<false>& other) noexcept
    requires Frozen
      : inodes_{std::move(other.inodes_)} {}

  std::unique_ptr<inode_impl> freeze() override {
    if constexpr (is_mutable) {
      return std::make_unique<inode_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  inode_id make_inode() override {
    if constexpr (is_mutable) {
      return inodes_.create();
    } else {
      frozen_panic();
    }
  }

  std::size_t inode_count() const override {
    TRACE_CALL;
    return inodes_.size();
  }

  file_id_vector const& get_files_for_inode(inode_id id) const override {
    TRACE_CALL;
    return inodes_.get_files(id);
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) override {
    TRACE_CALL;
    // this is safe even on frozen storage if it's single-threaded
    inodes_.set_files(id, std::move(fv));
  }

  void set_inode_scan_error(inode_id id, file_id fid,
                            std::exception_ptr ep) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      inodes_.set_scan_error(id, fid, std::move(ep));
    } else {
      frozen_panic();
    }
  }

  std::optional<inode_scan_error>
  get_inode_scan_error(inode_id id) const override {
    TRACE_CALL;
    return inodes_.get_scan_error(id);
  }

  void set_inode_num(inode_id id, std::uint64_t num) override {
    TRACE_CALL;
    // this is safe even on frozen storage if it's single-threaded
    inodes_.set_inode_num(id, num);
  }

  std::optional<std::uint64_t> get_inode_num(inode_id id) const override {
    TRACE_CALL;
    return inodes_.get_inode_num(id);
  }

  void
  set_inode_fragments(inode_id id, inode_fragments const& fragments) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      inodes_.set_fragments(id, fragments);
    } else {
      frozen_panic();
    }
  }

  void inode_fragment_add_data_chunk(inode_id id, std::size_t fragment_index,
                                     size_t block, size_t offset,
                                     size_t size) override {
    TRACE_CALL;
    inodes_.fragment_add_data_chunk(id, fragment_index, block, offset, size);
  }

  void inode_fragment_add_hole_chunk(inode_id id, std::size_t fragment_index,
                                     file_size_t size) override {
    TRACE_CALL;
    inodes_.fragment_add_hole_chunk(id, fragment_index, size);
  }

  std::size_t get_inode_fragment_count(inode_id id) const override {
    TRACE_CALL;
    return inodes_.get_fragment_count(id);
  }

  fragment_category
  get_inode_fragment_category(inode_id id, std::size_t index) const override {
    TRACE_CALL;
    return inodes_.get_fragment_category(id, index);
  }

  file_size_t
  get_inode_fragment_size(inode_id id, std::size_t index) const override {
    TRACE_CALL;
    return inodes_.get_fragment_size(id, index);
  }

  packed_chunk_vector const&
  get_inode_fragment_packed_chunks(inode_id id,
                                   std::size_t index) const override {
    TRACE_CALL;
    return inodes_.get_fragment_packed_chunks(id, index);
  }

  void set_inode_similarity(
      inode_id id, std::span<inode_similarity_hash_data const> data) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      inodes_.set_similarity(id, data);
    } else {
      frozen_panic();
    }
  }

  std::optional<std::uint32_t>
  get_inode_similarity_hash(inode_id id, fragment_category cat) const override {
    TRACE_CALL;
    return inodes_.get_similarity_hash(id, cat);
  }

  nilsimsa::hash_type const*
  get_inode_nilsimsa_hash(inode_id id, fragment_category cat) const override {
    TRACE_CALL;
    return inodes_.get_nilsimsa_hash(id, cat);
  }

  void
  dump_inode_similarity(inode_id id, std::ostream& os,
                        std::function<std::string(fragment_category)> const&
                            catlabel) const override {
    TRACE_CALL;
    inodes_.dump_similarity(id, os, catlabel);
  }

  void dump(std::ostream& os) const override;
  void dump_events(std::ostream& os) const override;

 private:
  packed_inode_data inodes_;

#ifdef DWARFS_TRACE_ENTRY_STORAGE_CALLS
  dwarfs::internal::event_tracer mutable ev_;
#endif
};

template <bool Frozen>
void entry_storage_<Frozen>::dump(std::ostream& os) const {
  shared_.dump(os);

  files_.dump(os, "file");
  dirs_.dump(os, "dir");
  links_.dump(os, "link");
  devices_.dump(os, "device");
  others_.dump(os, "other");
}

template <bool Frozen>
void inode_storage_<Frozen>::dump(std::ostream& os) const {
  inodes_.dump(os);
}

template <bool Frozen>
void entry_storage_<Frozen>::dump_events(std::ostream& os
                                         [[maybe_unused]]) const {
#ifdef DWARFS_TRACE_ENTRY_STORAGE_CALLS
  ev_.dump(os);
#endif
}

template <bool Frozen>
void inode_storage_<Frozen>::dump_events(std::ostream& os
                                         [[maybe_unused]]) const {
#ifdef DWARFS_TRACE_ENTRY_STORAGE_CALLS
  ev_.dump(os);
#endif
}

class synchronized_entry_storage_ final : public entry_storage::entry_impl {
 public:
  synchronized_entry_storage_() = default;
  explicit synchronized_entry_storage_(metadata_options const& options)
      : impl_{std::in_place, options} {}

  entry_id make_file(fs::path const& path, file_stat const& st,
                     dir_id const parent) override {
    return impl_.lock()->make_file(path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    dir_id const parent) override {
    return impl_.lock()->make_dir(path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     dir_id const parent) override {
    return impl_.lock()->make_link(path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       dir_id const parent) override {
    return impl_.lock()->make_device(path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      dir_id const parent) override {
    return impl_.lock()->make_other(path, st, parent);
  }

  void create_packed_file_data(file_id id) override {
    impl_.lock()->create_packed_file_data(id);
  }

  void set_entry_index(entry_id id, std::size_t index) override {
    impl_.lock()->set_entry_index(id, index);
  }

  std::optional<std::size_t> get_entry_index(entry_id id) const override {
    return impl_.lock()->get_entry_index(id);
  }

  void set_file_order_index(file_id id, std::size_t index) override {
    impl_.lock()->set_file_order_index(id, index);
  }

  std::size_t get_file_order_index(file_id id) const override {
    return impl_.lock()->get_file_order_index(id);
  }

  void set_link_target(link_id id, std::string link_target,
                       progress& prog) override {
    impl_.lock()->set_link_target(id, std::move(link_target), prog);
  }

  std::string_view get_link_target(link_id id) const override {
    return impl_.lock()->get_link_target(id);
  }

  void set_file_inode(file_id id, inode_id ino) override {
    impl_.lock()->set_file_inode(id, ino);
  }

  inode_id get_file_inode(file_id id) const override {
    return impl_.lock()->get_file_inode(id);
  }

  dir_id get_parent(entry_id const id) const override {
    return impl_.lock()->get_parent(id);
  }

  fs::path get_path(entry_id const id) const override {
    return impl_.lock()->get_path(id);
  }

  std::string get_unix_dpath(entry_id const id) const override {
    return impl_.lock()->get_unix_dpath(id);
  }

  std::string_view get_name(entry_id const id) const override {
    return impl_.lock()->get_name(id);
  }

  void remove_empty_dirs(progress& prog) override {
    impl_.lock()->remove_empty_dirs(prog);
  }

  void
  for_each_entry_in_dir(dir_id,
                        std::function<void(entry_id)> const&) const override {
    DWARFS_PANIC("synchronized for_each_entry_in_dir is not supported");
  }

  entry_id find_in_dir(dir_id id, std::string_view name) const override {
    return impl_.lock()->find_in_dir(id, name);
  }

  bool entry_less_revpath(entry_id lhs, entry_id rhs) const override {
    return impl_.lock()->entry_less_revpath(lhs, rhs);
  }

  void update_global_entry_data(entry_id id,
                                global_entry_data& data) const override {
    impl_.lock()->update_global_entry_data(id, data);
  }

  void
  pack_entry(entry_id id,
             thrift::metadata::metadata::inodes_member_type::reference entry_v2,
             global_entry_data const& data,
             time_resolution_converter const& timeres) const override {
    impl_.lock()->pack_entry(id, entry_v2, data, timeres);
  }

  unique_inode_id get_unique_inode_id(entry_id id) const override {
    return impl_.lock()->get_unique_inode_id(id);
  }

  file_stat::nlink_type get_nlink(entry_id id) const override {
    return impl_.lock()->get_nlink(id);
  }

  void
  create_hardlink(file_id target, file_id source, progress& prog) override {
    impl_.lock()->create_hardlink(target, source, prog);
  }

  std::size_t hardlink_count(file_id id) const override {
    return impl_.lock()->hardlink_count(id);
  }

  void set_file_invalid(file_id id) override {
    impl_.lock()->set_file_invalid(id);
  }

  bool is_file_invalid(file_id id) const override {
    return impl_.lock()->is_file_invalid(id);
  }

  std::span<std::byte>
  get_file_hash_buffer(file_id id, std::size_t buffer_size) override {
    return impl_.lock()->get_file_hash_buffer(id, buffer_size);
  }

  std::string_view get_file_hash(file_id id) const override {
    return impl_.lock()->get_file_hash(id);
  }

  file_size_t get_entry_size(entry_id id) const override {
    return impl_.lock()->get_entry_size(id);
  }

  file_size_info get_entry_size_info(entry_id id) const override {
    return impl_.lock()->get_entry_size_info(id);
  }

  void set_entry_empty(entry_id id) override {
    impl_.lock()->set_entry_empty(id);
  }

  void set_inode_num_for_entry(entry_id id, std::uint64_t ino) override {
    impl_.lock()->set_inode_num_for_entry(id, ino);
  }

  std::optional<std::uint64_t>
  get_inode_num_for_entry(entry_id id) const override {
    return impl_.lock()->get_inode_num_for_entry(id);
  }

  file_stat::dev_type get_represented_device(device_id id) const override {
    return impl_.lock()->get_represented_device(id);
  }

  bool empty() const noexcept override { return impl_.lock()->empty(); }

  void dump(std::ostream& os) const override { impl_.lock()->dump(os); }
  void dump_events(std::ostream& os) const override {
    impl_.lock()->dump_events(os);
  }

  std::unique_ptr<entry_impl> freeze() override {
    return impl_.lock()->freeze();
  }

 private:
  dwarfs::internal::synchronized<entry_storage_<false>> impl_;
};

class synchronized_inode_storage_ final : public entry_storage::inode_impl {
 public:
  inode_id make_inode() override { return impl_.lock()->make_inode(); }

  std::size_t inode_count() const override {
    return impl_.lock()->inode_count();
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) override {
    impl_.lock()->set_files_for_inode(id, std::move(fv));
  }

  file_id_vector const& get_files_for_inode(inode_id id) const override {
    return impl_.lock()->get_files_for_inode(id);
  }

  void set_inode_scan_error(inode_id id, file_id fid,
                            std::exception_ptr ep) override {
    impl_.lock()->set_inode_scan_error(id, fid, std::move(ep));
  }

  std::optional<inode_scan_error>
  get_inode_scan_error(inode_id id) const override {
    return impl_.lock()->get_inode_scan_error(id);
  }

  void set_inode_num(inode_id id, std::uint64_t num) override {
    impl_.lock()->set_inode_num(id, num);
  }

  std::optional<std::uint64_t> get_inode_num(inode_id id) const override {
    return impl_.lock()->get_inode_num(id);
  }

  void
  set_inode_fragments(inode_id id, inode_fragments const& fragments) override {
    impl_.lock()->set_inode_fragments(id, fragments);
  }

  void inode_fragment_add_data_chunk(inode_id id, std::size_t fragment_index,
                                     size_t block, size_t offset,
                                     size_t size) override {
    impl_.lock()->inode_fragment_add_data_chunk(id, fragment_index, block,
                                                offset, size);
  }

  void inode_fragment_add_hole_chunk(inode_id id, std::size_t fragment_index,
                                     file_size_t size) override {
    impl_.lock()->inode_fragment_add_hole_chunk(id, fragment_index, size);
  }

  std::size_t get_inode_fragment_count(inode_id id) const override {
    return impl_.lock()->get_inode_fragment_count(id);
  }

  fragment_category
  get_inode_fragment_category(inode_id id, std::size_t index) const override {
    return impl_.lock()->get_inode_fragment_category(id, index);
  }

  file_size_t
  get_inode_fragment_size(inode_id id, std::size_t index) const override {
    return impl_.lock()->get_inode_fragment_size(id, index);
  }

  packed_chunk_vector const&
  get_inode_fragment_packed_chunks(inode_id id,
                                   std::size_t index) const override {
    return impl_.lock()->get_inode_fragment_packed_chunks(id, index);
  }

  void set_inode_similarity(
      inode_id id, std::span<inode_similarity_hash_data const> data) override {
    impl_.lock()->set_inode_similarity(id, data);
  }

  std::optional<std::uint32_t>
  get_inode_similarity_hash(inode_id id, fragment_category cat) const override {
    return impl_.lock()->get_inode_similarity_hash(id, cat);
  }

  nilsimsa::hash_type const*
  get_inode_nilsimsa_hash(inode_id id, fragment_category cat) const override {
    return impl_.lock()->get_inode_nilsimsa_hash(id, cat);
  }

  void
  dump_inode_similarity(inode_id id, std::ostream& os,
                        std::function<std::string(fragment_category)> const&
                            catlabel) const override {
    impl_.lock()->dump_inode_similarity(id, os, catlabel);
  }

  void dump(std::ostream& os) const override { impl_.lock()->dump(os); }
  void dump_events(std::ostream& os) const override {
    impl_.lock()->dump_events(os);
  }

  std::unique_ptr<inode_impl> freeze() override {
    return impl_.lock()->freeze();
  }

 private:
  dwarfs::internal::synchronized<inode_storage_<false>> impl_;
};

entry_storage::entry_storage()
    : entry_impl_{std::make_unique<synchronized_entry_storage_>()}
    , inode_impl_{std::make_unique<synchronized_inode_storage_>()} {}

entry_storage::entry_storage(metadata_options const& options)
    : entry_impl_{std::make_unique<synchronized_entry_storage_>(options)}
    , inode_impl_{std::make_unique<synchronized_inode_storage_>()} {}

entry_storage::~entry_storage() = default;
entry_storage::entry_storage(entry_storage&&) noexcept = default;
entry_storage& entry_storage::operator=(entry_storage&&) noexcept = default;

void entry_storage::dump(std::ostream& os) const {
  entry_impl_->dump(os);
  inode_impl_->dump(os);
  entry_impl_->dump_events(os);
  inode_impl_->dump_events(os);
}

void entry_storage::dump_entries(std::ostream& os) const {
  entry_impl_->dump(os);
  entry_impl_->dump_events(os);
}

void entry_storage::dump_inodes(std::ostream& os) const {
  inode_impl_->dump(os);
  inode_impl_->dump_events(os);
}

std::string entry_storage::dump() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

std::string entry_storage::dump_entries() const {
  std::ostringstream oss;
  dump_entries(oss);
  return oss.str();
}

std::string entry_storage::dump_inodes() const {
  std::ostringstream oss;
  dump_inodes(oss);
  return oss.str();
}

void entry_storage::freeze_entries() noexcept {
  entry_impl_ = entry_impl_->freeze();
}

void entry_storage::freeze_inodes() noexcept {
  inode_impl_ = inode_impl_->freeze();
}

dir_handle
entry_storage::create_root_dir(fs::path const& path, file_stat const& st) {
  DWARFS_CHECK(empty(), "entry_storage root already set");
  return {*this, entry_impl_->make_dir(path, st, dir_id{})};
}

file_handle entry_storage::create_file(fs::path const& path, dir_handle parent,
                                       file_stat const& st) {
  assert(!empty());
  return {*this, entry_impl_->make_file(path, st, parent.id())};
}

dir_handle entry_storage::create_dir(fs::path const& path, dir_handle parent,
                                     file_stat const& st) {
  assert(!empty());
  return {*this, entry_impl_->make_dir(path, st, parent.id())};
}

link_handle entry_storage::create_link(fs::path const& path, dir_handle parent,
                                       file_stat const& st) {
  assert(!empty());
  return {*this, entry_impl_->make_link(path, st, parent.id())};
}

device_handle
entry_storage::create_device(fs::path const& path, dir_handle parent,
                             file_stat const& st) {
  assert(!empty());
  return {*this, entry_impl_->make_device(path, st, parent.id())};
}

other_handle
entry_storage::create_other(fs::path const& path, dir_handle parent,
                            file_stat const& st) {
  assert(!empty());
  return {*this, entry_impl_->make_other(path, st, parent.id())};
}

inode_handle entry_storage::create_inode() {
  return {*this, inode_impl_->make_inode()};
}

} // namespace dwarfs::writer::internal

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
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/inode_fragments.h>
#include <dwarfs/writer/metadata_options.h>

#include <dwarfs/internal/fsst.h>
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
#define DWARFS_HANDLE_NATIVE_PATHS
#endif

namespace fs = std::filesystem;
using namespace std::string_view_literals;

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

template <typename T>
std::size_t string_memory_usage(T const& s) {
  return sizeof(s) + (uses_inline_buffer(s) ? 0 : s.capacity());
}

class memory_usage_dumper {
 public:
  struct size_info {
    std::string_view label;
    std::size_t bytes;
    std::optional<std::size_t> count;
    bool is_tuple_field;
  };

  void add(std::string_view label, std::size_t bytes,
           std::optional<std::size_t> count = std::nullopt) {
    sizes_.push_back({label, bytes, count, false});
  }

  template <typename TupleType>
  void add_tuple_field_sizes(
      segtor<TupleType> const& data,
      std::array<std::string_view, std::tuple_size_v<TupleType>> const&
          field_labels) {
    auto const field_bytes = data.field_sizes_in_bytes();
    for (std::size_t i = 0; i < field_labels.size(); ++i) {
      if (field_bytes[i] > 0) {
        sizes_.push_back({field_labels[i], field_bytes[i], std::nullopt, true});
      }
    }
  }

  void dump(std::ostream& os, std::string_view name,
            std::optional<std::size_t> count = std::nullopt) const {
    auto const total_bytes =
        std::accumulate(sizes_.begin(), sizes_.end(), 0ULL,
                        [](std::size_t acc, auto const& si) {
                          return acc + (si.is_tuple_field ? 0 : si.bytes);
                        });

    if (count.has_value()) {
      os << *count << " ";
    }

    os << name << ": " << size_with_unit(total_bytes) << "\n";

    for (auto const& [label, bytes, num, is_tuple_field] : sizes_) {
      if (bytes > 0) {
        os << (is_tuple_field ? "    " : "  ");
        if (num.has_value()) {
          os << *num << " ";
        }
        os << label << ": " << size_with_unit(bytes) << "\n";
      }
    }
  }

 private:
  std::vector<size_info> sizes_;
};

enum class path_name_storage : std::size_t {
  utf8 = 0,
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  native = 1,
#endif
};

// TODO: remove if we don't need these
constexpr std::size_t kPathNameStorageIndexField{0};
// constexpr std::size_t kPathNameStorageTypeField{1};
constexpr std::array kPathNameStorageFieldNames{
    "index"sv,
    "type"sv,
};
using path_name_storage_tuple = std::tuple<std::size_t, path_name_storage>;

using inode_scan_error = std::pair<file_id, std::exception_ptr>;

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
    utf8_path_index_.reset();
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    native_path_index_.reset();
#endif
    device_index_.reset();
    mode_index_.reset();
    uid_index_.reset();
    gid_index_.reset();
    link_target_index_.reset();
  }

  auto set_root_path_component(fs::path const& component)
      -> path_name_storage_tuple {
    utf8_root_path_component_ = path_to_u8string_sanitized(component);
    native_root_path_component_ = component.native();
    return {}; // doesn't matter
  }

  auto
  add_path_component(fs::path const& component) -> path_name_storage_tuple {
    return add_path_component_impl(component.filename());
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

  auto get_utf8_root_path_component() const -> std::string {
    return u8string_to_string(utf8_root_path_component_.value());
  }

  auto get_utf8_path_component(path_name_storage_tuple const& storage) const
      -> std::string {
    [[maybe_unused]] auto const& [index, type] = storage;
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    if (type == path_name_storage::native) {
      return path_to_utf8_string_sanitized(native_path_components_.at(index));
    }
#endif
    return u8string_to_string(utf8_component_at(index));
  }

  auto get_native_root_path_component() const -> fs::path::string_type {
    return native_root_path_component_.value();
  }

  auto get_native_path_component(path_name_storage_tuple const& storage) const
      -> fs::path::string_type {
    [[maybe_unused]] auto const& [index, type] = storage;
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    if (type == path_name_storage::native) {
      return native_path_components_.at(index);
    }
#endif
    return fs::path(utf8_component_at(index)).native();
  }

  auto
  get_utf8_path_component_ref(std::size_t index) const -> std::u8string_view {
    return utf8_path_components_.at(index);
  }

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  auto get_native_path_component_ref(std::size_t index) const
      -> std::basic_string_view<fs::path::string_type::value_type> {
    return native_path_components_.at(index);
  }
#endif

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

  auto get_link_target(size_t index) const -> std::string {
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

  std::size_t utf8_path_component_count() const {
    return utf8_path_component_count_.value_or(utf8_path_components_.size());
  }

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  std::size_t native_path_component_count() const {
    return native_path_components_.size();
  }
#endif

  void set_utf8_path_component_count(std::size_t const count) {
    assert(!utf8_path_component_count_.has_value());
    utf8_path_components_.resize(count);
  }

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  void set_native_path_component_count(std::size_t const count) {
    native_path_components_.resize(count);
  }
#endif

  void swap_utf8_path_components(std::size_t const a, std::size_t const b) {
    assert(!utf8_path_component_count_.has_value());
    using std::swap;
    swap(utf8_path_components_.at(a), utf8_path_components_.at(b));
  }

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  void swap_native_path_components(std::size_t const a, std::size_t const b) {
    using std::swap;
    swap(native_path_components_.at(a), native_path_components_.at(b));
  }
#endif

  std::vector<std::string> get_sorted_path_components() const {
    std::vector<std::string> result;

    if (utf8_path_component_count_.has_value()) {
      result.reserve(*utf8_path_component_count_);

      for (std::size_t i = 0; i < *utf8_path_component_count_; ++i) {
        auto const begin = compressed_utf8_path_components_->positions.get(i);
        auto const end = compressed_utf8_path_components_->positions.get(i + 1);
        auto const sv =
            std::string_view{compressed_utf8_path_components_->buffer}.substr(
                begin, end - begin);
        result.push_back(utf8_path_decoder_->decompress(sv));
      }
    } else {
      result.reserve(utf8_path_components_.size()
#ifdef DWARFS_HANDLE_NATIVE_PATHS
                     + native_path_components_.size()
#endif
      );

      for (auto const& component : utf8_path_components_) {
        result.push_back(u8string_to_string(component));
      }

#ifdef DWARFS_HANDLE_NATIVE_PATHS
      for (auto const& component : native_path_components_) {
        result.push_back(path_to_utf8_string_sanitized(component));
      }
#endif
    }

    return result;
  }

  std::size_t
  get_path_component_index(path_name_storage_tuple const& entry) const {
    [[maybe_unused]] auto [index, type] = entry;

#ifdef DWARFS_HANDLE_NATIVE_PATHS
    auto const utf8_count =
        utf8_path_component_count_.value_or(utf8_path_components_.size());

    if (type == path_name_storage::native) {
      index += utf8_count;
    }

    assert(index < utf8_count + native_path_components_.size());
#endif

    return index;
  }

  void freeze_path_storage() {
    assert(!utf8_path_component_count_.has_value());

    if (utf8_path_components_.empty()) {
      return;
    }

    utf8_path_component_count_ = utf8_path_components_.size();

#ifdef DWARFS_HANDLE_NATIVE_PATHS
    for (auto& component : native_path_components_) {
      utf8_path_components_.emplace_back(path_to_u8string_sanitized(component));
    }
#endif

    compressed_utf8_path_components_ =
        dwarfs::internal::fsst_encoder::compress(utf8_path_components_);

    if (!compressed_utf8_path_components_) {
      utf8_path_components_.resize(*utf8_path_component_count_);
      utf8_path_component_count_.reset();
      return;
    }

    utf8_path_decoder_.emplace(compressed_utf8_path_components_->dictionary);

    utf8_path_components_.clear();
  }

  bool has_bulk_compressed_path_components() const {
    return compressed_utf8_path_components_.has_value();
  }

  dwarfs::internal::fsst_encoder::bulk_compression_result
  steal_bulk_compressed_path_components() {
    auto result = std::move(compressed_utf8_path_components_.value());
    compressed_utf8_path_components_.reset();
    return result;
  }

 private:
  std::u8string utf8_component_at(std::size_t index) const {
    if (utf8_path_component_count_.has_value()) {
      assert(index < *utf8_path_component_count_);
      assert(compressed_utf8_path_components_.has_value());
      assert(utf8_path_decoder_.has_value());
      auto const begin = compressed_utf8_path_components_->positions.get(index);
      auto const end =
          compressed_utf8_path_components_->positions.get(index + 1);
      auto const& buffer = compressed_utf8_path_components_->buffer;
      auto const sv =
          std::u8string_view{reinterpret_cast<char8_t const*>(buffer.data()),
                             buffer.size()}
              .substr(begin, end - begin);
      return utf8_path_decoder_->decompress(sv);
    }

    return utf8_path_components_.at(index);
  }

  auto add_path_component_impl(fs::path const& component)
      -> path_name_storage_tuple {
    std::size_t index;
    path_name_storage type{path_name_storage::utf8};

#ifdef DWARFS_HANDLE_NATIVE_PATHS
    if (!is_well_formed_utf16_path(component)) {
      type = path_name_storage::native;
      index = native_path_index_->add(component);
    }
#endif

    if (type == path_name_storage::utf8) {
      index = utf8_path_index_->add(path_to_u8string_sanitized(component));
    }

    return {index, type};
  }

  std::optional<std::u8string> utf8_root_path_component_;
  std::optional<fs::path::string_type> native_root_path_component_;

  cao_vector<std::u8string> utf8_path_components_;
  std::optional<flat_cao_index<std::u8string>> utf8_path_index_{
      utf8_path_components_};
  std::optional<std::size_t> utf8_path_component_count_;
  std::optional<dwarfs::internal::fsst_encoder::bulk_compression_result>
      compressed_utf8_path_components_;
  std::optional<dwarfs::internal::fsst_decoder> utf8_path_decoder_;

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  cao_vector<fs::path::string_type> native_path_components_;
  std::optional<flat_cao_index<fs::path::string_type>> native_path_index_{
      native_path_components_};
#endif

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
    path_name_storage_tuple path_storage;
  };

  explicit packed_entry_data(entry_type t)
      : this_type_{t} {}

  packed_entry_data(entry_type t, metadata_options const& options)
      : this_type_{t}
      , keep_mtime_{!options.timestamp.has_value()}
      , keep_atime_{options.keep_all_times}
      , keep_ctime_{options.keep_all_times}
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
  static constexpr std::array kStatCommonFieldNames{
      "nlink"sv,     "mode"sv,       "uid"sv,       "gid"sv,
      "atime sec"sv, "atime nsec"sv, "mtime sec"sv, "mtime nsec"sv,
      "ctime sec"sv, "ctime nsec"sv, "inode"sv,     "device index"sv,
  };

  using stat_common_tuple =
      std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                 std::int64_t, std::uint64_t, std::int64_t, std::uint64_t,
                 std::int64_t, std::uint64_t, std::uint64_t, std::uint64_t>;

  bool empty() const { return path_storage_index_.empty(); }

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

  fs::path::string_type get_native_path_string(shared_entry_data const& shared,
                                               uint64_t const index) const {
    auto const storage_ix = path_storage_index_.at(index);
    return shared.get_native_path_component(storage_ix);
  }

  std::string
  get_path_string(shared_entry_data const& shared, uint64_t const index) const {
    auto const storage_ix = path_storage_index_.at(index);
    return shared.get_utf8_path_component(storage_ix);
  }

  std::size_t get_path_storage_index(uint64_t const index) const {
    return path_storage_index_.template get_field<kPathNameStorageIndexField>(
        index);
  }

  path_name_storage_tuple get_path_storage(uint64_t const index) const {
    return path_storage_index_.get(index);
  }

  void set_path_storage(uint64_t const index,
                        path_name_storage_tuple const storage) {
    path_storage_index_.at(index) = storage;
  }

  void update_global_entry_data(shared_entry_data const& shared, uint64_t index,
                                global_entry_data& data) const;

  void
  pack_entry(shared_entry_data const& shared, uint64_t index,
             thrift::metadata::metadata::inodes_member_type::reference entry_v2,
             global_entry_data const& data,
             time_resolution_converter const& timeres) const;

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

  void drop_hash_buffers() {
    if (file_hashes_.has_value()) {
      file_hashes_.reset();
      for (auto&& fd : file_data_vec_) {
        get<kFileHashIndexField>(fd) = std::nullopt;
      }
      file_data_vec_.optimize_storage();
    }
  }

 private:
  struct mtime_traits;
  struct atime_traits;
  struct ctime_traits;

  template <typename... Ts, typename Fn>
  static void for_each_time(Fn const& fn) {
    (fn.template operator()<Ts>(), ...);
  }

  static void for_all_times(auto&& fn) {
    for_each_time<mtime_traits, atime_traits, ctime_traits>(fn);
  }

  entry_type this_type_;

  bool const keep_mtime_{true};
  bool const keep_atime_{false};
  bool const keep_ctime_{false};
  bool const keep_subsecond_{false};

  // index into `shared_entry_data::path_components_`
  segtor<path_name_storage_tuple> path_storage_index_;

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
  static constexpr std::array kFileDataFieldNames{
      "file hash index"sv,
      "hardlink count"sv,
      "inode number"sv,
  };

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

struct packed_entry_data::mtime_traits {
  static constexpr auto keep = &packed_entry_data::keep_mtime_;

  static constexpr auto sec_field = kModificationTimeSecondField;
  static constexpr auto nsec_field = kModificationTimeSubsecondField;

  static auto sec(auto const& st) { return st.mtime_unchecked(); }
  static auto nsec(auto const& st) { return st.mtime_nsec_unchecked(); }

  static void add(auto& data, auto v) { data.add_mtime(v); }
  static void set_spec(auto& out, auto sec, auto nsec) {
    out.set_mtimespec(sec, nsec);
  }
};

struct packed_entry_data::atime_traits {
  static constexpr auto keep = &packed_entry_data::keep_atime_;

  static constexpr auto sec_field = kAccessTimeSecondField;
  static constexpr auto nsec_field = kAccessTimeSubsecondField;

  static auto sec(auto const& st) { return st.atime_unchecked(); }
  static auto nsec(auto const& st) { return st.atime_nsec_unchecked(); }

  static void add(auto& data, auto v) { data.add_atime(v); }
  static void set_spec(auto& out, auto sec, auto nsec) {
    out.set_atimespec(sec, nsec);
  }
};

struct packed_entry_data::ctime_traits {
  static constexpr auto keep = &packed_entry_data::keep_ctime_;

  static constexpr auto sec_field = kStatusChangeTimeSecondField;
  static constexpr auto nsec_field = kStatusChangeTimeSubsecondField;

  static auto sec(auto const& st) { return st.ctime_unchecked(); }
  static auto nsec(auto const& st) { return st.ctime_nsec_unchecked(); }

  static void add(auto& data, auto v) { data.add_ctime(v); }
  static void set_spec(auto& out, auto sec, auto nsec) {
    out.set_ctimespec(sec, nsec);
  }
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
  static constexpr std::array kInodeFragmentFieldNames{
      "category"sv,
      "subcategory"sv,
      "kind"sv,
      "size"sv,
  };

  using inode_fragment_tuple =
      std::tuple<std::uint64_t, std::optional<std::uint64_t>, fragment_kind,
                 std::uint64_t>;

  static constexpr std::size_t kFragmentInfoOffsetField = 0;
  static constexpr std::size_t kFragmentInfoCountField = 1;
  static constexpr std::array kInodeFragmentInfoFieldNames{
      "offset"sv,
      "count"sv,
  };

  using inode_fragment_info_tuple = std::tuple<std::uint64_t, std::uint64_t>;

  static constexpr std::size_t kSimilarityInfoOffsetField = 0;
  static constexpr std::size_t kSimilarityInfoCountField = 1;
  static constexpr std::array kInodeSimilarityInfoFieldNames{
      "offset"sv,
      "count"sv,
  };

  using inode_similarity_info_tuple = std::tuple<std::uint64_t, std::uint64_t>;

  enum class similarity_hash_type : std::uint64_t {
    similarity = 0,
    nilsimsa = 1,
  };

  static constexpr std::size_t kSimilarityHashCategoryField = 0;
  static constexpr std::size_t kSimilarityHashSubcategoryField = 1;
  static constexpr std::size_t kSimilarityHashTypeField = 2;
  static constexpr std::size_t kSimilarityHashIndexField = 3;
  static constexpr std::array kInodeSimilarityHashFieldNames{
      "category"sv,
      "subcategory"sv,
      "type"sv,
      "index"sv,
  };

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
  auto const total_utf8_path_bytes = std::accumulate(
      utf8_path_components_.begin(), utf8_path_components_.end(), 0ULL,
      [](std::size_t acc, auto const& pc) {
        return acc + string_memory_usage(pc);
      });
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  auto const total_native_path_bytes = std::accumulate(
      native_path_components_.begin(), native_path_components_.end(), 0ULL,
      [](std::size_t acc, auto const& pc) {
        return acc + string_memory_usage(pc);
      });
#endif
  auto const total_link_bytes =
      std::accumulate(link_targets_.begin(), link_targets_.end(), 0ULL,
                      [](std::size_t acc, std::string const& link) {
                        return acc + sizeof(std::string) + link.size();
                      });

  memory_usage_dumper d;

  d.add("utf8 path components", total_utf8_path_bytes,
        utf8_path_components_.size());
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  d.add("native path components", total_native_path_bytes,
        native_path_components_.size());
#endif
  d.add("devices", devices_.size() * sizeof(devices_[0]), devices_.size());
  d.add("modes", modes_.size() * sizeof(modes_[0]), modes_.size());
  d.add("uids", uids_.size() * sizeof(uids_[0]), uids_.size());
  d.add("gids", gids_.size() * sizeof(gids_[0]), gids_.size());
  d.add("link targets", total_link_bytes, link_targets_.size());
  d.add("dir entries", total_cao_id_vec_bytes(dir_entries_),
        dir_entries_.size());

  d.dump(os, "shared entry data");
}

void packed_entry_data::dump(std::ostream& os, std::string_view name) const {
  if (path_storage_index_.empty()) {
    os << "no " << name << " entries\n";
    return;
  }

  memory_usage_dumper d;

  d.add("path storage index", path_storage_index_.size_in_bytes());
  d.add_tuple_field_sizes(path_storage_index_, kPathNameStorageFieldNames);
  d.add("stat common", stat_common_.size_in_bytes());
  d.add_tuple_field_sizes(stat_common_, kStatCommonFieldNames);
  d.add("size", entry_size_.size_in_bytes());
  d.add("allocated size",
        entry_allocated_size_.capacity() *
            sizeof(decltype(entry_allocated_size_)::value_type));
  d.add("parent dir index", parent_dir_id_.size_in_bytes());
  d.add("link target index", link_target_index_.size_in_bytes());
  d.add("represented device", represented_device_.size_in_bytes());
  d.add("inode number", inode_num_.size_in_bytes());
  d.add("final entry index", final_entry_index_.size_in_bytes());
  d.add("file data vec", file_data_vec_.size_in_bytes(), file_data_vec_.size());
  d.add_tuple_field_sizes(file_data_vec_, kFileDataFieldNames);
  d.add("file invalid vec",
        file_invalid_vec_.size() * sizeof(file_invalid_vec_[0]),
        file_invalid_vec_.size());
  d.add("file hashes", file_hashes_ ? file_hashes_->size_in_bytes() : 0,
        file_hashes_ ? file_hashes_->size() : 0);
  d.add("file inode id", file_inode_id_.size_in_bytes());
  d.add("file data index", file_data_index_.size_in_bytes());
  d.add("file order index", file_order_index_.size_in_bytes());

  d.dump(os, std::string{name} + " entries", path_storage_index_.size());
}

void packed_inode_data::dump(std::ostream& os) const {
  std::vector<std::pair<std::string_view, std::size_t>> sizes;

  memory_usage_dumper d;

  d.add("hardlinks", total_cao_id_vec_bytes(files_for_inode_));
  d.add("inode numbers", inode_num_.size_in_bytes());
  d.add("scan errors",
        inode_scan_errors_.capacity() * sizeof(inode_scan_error));
  d.add("fragment info", fragment_info_.size_in_bytes());
  d.add_tuple_field_sizes(fragment_info_, kInodeFragmentInfoFieldNames);
  d.add("fragment data", fragment_data_.size_in_bytes());
  d.add_tuple_field_sizes(fragment_data_, kInodeFragmentFieldNames);
  d.add("fragment chunks", total_cao_id_vec_bytes(fragment_chunks_));
  d.add("similarity info", similarity_info_.size_in_bytes());
  d.add_tuple_field_sizes(similarity_info_, kInodeSimilarityInfoFieldNames);
  d.add("similarity hashes", similarity_hash_.size_in_bytes());
  d.add_tuple_field_sizes(similarity_hash_, kInodeSimilarityHashFieldNames);
  d.add("similarity hash data",
        similarity_hash_data_.size() * sizeof(similarity_hash_data_[0]));
  d.add("nilsimsa hash data",
        nilsimsa_hash_data_.size() * sizeof(nilsimsa_hash_data_[0]));

  d.dump(os, "inodes", inode_num_.size());
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
  auto const path_storage = is_root ? shared.set_root_path_component(path)
                                    : shared.add_path_component(path);
  auto const entry_ix = path_storage_index_.size();
  path_storage_index_.push_back(path_storage);
  parent_dir_id_.push_back(parent);
  final_entry_index_.push_back(std::nullopt);

  auto const nlink = st.nlink_unchecked();
  assert(nlink > 0);

  stat_common_tuple tmp{};
  std::get<kNlinkMinusOneField>(tmp) = nlink - 1;
  std::get<kModeIndexField>(tmp) = shared.add_mode(st.mode_unchecked());
  std::get<kUidIndexField>(tmp) = shared.add_uid(st.uid_unchecked());
  std::get<kGidIndexField>(tmp) = shared.add_gid(st.gid_unchecked());

  for_all_times([&]<typename time>() {
    if (this->*time::keep) {
      std::get<time::sec_field>(tmp) = time::sec(st);

      if (this->keep_subsecond_) {
        std::get<time::nsec_field>(tmp) = time::nsec(st);
      }
    }
  });

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

  return {entry_ix, path_storage};
}

void packed_entry_data::update_global_entry_data(
    shared_entry_data const& shared, uint64_t const index,
    global_entry_data& data) const {
  auto const& stat = stat_common_.at(index);

  data.add_mode(shared.get_mode(get<kModeIndexField>(stat)));
  data.add_uid(shared.get_uid(get<kUidIndexField>(stat)));
  data.add_gid(shared.get_gid(get<kGidIndexField>(stat)));

  for_all_times([&]<typename time>() {
    if (this->*time::keep) {
      time::add(data, std::get<time::sec_field>(stat));
    }
  });
}

void packed_entry_data::pack_entry(
    shared_entry_data const& shared, uint64_t const index,
    thrift::metadata::metadata::inodes_member_type::reference entry_v2,
    global_entry_data const& data,
    time_resolution_converter const& timeres) const {
  auto const& stat = stat_common_.at(index);
  file_stat out{};

  out.set_mode(shared.get_mode(get<kModeIndexField>(stat)));
  out.set_uid(shared.get_uid(get<kUidIndexField>(stat)));
  out.set_gid(shared.get_gid(get<kGidIndexField>(stat)));

  for_all_times([&]<typename time>() {
    if (this->*time::keep) {
      time::set_spec(out, std::get<time::sec_field>(stat),
                     std::get<time::nsec_field>(stat));
    } else {
      time::set_spec(out, 0, 0);
    }
  });

  data.pack_inode_stat(entry_v2, out, timeres);
}

[[noreturn]] void frozen_panic() { DWARFS_PANIC("entry_storage is frozen"); }

} // namespace

template <bool Frozen>
class entry_storage_ final : public entry_storage::entry_impl {
 public:
  static constexpr bool is_mutable = !Frozen;
  static constexpr entry_id kRootId{entry_type::E_DIR, 0};

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

  std::unique_ptr<entry_impl> freeze(logger& lgr, progress& prog) override {
    if constexpr (is_mutable) {
      auto set_status = [&prog](std::string_view status) {
        prog.set_status_function(
            [message = "freezing entries: " + std::string{status}](
                progress const&, size_t) { return message; });
      };

      LOG_PROXY(debug_logger_policy, lgr);

      set_status("dropping indices");
      {
        auto tv = LOG_CPU_TIMED_VERBOSE;
        shared_.drop_indices();
        tv << "dropped indices";
      }

      // This must be done before sorting directory entries, since
      // `sort_all_directory_entries()` relies on the path storage indices
      // to already be sorted.
      sort_path_storage(lgr, set_status);

      set_status("sorting directory entries");
      {
        auto tv = LOG_CPU_TIMED_VERBOSE;
        sort_all_directory_entries();
        tv << "sorted directory entries";
      }

      set_status("compressing path storage");
      {
        auto tv = LOG_CPU_TIMED_VERBOSE;
        shared_.freeze_path_storage();
        tv << "compressed path storage";
      }

      return std::make_unique<entry_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  entry_id
  make_obj_(entry_type const type, packed_entry_data& data,
            fs::path const& path, file_stat const& st, dir_id const parent) {
    if constexpr (is_mutable) {
      auto const [entry_ix, path_storage] =
          data.add_entry_common(shared_, type, path, st, parent);

      if (parent) {
        shared_.add_dir_entry(parent, type, entry_ix);

        if (auto const it = dir_entry_lookup_.find(parent.index());
            it != dir_entry_lookup_.end()) {
          auto& lookup = it->second;
          auto const inserted [[maybe_unused]] =
              lookup
                  .emplace(shared_.get_utf8_path_component(path_storage),
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

  std::string get_link_target(link_id id) const override {
    TRACE_CALL;
    return shared_.get_link_target(links_.get_link_target_index(id));
  }

  dir_id get_parent(entry_id const id) const override {
    TRACE_CALL;
    return get_parent_impl(id);
  }

  fs::path get_path(entry_id id) const override {
    TRACE_CALL;

    small_vector<entry_id, 64> components;

    fill_path_components(id, components);

    return fs::path(build_path_from_components<fs::path::string_type,
                                               fs::path::preferred_separator>(
        components, [this](entry_id const id) -> fs::path::string_type {
          return get_native_path_string_impl(id);
        }));
  }

  std::string get_unix_dpath(entry_id id) const override {
    TRACE_CALL;

    bool const is_dir = id.is_dir();
    std::string p;

    small_vector<entry_id, 64> components;

    fill_path_components(id, components);

    p = build_path_from_components<std::string, '/'>(
        components, [this](entry_id const id) -> std::string {
          return get_path_string_impl(id);
        });

    if (is_dir && !p.ends_with('/')) {
      p.append(1, '/');
    }

    return p;
  }

  std::string get_name(entry_id const id) const override {
    TRACE_CALL;
    return get_path_string_impl(id);
  }

  void remove_empty_dirs(progress& prog) override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      remove_empty_dirs_impl(prog, dir_id{kRootId});
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

    if constexpr (is_mutable) {
      DWARFS_PANIC(
          "entry_less_revpath is not supported for mutable entry_storage");
    } else {
#ifdef DWARFS_HANDLE_NATIVE_PATHS
      if (shared_.native_path_component_count() > 0) {
        return entry_less_impl(lhs, rhs, [this](entry_id const id) {
          return get_path_string_impl(id);
        });
      }
#endif

      // If there are no native path components, and after freezing, path
      // components are ordered, and so are their indices. We can thus
      // compare entries directly by their path storage index without
      // having to materialize the path strings at all.
      return entry_less_impl(lhs, rhs, [this](entry_id const id) {
        return get_path_storage_index_impl(id);
      });
    }
  }

  std::vector<std::string> get_sorted_path_components() const override {
    TRACE_CALL;

    if constexpr (is_mutable) {
      DWARFS_PANIC("sorted path components can only be used after freezing");
    } else {
      return shared_.get_sorted_path_components();
    }
  }

  std::size_t get_path_component_index(entry_id id) const override {
    TRACE_CALL;

    if constexpr (is_mutable) {
      DWARFS_PANIC("path component index can only be used after freezing");
    } else {
      if (id == kRootId) {
        return 0;
      }

      return shared_.get_path_component_index(get_path_storage_impl(id));
    }
  }

  bool has_bulk_compressed_path_components() const override {
    TRACE_CALL;

    if constexpr (is_mutable) {
      return false;
    } else {
      return shared_.has_bulk_compressed_path_components();
    }
  }

  dwarfs::internal::fsst_encoder::bulk_compression_result
  steal_bulk_compressed_path_components() override {
    TRACE_CALL;

    if constexpr (is_mutable) {
      DWARFS_PANIC(
          "bulk compressed path components can only be stolen after freezing");
    } else {
      return shared_.steal_bulk_compressed_path_components();
    }
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

  void sort_file_id_vector(file_id_vector& fv) const override {
    TRACE_CALL;
    if constexpr (is_mutable) {
      DWARFS_PANIC(
          "sorting file_id_vector is not supported for mutable entry_storage");
    } else {
      // See comment in `entry_less_revpath` about ordering. In this case,
      // we only want deterministic ordering, so we don't care about the
      // native paths at all.
      std::ranges::sort(fv, [this](file_id const a, file_id const b) {
        return entry_less_impl(a, b, [this](entry_id const id) {
          return get_path_storage_impl(id);
        });
      });
    }
  }

  void drop_file_hashes() override {
    TRACE_CALL;
    files_.drop_hash_buffers();
  }

  void dump(std::ostream& os) const override;
  void dump_events(std::ostream& os) const override;

 private:
  template <typename KeyFn>
  bool entry_less_impl(entry_id lhs, entry_id rhs, KeyFn const& key_fn) const {
    while (lhs.valid() && rhs.valid()) {
      auto const& lkey = key_fn(lhs);
      auto const& rkey = key_fn(rhs);

      if (lkey != rkey) {
        return lkey < rkey;
      }

      lhs = get_parent_impl(lhs);
      rhs = get_parent_impl(rhs);
    }

    return rhs.valid();
  }

  template <typename StatusFunc>
  void sort_path_storage(logger& lgr, StatusFunc const& set_status)
    requires is_mutable;

  template <typename Func>
  void walk_entries_with_root(Func const& func) const {
    func(kRootId);
    walk_entries(func);
  }

  template <typename Func>
  void walk_entries(Func const& func) const {
    walk_entries_rec(dir_id{kRootId}, func);
  }

  template <typename Func>
  void walk_entries_rec(dir_id dir, Func const& func) const {
    for (auto const eid : shared_.get_dir_entries(dir)) {
      func(eid);
      if (auto const did = dir_id{eid}) {
        walk_entries_rec(did, func);
      }
    }
  }

  template <typename Vector>
  void fill_path_components(entry_id id, Vector& components) const {
    while (id.valid()) {
      components.push_back(id);
      id = get_parent_impl(id);
    }
  }

  template <typename Output, Output::value_type Separator, typename Vector,
            typename GetPathFunc>
  Output build_path_from_components(Vector const& components,
                                    GetPathFunc const& get_path_func) const {
    static constexpr std::size_t kDefaultPathSize = 255;

    Output p;

    // TODO: we might be able to determine a better size hint here by looking
    //       at the components, but this is probably overkill since these
    //       buffers are usually only temporary

    p.reserve(kDefaultPathSize);

    bool first = true;

    for (auto it = components.rbegin(); it != components.rend(); ++it) {
      auto const& name = get_path_func(*it);

      if (!p.empty() && !p.ends_with(Separator)) {
        p.append(1, Separator);
      }

      p.append(name);

      if (first) {
        static constexpr auto kLocalPathSeparator =
            static_cast<Output::value_type>(fs::path::preferred_separator);
        first = false;
        if constexpr (kLocalPathSeparator != Separator) {
          std::replace(p.begin(), p.end(), kLocalPathSeparator, Separator);
        } else {
#ifdef DWARFS_HANDLE_NATIVE_PATHS
          std::replace(p.begin(), p.end(), static_cast<Output::value_type>('/'),
                       Separator);
#endif
        }
      }
    }

    return p;
  }

  void sort_all_directory_entries()
    requires is_mutable
  {
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    if (shared_.native_path_component_count() > 0) {
      shared_.sort_all_dir_entries(
          [this](entry_id const aid, entry_id const bid) {
            return get_path_string_impl(aid) < get_path_string_impl(bid);
          });
      return;
    }
#endif

    // See comment in `entry_less_revpath` about ordering.
    shared_.sort_all_dir_entries(
        [this](entry_id const aid, entry_id const bid) {
          return get_path_storage_index_impl(aid) <
                 get_path_storage_index_impl(bid);
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

  fs::path::string_type get_native_path_string_impl(entry_id const id) const {
    if (id == kRootId) {
      return shared_.get_native_root_path_component();
    }
    return dispatch_shared_(&packed_entry_data::get_native_path_string, id);
  }

  std::string get_path_string_impl(entry_id const id) const {
    if (id == kRootId) {
      return shared_.get_utf8_root_path_component();
    }
    return dispatch_shared_(&packed_entry_data::get_path_string, id);
  }

  std::size_t get_path_storage_index_impl(entry_id const id) const {
    return dispatch_(&packed_entry_data::get_path_storage_index, id);
  }

  path_name_storage_tuple get_path_storage_impl(entry_id const id) const {
    return dispatch_(&packed_entry_data::get_path_storage, id);
  }

  void set_path_storage_impl(entry_id const id,
                             path_name_storage_tuple const value) {
    dispatch_(&packed_entry_data::set_path_storage, id, value);
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

  using dir_entry_lookup_table = phmap::flat_hash_map<std::string, entry_id>;
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

namespace {

using reorder_map_type =
    dwarfs::container::packed_int_vector<std::optional<std::size_t>>;

// At this point, after compaction and sorting, the prefix of `map` contains
// the sorted order:
//
//   map[order] = old_index
//
// for `order` in [0, used).
//
// What we need for updating entries is the inverse mapping:
//
//   map[old_index] = new_order
//
// We build that inverse mapping in-place, without allocating a second map.
//
// The problem is that the source permutation lives in `map[0..used)`, and
// some old indices may also be in that same range. So writing
//
//   map[old_index] = order
//
// can overwrite another still-unprocessed source value:
//
//   map[old_index] = some_other_old_index
//
// To handle this, we walk the permutation as a set of chains/cycles. Whenever
// the destination slot `old_index` is inside the source prefix and still
// contains an unprocessed source value, we first save that source value, then
// overwrite the slot with the final inverse mapping, and continue from the
// saved value.
//
// Final inverse values are tagged as:
//
//   count + order
//
// This lets us distinguish them from unprocessed source values, which are
// always less than `count`.
//
// Therefore, during inversion:
//
//   value < count     => unprocessed source old_index
//   value >= count    => finalized inverse mapping, i.e. count + order
//   nullopt           => no mapping / already cleared chain marker
//
// After this phase:
//
//   map[old_index] = count + order   for used components
//   map[old_index] = nullopt         for unused components

void invert_map_in_place(reorder_map_type& map, std::size_t const used,
                         std::size_t const count) {
  auto is_source_value = [count](std::optional<std::size_t> const& value) {
    return value.has_value() && *value < count;
  };

  auto tag = [count](std::size_t const order) { return count + order; };

  for (std::size_t i = 0; i < used; ++i) {
    // If this slot still contains an unprocessed source value, then it is
    // part of the order -> old_index mapping. If it already contains a tagged
    // value or nullopt, it has been handled by an earlier chain walk.
    auto const value = map[i].load();

    if (!is_source_value(value)) {
      continue;
    }

    // We are currently processing:
    //
    //   map[order] = old_index
    //
    // and want to write:
    //
    //   map[old_index] = tag(order)
    auto order = i;
    auto old_index = *value;

    // Clear the starting slot. This acts as a marker that breaks cycles.
    // Example cycle:
    //
    //   map[0] = 1
    //   map[1] = 0
    //
    // Clearing map[0] lets the walk terminate when it comes back to 0.
    map[i] = std::nullopt;

    for (;;) {
      if (old_index < used) {
        // `old_index` is also inside the source prefix. It may still contain
        // another source mapping:
        //
        //   map[old_index] = next_old_index
        //
        // If so, save it before overwriting this slot with its final inverse
        // mapping.
        auto const next = map[old_index].load();

        if (is_source_value(next)) {
          map[old_index] = tag(order);

          // Continue with the source mapping we just displaced:
          //
          //   map[old_index] used to mean:
          //     order = old_index
          //     old_index = *next
          order = old_index;
          old_index = *next;
          continue;
        }
      }

      // Either `old_index` is outside the source prefix, or the slot inside
      // the prefix no longer contains an unprocessed source value. In both
      // cases, we can safely write the final inverse mapping and terminate
      // this chain.
      map[old_index] = tag(order);
      break;
    }
  }
}

// After map inversion and updating all entries, the map still contains:
//
//   map[old_index] = count + new_index
//
// This is exactly the information needed to reorder the storage vector in
// place:
//
//   component currently at old_index should move to new_index
//
// The first `used` elements of the storage vector should become the sorted
// path components. The tail [used, count) contains orphaned components and
// does not need to preserve any meaningful order.
//
// The algorithm below repeatedly swaps the component at `pos` into its final
// target position. The mapping travels with the component: `map[position]`
// describes where the component currently at `position` still needs to go.
//
// When a component is swapped into its final target position, we clear the
// mapping at that target. A cleared mapping means: the component currently
// here is already in its final place, or this is an unused/orphaned slot.
//
// This consumes the map while reordering; after this phase, the map no longer
// contains useful information.

void reorder_by_map(reorder_map_type& map,
                    std::size_t const used [[maybe_unused]],
                    std::size_t const count, auto const& swap_component) {
  auto untag = [count](std::size_t value) {
    assert(value >= count);
    return value - count;
  };

  for (std::size_t pos = 0; pos < count; ++pos) {
    while (true) {
      auto value = map[pos].load();

      // If there is no mapping at `pos`, either this position is already
      // settled or it is an unused/orphaned tail slot.
      if (!value.has_value()) {
        break;
      }

      // The component currently at `pos` belongs at `target`.
      auto const target = untag(*value);

      assert(target < used);

      if (target == pos) {
        // The component at `pos` is already in the correct sorted
        // position. Clear the map entry to mark this position as settled.
        map[pos] = std::nullopt;
        break;
      }

      // We are about to move the component at `pos` to `target`, but
      // `target` currently contains some other component. Save that
      // component's mapping before the swap, because after the swap that
      // displaced component will be at `pos`.
      auto displaced_mapping = map[target].load();

      swap_component(pos, target);

      // The component moved from `pos` to `target`, and `target` is
      // exactly where it belongs, so `target` is now settled.
      map[target] = std::nullopt;

      // The component displaced from `target` is now at `pos`. Its old
      // mapping must move with it so the next loop iteration can place
      // it.
      //
      // If `displaced_mapping` is nullopt, then the displaced component
      // is an unused/orphaned component, and the loop will terminate on
      // the next iteration.
      map[pos] = displaced_mapping;
    }
  }
}

} // namespace

template <bool Frozen>
template <typename StatusFunc>
void entry_storage_<Frozen>::sort_path_storage(logger& lgr,
                                               StatusFunc const& set_status)
  requires is_mutable
{
  LOG_PROXY(debug_logger_policy, lgr);
  std::optional<std::decay_t<decltype(LOG_TIMED_VERBOSE)>> timer;

  auto const utf8_count = shared_.utf8_path_component_count();
  reorder_map_type utf8_map(reorder_map_type::required_bits(2 * utf8_count),
                            utf8_count);
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  auto const native_count = shared_.native_path_component_count();
  reorder_map_type native_map(reorder_map_type::required_bits(2 * native_count),
                              native_count);
#endif

  //---------------------------------------------------------------------------
  // mark used path components
  //---------------------------------------------------------------------------

  set_status("marking used path components");
  timer = LOG_TIMED_VERBOSE;

  walk_entries([&](entry_id const id) {
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    auto const [index, type] = get_path_storage_impl(id);
    if (type == path_name_storage::utf8) {
      utf8_map[index] = 0;
    } else {
      native_map[index] = 0;
    }
#else
    auto const index = get_path_storage_index_impl(id);
    utf8_map[index] = 0;
#endif
  });

  *timer << "marked used path components";
  timer.reset();

  //---------------------------------------------------------------------------
  // compact used indices
  //---------------------------------------------------------------------------

  set_status("compacting path indices");
  timer = LOG_TIMED_VERBOSE;

  auto compact_map = [&](reorder_map_type& map, std::size_t const count) {
    std::size_t used = 0;

    for (std::size_t i = 0; i < count; ++i) {
      if (map[i].has_value()) {
        map[used++] = i;
      }
    }

    std::fill(map.begin() + used, map.end(), std::nullopt);

    return used;
  };

  auto const utf8_used = compact_map(utf8_map, utf8_count);
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  auto const native_used = compact_map(native_map, native_count);
#endif

  *timer << "compacted " << utf8_count << " -> " << utf8_used
         << " UTF-8"
#ifdef DWARFS_HANDLE_NATIVE_PATHS
            " and "
         << native_count << " -> " << native_used
         << " native"
#endif
            " path components";
  timer.reset();

  //---------------------------------------------------------------------------
  // sort used indices
  //---------------------------------------------------------------------------

  set_status("sorting path indices");
  timer = LOG_TIMED_VERBOSE;

  auto sort_prefix = [](reorder_map_type& map, std::size_t const used,
                        auto const& get_component) {
    std::ranges::sort(
        map.begin(), map.begin() + used,
        [&](std::optional<std::size_t> a, std::optional<std::size_t> b) {
          assert(a.has_value() && b.has_value());
          return get_component(*a) < get_component(*b);
        });
  };

  sort_prefix(utf8_map, utf8_used, [&](std::size_t index) {
    return shared_.get_utf8_path_component_ref(index);
  });

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  sort_prefix(native_map, native_used, [&](std::size_t index) {
    return shared_.get_native_path_component_ref(index);
  });
#endif

  *timer << "sorted path indices";
  timer.reset();

  //---------------------------------------------------------------------------
  // invert the maps in-place
  //---------------------------------------------------------------------------

  set_status("inverting path index maps");
  timer = LOG_TIMED_VERBOSE;

  invert_map_in_place(utf8_map, utf8_used, utf8_count);
#ifdef DWARFS_HANDLE_NATIVE_PATHS
  invert_map_in_place(native_map, native_used, native_count);
#endif

  *timer << "inverted path index maps";
  timer.reset();

  //---------------------------------------------------------------------------
  // update entries with new indices
  //---------------------------------------------------------------------------

  set_status("updating entry path component indices");
  timer = LOG_TIMED_VERBOSE;

  // The map is still keyed by old indices here. Entries must be updated before
  // the path component vectors are physically reordered.

  auto translated_index = [](reorder_map_type const& map,
                             std::size_t const old_index,
                             std::size_t const count) {
    auto const value = map[old_index];
    assert(value.has_value());
    assert(*value >= count);
    return *value - count;
  };

  walk_entries([&](entry_id const id) {
#ifdef DWARFS_HANDLE_NATIVE_PATHS
    auto [index, type] = get_path_storage_impl(id);

    if (type == path_name_storage::utf8) {
      index = translated_index(utf8_map, index, utf8_count);
    } else {
      index = translated_index(native_map, index, native_count);
    }

    set_path_storage_impl(id, {index, type});
#else
    auto index = get_path_storage_index_impl(id);
    index = translated_index(utf8_map, index, utf8_count);
    set_path_storage_impl(id, {index, path_name_storage::utf8});
#endif
  });

  *timer << "updated entry path component indices";
  timer.reset();

  //---------------------------------------------------------------------------
  // re-order the path storage vectors
  //---------------------------------------------------------------------------

  set_status("sorting path storage vectors");
  timer = LOG_TIMED_VERBOSE;

  reorder_by_map(utf8_map, utf8_used, utf8_count,
                 [&](std::size_t a, std::size_t b) {
                   shared_.swap_utf8_path_components(a, b);
                 });

  shared_.set_utf8_path_component_count(utf8_used);

#ifdef DWARFS_HANDLE_NATIVE_PATHS
  reorder_by_map(native_map, native_used, native_count,
                 [&](std::size_t a, std::size_t b) {
                   shared_.swap_native_path_components(a, b);
                 });

  shared_.set_native_path_component_count(native_used);
#endif

  *timer << "sorted path storage vectors";
}

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

  std::string get_link_target(link_id id) const override {
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

  std::string get_name(entry_id const id) const override {
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

  std::vector<std::string> get_sorted_path_components() const override {
    return impl_.lock()->get_sorted_path_components();
  }

  std::size_t get_path_component_index(entry_id id) const override {
    return impl_.lock()->get_path_component_index(id);
  }

  bool has_bulk_compressed_path_components() const override {
    return impl_.lock()->has_bulk_compressed_path_components();
  }

  dwarfs::internal::fsst_encoder::bulk_compression_result
  steal_bulk_compressed_path_components() override {
    return impl_.lock()->steal_bulk_compressed_path_components();
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

  void sort_file_id_vector(file_id_vector& fv) const override {
    impl_.lock()->sort_file_id_vector(fv);
  }

  void drop_file_hashes() override { impl_.lock()->drop_file_hashes(); }

  std::unique_ptr<entry_impl> freeze(logger& lgr, progress& prog) override {
    return impl_.lock()->freeze(lgr, prog);
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

void entry_storage::freeze_entries(logger& lgr, progress& prog) noexcept {
  entry_impl_ = entry_impl_->freeze(lgr, prog);
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

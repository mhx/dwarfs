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

#include <dwarfs/container/chunked_append_only_vector.h>
#include <dwarfs/container/compact_packed_int_vector.h>
#include <dwarfs/container/map_utils.h>
#include <dwarfs/container/packed_value_traits_optional.h>
#include <dwarfs/container/pinned_byte_span_store.h>
#include <dwarfs/container/segmented_packed_int_vector.h>
#include <dwarfs/dense_value_index.h>
#include <dwarfs/error.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/synchronized.h>
#include <dwarfs/writer/internal/detail/inode_impl.h>
#include <dwarfs/writer/internal/entry_id_vector.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/progress.h>

// TODO: disable everywhere but Windows
#define DWARFS_KEEP_FS_PATHS 1

namespace fs = std::filesystem;

namespace dwarfs::writer::internal {

namespace {

// TODO: consider using just a single mutex for *all* storage?
//       then we could de-dupe things like names/paths across
//       all different entry types
//
// It still makes sense to separate files/dirs/links/devices/other
// since they will populate different slots of the data, and leave
// others unpopulated. Not differentiating them would make storage
// less efficient because e.g. *only* files and symlinks would
// populate size; only files would populate allocated_size. Only
// devices would populate device numbers, etc. If we use *one* data
// structure to rule them all, but just keep some of the fields
// unused for some entry types, this would make things *much*
// simpler, since we never allocate memory for the unused fields.
//
// TODO: trace how often and where we're calling each accessor,
//       and whether it's read or write. => build this as a compile
//       time feature into entry_storage.
//
// TODO: rethink if we still need visitors - it might make more sense
//       to just have some `for_each_device` etc. methods and those
//       might play better with the storage idea (but then again, they
//       may not, because we don't know upfront which fields are going
//       to be accessed) - still, the visitors seem a bit overkill

template <dwarfs::container::packed_vector_value T,
          std::size_t SegmentSize = 4096>
using segtor = dwarfs::container::segmented_packed_int_vector<T, SegmentSize>;

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
    return fs::path(name_);
#endif
  }

  std::string_view name() const { return name_; }

  std::size_t size_in_bytes() const {
#if DWARFS_KEEP_FS_PATHS
    return sizeof(path_component) +
           path_.native().size() * sizeof(fs::path::value_type) + name_.size();
#else
    return sizeof(path_component) + name_.size();
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
                           return acc + de.size_in_bytes();
                         }) +
         sizeof(vec[0]) * vec.size();
}

struct shared_entry_data {
  void drop_indices() {
    path_index_.reset();
    device_index_.reset();
    mode_index_.reset();
    uid_index_.reset();
    gid_index_.reset();
    link_index_.reset();
  }

  void drop_lookup_tables() { dir_entry_lookup_.clear(); }

  auto add_path_component(fs::path const& component, bool is_root) {
    return path_index_->add(component, is_root);
  }

  auto add_device(file_stat::dev_type dev) { return device_index_->add(dev); }

  auto add_mode(file_stat::mode_type mode) { return mode_index_->add(mode); }

  auto add_uid(file_stat::uid_type uid) { return uid_index_->add(uid); }

  auto add_gid(file_stat::gid_type gid) { return gid_index_->add(gid); }

  auto add_link(std::string link) { return link_index_->add(std::move(link)); }

  void dump(std::ostream& os) const {
    auto const total_path_bytes =
        std::accumulate(path_components_.begin(), path_components_.end(), 0ULL,
                        [](std::size_t acc, path_component const& pc) {
                          return acc + pc.size_in_bytes();
                        });
    auto const total_link_bytes =
        std::accumulate(links_.begin(), links_.end(), 0ULL,
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
    os << "  links: " << links_.size() << " ("
       << size_with_unit(total_link_bytes) << ")\n";
    os << "  dir entries: " << dir_entries_.size() << " ("
       << size_with_unit(total_cao_id_vec_bytes(dir_entries_)) << ")\n";
  }

  // TODO; remove those trailing underscores?

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

  cao_vector<std::string> links_;
  std::optional<flat_cao_index<std::string>> link_index_{links_};

  // indexed by dir index, contains all entry ids of the directory
  cao_vector<entry_id_vector> dir_entries_;

  using dir_entry_lookup_table =
      phmap::flat_hash_map<std::string_view, entry_id>;
  phmap::flat_hash_map<uint64_t,
                       dir_entry_lookup_table> mutable dir_entry_lookup_;
};

struct packed_entry_data {
  packed_entry_data(entry_type t)
      : type{t} {}

  entry_type type;

  segtor<size_t> path_name_index;
  segtor<std::optional<uint64_t>> parent_dir_index;
  segtor<std::optional<size_t>> entry_index;
  segtor<std::optional<std::uint64_t>> inode_num;

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
  segtor<stat_common_tuple> stat_common;
  segtor<file_stat::off_type> entry_size;
  phmap::flat_hash_map<uint64_t, file_stat::off_type>
      entry_allocated_size_lookup;

  // file-specific
  segtor<size_t> file_order_index;
  segtor<inode_id> file_inode_id;
  segtor<std::optional<size_t>> file_data_index; // indexes into `file_data_vec`
  std::optional<dwarfs::container::pinned_byte_span_store<512>> file_hashes;

  static constexpr std::size_t kFileHashIndexField{0};
  static constexpr std::size_t kHardlinkCountMinusOneField{1};
  static constexpr std::size_t kInodeNumberField{2};
  using file_data_tuple =
      std::tuple<std::optional<std::uint64_t>, std::uint64_t,
                 std::optional<std::uint64_t>>;
  segtor<file_data_tuple> file_data_vec;
  cao_vector<std::atomic<bool>> file_invalid_vec;

  // link-specific
  segtor<std::optional<size_t>> link_target_index;

  // device-specific:
  segtor<file_stat::dev_type> represented_device;

  bool empty() const { return path_name_index.empty(); }

  std::size_t add_entry_common(shared_entry_data& shared, entry_type type,
                               fs::path const& path, file_stat const& st,
                               entry_id const parent) {
    bool const is_root = !parent.valid();
    assert(is_root || parent.is_dir());
    auto const path_ix = shared.add_path_component(path, is_root);
    auto const entry_ix = path_name_index.size();
    path_name_index.push_back(path_ix);
    parent_dir_index.push_back(is_root ? std::nullopt
                                       : std::make_optional(parent.index()));
    entry_index.push_back(std::nullopt);
    if (type != entry_type::E_FILE) {
      inode_num.push_back(std::nullopt);
    }

    st.ensure_valid(
        file_stat::nlink_valid | file_stat::mode_valid | file_stat::uid_valid |
        file_stat::gid_valid | file_stat::atime_valid | file_stat::mtime_valid |
        file_stat::ctime_valid | file_stat::dev_valid | file_stat::ino_valid |
        file_stat::size_valid | file_stat::allocated_size_valid);

    auto const nlink = st.nlink_unchecked();
    assert(nlink > 0);

    stat_common_tuple tmp{};
    std::get<kNlinkMinusOneField>(tmp) = nlink - 1;
    std::get<kModeIndexField>(tmp) = shared.add_mode(st.mode_unchecked());
    std::get<kUidIndexField>(tmp) = shared.add_uid(st.uid_unchecked());
    std::get<kGidIndexField>(tmp) = shared.add_gid(st.gid_unchecked());
    std::get<kAccessTimeSecondField>(tmp) = st.atime_unchecked();
    std::get<kAccessTimeSubsecondField>(tmp) = st.atime_nsec_unchecked();
    std::get<kModificationTimeSecondField>(tmp) = st.mtime_unchecked();
    std::get<kModificationTimeSubsecondField>(tmp) = st.mtime_nsec_unchecked();
    std::get<kStatusChangeTimeSecondField>(tmp) = st.ctime_unchecked();
    std::get<kStatusChangeTimeSubsecondField>(tmp) = st.ctime_nsec_unchecked();
    std::get<kInodeField>(tmp) = st.ino_unchecked();
    std::get<kDeviceIndexField>(tmp) = shared.add_device(st.dev_unchecked());

    stat_common.push_back(tmp);

    auto const size = st.size_unchecked();
    auto const allocated_size = st.allocated_size_unchecked();
    auto const index = entry_size.size();
    entry_size.push_back(size);
    if (size != allocated_size) {
      entry_allocated_size_lookup.emplace(index, st.allocated_size_unchecked());
    }

    if (!is_root) {
      assert(parent.index() < shared.dir_entries_.size());
      shared.dir_entries_.at(parent.index()).push_back({type, entry_ix});

      if (auto const it = shared.dir_entry_lookup_.find(parent.index());
          it != shared.dir_entry_lookup_.end()) {
        auto& lookup = it->second;
        auto inserted = lookup
                            .emplace(shared.path_components_.at(path_ix).name(),
                                     entry_id{type, entry_ix})
                            .second;
        if (!inserted) {
          DWARFS_PANIC("duplicate entry name in directory");
        }
      }
    }

    return entry_ix;
  }

  void add_file_specific() {
    assert(type == entry_type::E_FILE);
    file_data_index.push_back(std::nullopt);
    file_inode_id.push_back(inode_id{});
    file_order_index.push_back(0);
  }

  void add_link_specific() {
    assert(type == entry_type::E_LINK);
    link_target_index.push_back(std::nullopt);
  }

  void add_device_specific(file_stat const& st) {
    assert(type == entry_type::E_DEVICE);
    represented_device.push_back(st.rdev_unchecked());
  }

  entry_id get_parent(uint64_t const index) const {
    entry_id rv;
    if (auto const id = parent_dir_index.at(index)) {
      rv = {entry_type::E_DIR, *id};
    }
    return rv;
  }

  fs::path
  get_path(shared_entry_data const& shared, uint64_t const index) const {
    auto const path_ix = path_name_index.at(index);
    return shared.path_components_.at(path_ix).path();
  }

  std::string_view
  get_path_string(shared_entry_data const& shared, uint64_t const index) const {
    auto const path_ix = path_name_index.at(index);
    return shared.path_components_.at(path_ix).name();
  }

  void update_global_entry_data(shared_entry_data const& shared,
                                uint64_t const index,
                                global_entry_data& data) const {
    auto const& stat = stat_common.at(index);

    data.add_mode(shared.modes_.at(get<kModeIndexField>(stat)));
    data.add_uid(shared.uids_.at(get<kUidIndexField>(stat)));
    data.add_gid(shared.gids_.at(get<kGidIndexField>(stat)));
    data.add_atime(get<kAccessTimeSecondField>(stat));
    data.add_mtime(get<kModificationTimeSecondField>(stat));
    data.add_ctime(get<kStatusChangeTimeSecondField>(stat));
  }

  void pack_entry(shared_entry_data const& shared, uint64_t const index,
                  thrift::metadata::inode_data& entry_v2,
                  global_entry_data const& data,
                  time_resolution_converter const& timeres) const {
    auto const& stat = stat_common.at(index);
    file_stat out{};
    out.set_mode(shared.modes_.at(get<kModeIndexField>(stat)));
    out.set_uid(shared.uids_.at(get<kUidIndexField>(stat)));
    out.set_gid(shared.gids_.at(get<kGidIndexField>(stat)));
    out.set_atimespec(get<kAccessTimeSecondField>(stat),
                      get<kAccessTimeSubsecondField>(stat));
    out.set_mtimespec(get<kModificationTimeSecondField>(stat),
                      get<kModificationTimeSubsecondField>(stat));
    out.set_ctimespec(get<kStatusChangeTimeSecondField>(stat),
                      get<kStatusChangeTimeSubsecondField>(stat));
    data.pack_inode_stat(entry_v2, out, timeres);
  }

  unique_inode_id get_unique_inode_id(shared_entry_data const& shared,
                                      uint64_t const index) const {
    auto const& stat = stat_common.at(index);
    return unique_inode_id{shared.devices_.at(get<kDeviceIndexField>(stat)),
                           get<kInodeField>(stat)};
  }

  file_stat::nlink_type get_nlink(uint64_t const index) const {
    auto const& stat = stat_common.at(index);
    return get<kNlinkMinusOneField>(stat) + 1;
  }

  void create_hardlink(file_id target, file_id source, progress& prog) {
    assert(type == entry_type::E_FILE);
    auto target_fdi = file_data_index.at(target.index());
    auto const& source_fdi = file_data_index.at(source.index());
    assert(!target_fdi.has_value());
    assert(source_fdi.has_value());
    auto const size = entry_size.at(source.index());
    auto const allocated_size =
        container::get_optional(entry_allocated_size_lookup, source.index())
            .value_or(size);

    prog.hardlink_size += size;
    prog.allocated_hardlink_size += allocated_size;
    ++prog.hardlinks;

    auto const fdi = source_fdi.value();
    target_fdi = fdi;
    ++get<kHardlinkCountMinusOneField>(file_data_vec.at(fdi));
  }

  size_t get_file_data_index(std::uint64_t const index) const {
    assert(type == entry_type::E_FILE);
    auto fdi = file_data_index.at(index);
    DWARFS_CHECK(fdi.has_value(), "file data unset");
    return *fdi;
  }

  std::size_t hardlink_count(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return get<kHardlinkCountMinusOneField>(file_data_vec.at(fdi)) + 1;
  }

  void set_file_invalid(file_id id) {
    auto const fdi = get_file_data_index(id.index());
    file_invalid_vec.at(fdi).store(true);
  }

  bool is_file_invalid(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return file_invalid_vec.at(fdi).load();
  }

  void set_entry_index(std::uint64_t const index, std::size_t const ix) {
    auto ei = entry_index.at(index);
    DWARFS_CHECK(!ei.has_value(), "attempt to set entry index more than once");
    ei = ix;
  }

  std::optional<std::size_t> get_entry_index(std::uint64_t const index) const {
    return entry_index.at(index);
  }

  void set_file_order_index(file_id id, std::size_t index) {
    assert(type == entry_type::E_FILE);
    file_order_index.at(id.index()) = index;
  }

  std::size_t get_file_order_index(file_id id) const {
    assert(type == entry_type::E_FILE);
    return file_order_index.at(id.index());
  }

  void set_file_hash_index(file_id id, std::size_t index) {
    auto const fdi = get_file_data_index(id.index());
    auto hash_index = get<kFileHashIndexField>(file_data_vec.at(fdi));
    DWARFS_CHECK(!hash_index.has_value(),
                 "attempt to set file hash index more than once");
    hash_index = index;
  }

  std::optional<std::size_t> get_file_hash_index(file_id id) const {
    auto const fdi = get_file_data_index(id.index());
    return get<kFileHashIndexField>(file_data_vec.at(fdi));
  }

  file_size_t get_size(uint64_t const index) const {
    return entry_size.at(index);
  }

  file_size_t get_allocated_size(uint64_t const index) const {
    return container::get_optional(entry_allocated_size_lookup, index)
        .value_or(get_size(index));
  }

  void set_empty(uint64_t const index) {
    entry_size.at(index) = 0;
    entry_allocated_size_lookup.erase(index);
  }

  void set_inode_num(uint64_t const index, uint64_t ino) {
    if (type == entry_type::E_FILE) {
      auto const fdi = get_file_data_index(index);
      auto file_inode = get<kInodeNumberField>(file_data_vec.at(fdi));
      DWARFS_CHECK(!file_inode.has_value(),
                   "attempt to set inode number more than once");
      file_inode = ino;
    } else {
      DWARFS_CHECK(!inode_num.at(index).has_value(),
                   "attempt to set inode number more than once");
      inode_num.at(index) = ino;
    }
  }

  std::optional<uint64_t> get_inode_num(uint64_t const index) const {
    if (type == entry_type::E_FILE) {
      auto const fdi = get_file_data_index(index);
      return get<kInodeNumberField>(file_data_vec.at(fdi));
    }
    return inode_num.at(index);
  }

  void set_inode_id(file_id fid, inode_id iid) {
    assert(type == entry_type::E_FILE);
    auto inode = file_inode_id.at(fid.index());
    DWARFS_CHECK(!inode.load().valid(), "inode already set for file");
    inode = iid;
  }

  inode_id get_inode_id(file_id fid) const {
    assert(type == entry_type::E_FILE);
    return file_inode_id.at(fid.index());
  }

  void set_link_target_index(link_id lid, size_t index) {
    assert(type == entry_type::E_LINK);
    auto link_target = link_target_index.at(lid.index());
    DWARFS_CHECK(!link_target.has_value(),
                 "attempt to set link target index more than once");
    link_target = index;
  }

  std::size_t get_link_target_index(link_id lid) const {
    assert(type == entry_type::E_LINK);
    auto const link_target = link_target_index.at(lid.index());
    DWARFS_CHECK(link_target.has_value(), "link target index not set");
    return *link_target;
  }

  void dump(std::ostream& os, std::string_view name) const {
    if (path_name_index.empty()) {
      os << "no " << name << " entries\n";
      return;
    }

    auto const path_name_index_bytes = path_name_index.size_in_bytes();
    auto const parent_dir_index_bytes = parent_dir_index.size_in_bytes();
    auto const entry_index_bytes = entry_index.size_in_bytes();
    auto const file_order_index_bytes = file_order_index.size_in_bytes();
    auto const inode_num_bytes = inode_num.size_in_bytes();
    auto const file_data_index_bytes = file_data_index.size_in_bytes();
    auto const file_inode_id_bytes = file_inode_id.size_in_bytes();
    auto const link_target_index_bytes = link_target_index.size_in_bytes();
    auto const stat_common_bytes = stat_common.size_in_bytes();
    auto const entry_size_bytes = entry_size.size_in_bytes();
    auto const entry_allocated_size_lookup_bytes =
        entry_allocated_size_lookup.capacity() *
        sizeof(decltype(entry_allocated_size_lookup)::value_type);
    auto const file_hashes_bytes =
        file_hashes ? file_hashes->size_in_bytes() : 0;
    auto const total_bytes =
        path_name_index_bytes + parent_dir_index_bytes + entry_index_bytes +
        file_order_index_bytes + inode_num_bytes + file_data_index_bytes +
        file_inode_id_bytes + link_target_index_bytes + stat_common_bytes +
        entry_size_bytes + entry_allocated_size_lookup_bytes +
        file_hashes_bytes;

    os << path_name_index.size() << " " << name << " entries ("
       << size_with_unit(total_bytes) << "):\n";
    os << "  path name index: " << size_with_unit(path_name_index_bytes)
       << "\n";
    os << "  parent dir index: " << size_with_unit(parent_dir_index_bytes)
       << "\n";
    os << "  entry index: " << size_with_unit(entry_index_bytes) << "\n";
    os << "  inode number: " << size_with_unit(inode_num_bytes) << "\n";
    os << "  file order index: " << size_with_unit(file_order_index_bytes)
       << "\n";
    os << "  file data index: " << size_with_unit(file_data_index_bytes)
       << "\n";
    os << "  file inode id: " << size_with_unit(file_inode_id_bytes) << "\n";
    os << "  stat common: " << size_with_unit(stat_common_bytes) << "\n";
    os << "  size: " << size_with_unit(entry_size.size_in_bytes()) << "\n";
    os << "  allocated size lookup: "
       << size_with_unit(entry_allocated_size_lookup_bytes) << "\n";

    if (!link_target_index.empty()) {
      os << "  link target index: " << size_with_unit(link_target_index_bytes)
         << "\n";
    }

    if (file_hashes) {
      os << "  file hashes: " << size_with_unit(file_hashes_bytes) << "\n";
    }
  }
};

[[noreturn]] void frozen_panic() { DWARFS_PANIC("entry_storage is frozen"); }

} // namespace

template <bool Frozen>
class entry_storage_ final : public entry_storage::impl {
 public:
  static constexpr bool is_mutable = !Frozen;

  friend class entry_storage_<true>;

  entry_storage_()
    requires is_mutable
  = default;

  entry_storage_(entry_storage_<false>& other) noexcept
    requires Frozen
      : inodes_{std::move(other.inodes_)}
      , files_for_inode_{std::move(other.files_for_inode_)}
      , shared_{std::move(other.shared_)}
      , packed_files_{std::move(other.packed_files_)}
      , packed_dirs_{std::move(other.packed_dirs_)}
      , packed_links_{std::move(other.packed_links_)}
      , packed_devices_{std::move(other.packed_devices_)}
      , packed_others_{std::move(other.packed_others_)} {}

  std::unique_ptr<impl> freeze() override {
    if constexpr (is_mutable) {
      sort_all_directory_entries();
      shared_.drop_indices();
      shared_.drop_lookup_tables();
      return std::make_unique<entry_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  entry_id
  make_obj_(entry_type const type, packed_entry_data& data,
            fs::path const& path, file_stat const& st, entry_id const parent) {
    if constexpr (is_mutable) {
      auto const ix = data.add_entry_common(shared_, type, path, st, parent);
      switch (type) {
      case entry_type::E_FILE:
        data.add_file_specific();
        break;
      case entry_type::E_DIR:
        shared_.dir_entries_.emplace_back();
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
      return {type, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_file(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return make_obj_(entry_type::E_FILE, packed_files_, path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    entry_id const parent) override {
    return make_obj_(entry_type::E_DIR, packed_dirs_, path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return make_obj_(entry_type::E_LINK, packed_links_, path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       entry_id const parent) override {
    return make_obj_(entry_type::E_DEVICE, packed_devices_, path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      entry_id const parent) override {
    return make_obj_(entry_type::E_OTHER, packed_others_, path, st, parent);
  }

  inode_id make_inode() override {
    if constexpr (is_mutable) {
      auto id = inodes_.size();
      inodes_.emplace_back();
      files_for_inode_.emplace_back();
      return inode_id{id};
    } else {
      frozen_panic();
    }
  }

  bool empty() const noexcept override { return packed_dirs_.empty(); }

  void dump(std::ostream& os) const override;

  void create_packed_file_data(file_id id) override {
    if constexpr (is_mutable) {
      auto index = packed_files_.file_data_vec.size();
      packed_files_.file_data_vec.push_back({std::nullopt, 0, std::nullopt});
      packed_files_.file_data_index.at(id.index()) = index;
      packed_files_.file_invalid_vec.emplace_back(false);
    } else {
      frozen_panic();
    }
  }

  std::size_t inode_count() const override { return inodes_.size(); }

  inode* get_inode(inode_id const id) override {
    return &inodes_.at(id.index());
  }

  void set_entry_index(entry_id id, std::size_t index) override {
    // this is safe even on frozen storage if it's single-threaded
    dispatch_(&packed_entry_data::set_entry_index, id, index);
  }

  std::optional<std::size_t> get_entry_index(entry_id id) const override {
    return dispatch_(&packed_entry_data::get_entry_index, id);
  }

  void set_file_order_index(file_id id, std::size_t index) override {
    // this is safe even on frozen storage if it's single-threaded
    packed_files_.set_file_order_index(id, index);
  }

  std::size_t get_file_order_index(file_id id) const override {
    return packed_files_.get_file_order_index(id);
  }

  void set_link_target(link_id id, std::string link_target,
                       progress& prog) override {
    if constexpr (is_mutable) {
      auto const index = shared_.add_link(std::move(link_target));
      packed_links_.set_link_target_index(id, index);
      auto const size = packed_links_.get_size(id.index());
      auto const allocated_size = packed_links_.get_allocated_size(id.index());
      prog.original_size += size;
      prog.allocated_original_size += allocated_size;
      prog.symlink_size += size;
    } else {
      frozen_panic();
    }
  }

  std::string_view get_link_target(link_id id) const override {
    return shared_.links_.at(packed_links_.get_link_target_index(id));
  }

  file_id_vector const& get_files_for_inode(inode_id id) const override {
    return files_for_inode_.at(id.index());
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) override {
    // this is safe even on frozen storage if it's single-threaded
    auto& vec = files_for_inode_.at(id.index());
    DWARFS_CHECK(vec.empty(), "files already set for inode");
    vec = std::move(fv);
  }

  void set_file_inode(file_id id, inode_id ino) override {
    // this is safe even on frozen storage if it's single-threaded
    packed_files_.set_inode_id(id, ino);
  }

  inode_id get_file_inode(file_id id) const override {
    return packed_files_.get_inode_id(id);
  }

  entry_id get_parent(entry_id const id) const override {
    return get_parent_impl(id);
  }

  fs::path get_path(entry_id const id) const override {
    fs::path p = get_path_impl(id);

    if (auto const parent = get_parent_impl(id)) {
      p = get_path(parent) / p;
    }

    return p;
  }

  std::string get_unix_dpath(entry_id const id) const override {
    std::string p{get_path_string_impl(id)};

    if (is_root_path(p)) {
      p = "/";
    } else {
      if (id.is_dir() && !p.empty() && !p.ends_with(kLocalPathSeparator)) {
        p += '/';
      }

      if (auto const parent = get_parent_impl(id)) {
        p = get_unix_dpath(parent) + p;
      } else if constexpr (kLocalPathSeparator != '/') {
        std::ranges::replace(p, kLocalPathSeparator, '/');
      }
    }

    return p;
  }

  std::string_view get_name(entry_id const id) const override {
    return get_path_string_impl(id);
  }

  bool is_dir_empty(entry_id const id) const override {
    assert(id.is_dir());
    return shared_.dir_entries_.at(id.index()).empty();
  }

  void remove_empty_dirs(progress& prog) override {
    if constexpr (is_mutable) {
      remove_empty_dirs_impl(prog, 0);
    } else {
      frozen_panic();
    }
  }

  void
  for_each_entry_in_dir(entry_id id,
                        std::function<void(entry_id)> const& f) const override {
    assert(id.is_dir());
    for (auto const eid : shared_.dir_entries_.at(id.index())) {
      f(eid);
    }
  }

  static constexpr std::size_t kMinDirEntriesForLookupTable = 16;

  entry_id find_in_dir(entry_id id, std::string_view name) const override {
    assert(id.is_dir());

    auto const& de = shared_.dir_entries_.at(id.index());

    if constexpr (is_mutable) {
      if (de.size() < kMinDirEntriesForLookupTable) {
        auto const it = std::ranges::find_if(
            de, [&](entry_id const id) { return get_name(id) == name; });

        if (it != de.end()) {
          return *it;
        }
      } else {
        auto [lit, created] = shared_.dir_entry_lookup_.try_emplace(id.index());

        if (created) {
          for (auto const eid : de) {
            if (!lit->second.emplace(get_name(eid), eid).second) {
              DWARFS_PANIC("duplicate entry name in directory");
            }
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

  void update_global_entry_data(entry_id id,
                                global_entry_data& data) const override {
    update_global_entry_data_impl(id, data);
  }

  void pack_entry(entry_id id, thrift::metadata::inode_data& entry_v2,
                  global_entry_data const& data,
                  time_resolution_converter const& timeres) const override {
    pack_entry_impl(id, entry_v2, data, timeres);
  }

  unique_inode_id get_unique_inode_id(entry_id id) const override {
    return get_unique_inode_id_impl(id);
  }

  file_stat::nlink_type get_nlink(entry_id id) const override {
    return get_nlink_impl(id);
  }

  void
  create_hardlink(file_id target, file_id source, progress& prog) override {
    if constexpr (is_mutable) {
      packed_files_.create_hardlink(target, source, prog);
    } else {
      frozen_panic();
    }
  }

  std::size_t hardlink_count(file_id id) const override {
    return packed_files_.hardlink_count(id);
  }

  void set_file_invalid(file_id id) override {
    packed_files_.set_file_invalid(id);
  }

  bool is_file_invalid(file_id id) const override {
    return packed_files_.is_file_invalid(id);
  }

  std::span<std::byte>
  get_file_hash_buffer(file_id id, std::size_t buffer_size) override {
    if constexpr (is_mutable) {
      if (!packed_files_.file_hashes.has_value()) {
        packed_files_.file_hashes.emplace(buffer_size);
      } else if (packed_files_.file_hashes->span_size() != buffer_size) {
        DWARFS_PANIC(
            fmt::format("hash buffer size mismatch: expected {}, got {}",
                        packed_files_.file_hashes->span_size(), buffer_size));
      }
      auto& hashes = *packed_files_.file_hashes;
      auto const index = hashes.size();
      packed_files_.set_file_hash_index(id, index);
      return hashes.emplace_back();
    } else {
      frozen_panic();
    }
  }

  std::string_view get_file_hash(file_id id) const override {
    if (packed_files_.file_hashes.has_value()) {
      auto const& hashes = *packed_files_.file_hashes;
      auto const index = packed_files_.get_file_hash_index(id);

      if (index.has_value()) {
        auto const span = hashes.at(*index);
        return {reinterpret_cast<char const*>(span.data()), span.size()};
      }
    }
    return {};
  }

  file_size_t get_entry_size(entry_id id) const override {
    return dispatch_(&packed_entry_data::get_size, id);
  }

  file_size_t get_entry_allocated_size(entry_id id) const override {
    return dispatch_(&packed_entry_data::get_allocated_size, id);
  }

  void set_entry_empty(entry_id id) override {
    if constexpr (is_mutable) {
      dispatch_(&packed_entry_data::set_empty, id);
    } else {
      frozen_panic();
    }
  }

  void set_inode_num_for_entry(entry_id id, std::uint64_t ino) override {
    dispatch_(&packed_entry_data::set_inode_num, id, ino);
  }

  std::optional<std::uint64_t>
  get_inode_num_for_entry(entry_id id) const override {
    return dispatch_(&packed_entry_data::get_inode_num, id);
  }

  file_stat::dev_type get_represented_device(device_id id) const override {
    return packed_devices_.represented_device.at(id.index());
  }

 private:
  void sort_all_directory_entries()
    requires is_mutable
  {
    for (auto& de : shared_.dir_entries_) {
      std::ranges::sort(de, [this](entry_id const aid, entry_id const bid) {
        return get_name(aid) < get_name(bid);
      });
    }
  }

  void remove_empty_dirs_impl(progress& prog, uint64_t dir_index)
    requires is_mutable
  {
    auto& de = shared_.dir_entries_.at(dir_index);

    auto last = std::remove_if(de.begin(), de.end(), [&](entry_id const id) {
      if (id.is_dir()) {
        remove_empty_dirs_impl(prog, id.index());
        return shared_.dir_entries_.at(id.index()).empty();
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
      return (me.packed_files_.*method)(id.index(),
                                        std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (me.packed_dirs_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (me.packed_links_.*method)(id.index(),
                                        std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (me.packed_devices_.*method)(id.index(),
                                          std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (me.packed_others_.*method)(id.index(),
                                         std::forward<Args>(args)...);
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
      return (me.packed_files_.*method)(me.shared_, id.index(),
                                        std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (me.packed_dirs_.*method)(me.shared_, id.index(),
                                       std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (me.packed_links_.*method)(me.shared_, id.index(),
                                        std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (me.packed_devices_.*method)(me.shared_, id.index(),
                                          std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (me.packed_others_.*method)(me.shared_, id.index(),
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

  entry_id get_parent_impl(entry_id const id) const {
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

  void
  pack_entry_impl(entry_id const id, thrift::metadata::inode_data& entry_v2,
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

  cao_vector<detail::inode_impl> inodes_;
  cao_vector<file_id_vector> files_for_inode_;

  shared_entry_data shared_;
  packed_entry_data packed_files_{entry_type::E_FILE};
  packed_entry_data packed_dirs_{entry_type::E_DIR};
  packed_entry_data packed_links_{entry_type::E_LINK};
  packed_entry_data packed_devices_{entry_type::E_DEVICE};
  packed_entry_data packed_others_{entry_type::E_OTHER};
};

template <bool Frozen>
void entry_storage_<Frozen>::dump(std::ostream& os) const {
  os << "num inodes: " << inodes_.size() << "\n";
  os << "  hardlinks: "
     << size_with_unit(total_cao_id_vec_bytes(files_for_inode_)) << "\n";

  shared_.dump(os);
  packed_files_.dump(os, "files");
  packed_dirs_.dump(os, "dirs");
  packed_links_.dump(os, "links");
  packed_devices_.dump(os, "devices");
  packed_others_.dump(os, "others");
}

class synchronized_entry_storage_ final : public entry_storage::impl {
 public:
  entry_id make_file(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return impl_.lock()->make_file(path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    entry_id const parent) override {
    return impl_.lock()->make_dir(path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return impl_.lock()->make_link(path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       entry_id const parent) override {
    return impl_.lock()->make_device(path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      entry_id const parent) override {
    return impl_.lock()->make_other(path, st, parent);
  }

  inode_id make_inode() override { return impl_.lock()->make_inode(); }

  void create_packed_file_data(file_id id) override {
    impl_.lock()->create_packed_file_data(id);
  }

  std::size_t inode_count() const override {
    return impl_.lock()->inode_count();
  }

  inode* get_inode(inode_id const id) override {
    return impl_.lock()->get_inode(id);
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

  file_id_vector const& get_files_for_inode(inode_id id) const override {
    return impl_.lock()->get_files_for_inode(id);
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) override {
    impl_.lock()->set_files_for_inode(id, std::move(fv));
  }

  void set_file_inode(file_id id, inode_id ino) override {
    impl_.lock()->set_file_inode(id, ino);
  }

  inode_id get_file_inode(file_id id) const override {
    return impl_.lock()->get_file_inode(id);
  }

  entry_id get_parent(entry_id const id) const override {
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

  bool is_dir_empty(entry_id const id) const override {
    return impl_.lock()->is_dir_empty(id);
  }

  void remove_empty_dirs(progress& prog) override {
    impl_.lock()->remove_empty_dirs(prog);
  }

  void
  for_each_entry_in_dir(entry_id,
                        std::function<void(entry_id)> const&) const override {
    DWARFS_PANIC("synchronized for_each_entry_in_dir is not supported");
  }

  entry_id find_in_dir(entry_id id, std::string_view name) const override {
    return impl_.lock()->find_in_dir(id, name);
  }

  void update_global_entry_data(entry_id id,
                                global_entry_data& data) const override {
    impl_.lock()->update_global_entry_data(id, data);
  }

  void pack_entry(entry_id id, thrift::metadata::inode_data& entry_v2,
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
  create_hardlink(file_id source, file_id target, progress& prog) override {
    impl_.lock()->create_hardlink(source, target, prog);
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

  file_size_t get_entry_allocated_size(entry_id id) const override {
    return impl_.lock()->get_entry_allocated_size(id);
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

  std::unique_ptr<impl> freeze() override { return impl_.lock()->freeze(); }

 private:
  dwarfs::internal::synchronized<entry_storage_<false>> impl_;
};

entry_storage::entry_storage()
    : impl_(std::make_unique<synchronized_entry_storage_>()) {}

entry_storage::~entry_storage() = default;
entry_storage::entry_storage(entry_storage&&) noexcept = default;
entry_storage& entry_storage::operator=(entry_storage&&) noexcept = default;

std::string entry_storage::dump() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

void entry_storage::freeze() noexcept { impl_ = impl_->freeze(); }

dir_handle
entry_storage::create_root_dir(fs::path const& path, file_stat const& st) {
  DWARFS_CHECK(empty(), "entry_storage root already set");
  return {*this, impl_->make_dir(path, st, entry_id())};
}

file_handle
entry_storage::create_file(fs::path const& path, entry_handle parent,
                           file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_file(path, st, parent.id())};
}

dir_handle entry_storage::create_dir(fs::path const& path, entry_handle parent,
                                     file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_dir(path, st, parent.id())};
}

link_handle
entry_storage::create_link(fs::path const& path, entry_handle parent,
                           file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_link(path, st, parent.id())};
}

device_handle
entry_storage::create_device(fs::path const& path, entry_handle parent,
                             file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_device(path, st, parent.id())};
}

other_handle
entry_storage::create_other(fs::path const& path, entry_handle parent,
                            file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_other(path, st, parent.id())};
}

inode_handle entry_storage::create_inode() {
  return {*this, impl_->make_inode()};
}

} // namespace dwarfs::writer::internal

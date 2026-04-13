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
#include <dwarfs/container/packed_value_traits_optional.h>
#include <dwarfs/container/segmented_packed_int_vector.h>
#include <dwarfs/dense_value_index.h>
#include <dwarfs/error.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/synchronized.h>
#include <dwarfs/writer/internal/detail/inode_impl.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/entry_storage.h>
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

template <dwarfs::container::integer_packable T, std::size_t SegmentSize = 4096>
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
using compact_auto_vec = dwarfs::container::compact_auto_packed_int_vector<T>;

struct shared_entry_data {
  void drop_indices() { path_index_.reset(); }
  void drop_lookup_tables() { dir_entry_lookup_.clear(); }

  auto add_path_component(fs::path const& component, bool is_root) {
    return path_index_->add(component, is_root);
  }

  void dump(std::ostream& os) const {
    auto const total_path_bytes =
        std::accumulate(path_components_.begin(), path_components_.end(), 0ULL,
                        [](std::size_t acc, path_component const& pc) {
                          return acc + pc.size_in_bytes();
                        });

    auto const total_dir_entry_bytes =
        std::accumulate(dir_entries_.begin(), dir_entries_.end(), 0ULL,
                        [](std::size_t acc, auto const& de) {
                          return acc + de.size_in_bytes();
                        }) +
        sizeof(dir_entries_[0]) * dir_entries_.size();

    os << "shared entry data:\n";
    os << "  path components: " << path_components_.size() << " ("
       << size_with_unit(total_path_bytes) << ")\n";
    os << "  dir entries: " << dir_entries_.size() << " ("
       << size_with_unit(total_dir_entry_bytes) << ")\n";
  }

  // TODO; remove those trailing underscores?

  cao_vector<path_component> path_components_;
  std::optional<flat_cao_index<path_component>> path_index_{path_components_};

  // indexed by dir index, contains all entry ids of the directory
  cao_vector<compact_auto_vec<entry_id>> dir_entries_;

  using dir_entry_lookup_table =
      phmap::flat_hash_map<std::string_view, entry_id>;
  phmap::flat_hash_map<uint64_t,
                       dir_entry_lookup_table> mutable dir_entry_lookup_;
};

struct packed_entry_data {
  segtor<size_t> path_name_index;
  segtor<std::optional<uint64_t>> parent_dir_index;
  segtor<size_t> entry_index;
  // TODO: stat

  // file-specific
  segtor<size_t> order_index;
  segtor<size_t>
      inode_index; // TODO change this in `entry` first to make sure it works
  segtor<size_t> file_data_index; // indexes into `file_data`

  // dir-specific
  // TODO: these are more interesting, especially the lookup table, `optional`
  // and the
  //       `entries` vector

  // link-specific
  segtor<size_t> link_target_index; // indexes into de-duped link target vector
  // TODO: optional inode again

  // device-specific
  // TODO: again, optional inode - this is required for *all* types, except for
  // files
  //       which have this info in `file_data` already.

  void add_entry_common(shared_entry_data& shared, entry_type type,
                        fs::path const& path, file_stat const&,
                        entry_id const parent) {
    bool const is_root = !parent.valid();
    assert(is_root || parent.is_dir());
    auto const path_ix = shared.add_path_component(path, is_root);
    auto const entry_ix = path_name_index.size();
    path_name_index.push_back(path_ix);
    parent_dir_index.push_back(is_root ? std::nullopt
                                       : std::make_optional(parent.index()));

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

  void dump(std::ostream& os, std::string_view name) const {
    auto const path_name_index_bytes = path_name_index.size_in_bytes();
    auto const parent_dir_index_bytes = parent_dir_index.size_in_bytes();
    auto const entry_index_bytes = entry_index.size_in_bytes();
    auto const order_index_bytes = order_index.size_in_bytes();
    auto const inode_index_bytes = inode_index.size_in_bytes();
    auto const file_data_index_bytes = file_data_index.size_in_bytes();
    auto const link_target_index_bytes = link_target_index.size_in_bytes();
    auto const total_bytes = path_name_index_bytes + parent_dir_index_bytes +
                             entry_index_bytes + order_index_bytes +
                             inode_index_bytes + file_data_index_bytes +
                             link_target_index_bytes;

    os << path_name_index.size() << " " << name << " entries ("
       << size_with_unit(total_bytes) << "):\n";
    os << "  path name index: " << size_with_unit(path_name_index_bytes)
       << "\n";
    os << "  parent dir index: " << size_with_unit(parent_dir_index_bytes)
       << "\n";
    os << "  entry index: " << size_with_unit(entry_index_bytes) << "\n";
    os << "  order index: " << size_with_unit(order_index_bytes) << "\n";
    os << "  inode index: " << size_with_unit(inode_index_bytes) << "\n";
    os << "  file data index: " << size_with_unit(file_data_index_bytes)
       << "\n";
    os << "  link target index: " << size_with_unit(link_target_index_bytes)
       << "\n";
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
      : files_{std::move(other.files_)}
      , dirs_{std::move(other.dirs_)}
      , links_{std::move(other.links_)}
      , devices_{std::move(other.devices_)}
      , others_{std::move(other.others_)}
      , file_data_{std::move(other.file_data_)}
      , inodes_{std::move(other.inodes_)}
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

  template <typename T>
  static entry_id
  make_obj_(cao_vector<T>& vec, entry_type type, file_stat const& st) {
    if constexpr (is_mutable) {
      auto ix = vec.size();
      vec.emplace_back(st);
      return {type, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_file(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    packed_files_.add_entry_common(shared_, entry_type::E_FILE, path, st,
                                   parent);
    return make_obj_(files_, entry_type::E_FILE, st);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    entry_id const parent) override {
    packed_dirs_.add_entry_common(shared_, entry_type::E_DIR, path, st, parent);
    shared_.dir_entries_.emplace_back();
    return make_obj_(dirs_, entry_type::E_DIR, st);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    packed_links_.add_entry_common(shared_, entry_type::E_LINK, path, st,
                                   parent);
    return make_obj_(links_, entry_type::E_LINK, st);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       entry_id const parent) override {
    packed_devices_.add_entry_common(shared_, entry_type::E_DEVICE, path, st,
                                     parent);
    return make_obj_(devices_, entry_type::E_DEVICE, st);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      entry_id const parent) override {
    packed_others_.add_entry_common(shared_, entry_type::E_OTHER, path, st,
                                    parent);
    return make_obj_(others_, entry_type::E_OTHER, st);
  }

  inode_ptr make_inode() override {
    if constexpr (is_mutable) {
      auto id [[maybe_unused]] = inodes_.size(); // TODO
      inodes_.emplace_back();
      return &inodes_.back(); // TODO
    } else {
      frozen_panic();
    }
  }

  bool empty() const noexcept override { return dirs_.empty(); }

  void dump(std::ostream& os) const override;

  size_t create_file_data() override {
    if constexpr (is_mutable) {
      auto id = file_data_.size();
      file_data_.emplace_back();
      return id;
    } else {
      frozen_panic();
    }
  }

  [[nodiscard]] file_data& get_file_data(size_t const id) override {
    return file_data_.at(id);
  }

  entry* get_entry(entry_id const id) override {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return &files_.at(id.index());
    case entry_type::E_DIR:
      return &dirs_.at(id.index());
    case entry_type::E_LINK:
      return &links_.at(id.index());
    case entry_type::E_DEVICE:
      return &devices_.at(id.index());
    case entry_type::E_OTHER:
      return &others_.at(id.index());
    default:
      throw std::runtime_error("invalid entry type");
    }
  }

  std::size_t inode_count() const override { return inodes_.size(); }

  inode* get_inode(std::uint64_t const index) override {
    return &inodes_.at(index);
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

  template <typename Method, typename... Args>
  decltype(auto)
  dispatch_(Method method, entry_id const id, Args&&... args) const {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return (packed_files_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (packed_dirs_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (packed_links_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (packed_devices_.*method)(id.index(), std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (packed_others_.*method)(id.index(), std::forward<Args>(args)...);
    default:
      DWARFS_PANIC("invalid entry type");
    }
  }

  template <typename Method, typename... Args>
  decltype(auto)
  dispatch_shared_(Method method, entry_id const id, Args&&... args) const {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return (packed_files_.*method)(shared_, id.index(),
                                     std::forward<Args>(args)...);
    case entry_type::E_DIR:
      return (packed_dirs_.*method)(shared_, id.index(),
                                    std::forward<Args>(args)...);
    case entry_type::E_LINK:
      return (packed_links_.*method)(shared_, id.index(),
                                     std::forward<Args>(args)...);
    case entry_type::E_DEVICE:
      return (packed_devices_.*method)(shared_, id.index(),
                                       std::forward<Args>(args)...);
    case entry_type::E_OTHER:
      return (packed_others_.*method)(shared_, id.index(),
                                      std::forward<Args>(args)...);
    default:
      DWARFS_PANIC("invalid entry type");
    }
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

  cao_vector<file> files_;
  cao_vector<dir> dirs_;
  cao_vector<link> links_;
  cao_vector<device> devices_;
  cao_vector<other> others_;
  cao_vector<file_data> file_data_;
  cao_vector<detail::inode_impl> inodes_;

  shared_entry_data shared_;
  packed_entry_data packed_files_;
  packed_entry_data packed_dirs_;
  packed_entry_data packed_links_;
  packed_entry_data packed_devices_;
  packed_entry_data packed_others_;
};

template <bool Frozen>
void entry_storage_<Frozen>::dump(std::ostream& os) const {
  os << "num dirs: " << dirs_.size() << "\n";
  os << "num files: " << files_.size() << "\n";
  os << "num file data: " << file_data_.size() << "\n";
  os << "num links: " << links_.size() << "\n";
  os << "num devices: " << devices_.size() << "\n";
  os << "num others: " << others_.size() << "\n";
  os << "num inodes: " << inodes_.size() << "\n";

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

  inode_ptr make_inode() override { return impl_.lock()->make_inode(); }

  size_t create_file_data() override {
    return impl_.lock()->create_file_data();
  }

  file_data& get_file_data(size_t const id) override {
    return impl_.lock()->get_file_data(id);
  }

  entry* get_entry(entry_id const id) override {
    return impl_.lock()->get_entry(id);
  }

  std::size_t inode_count() const override {
    return impl_.lock()->inode_count();
  }

  inode* get_inode(std::uint64_t const id) override {
    return impl_.lock()->get_inode(id);
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

inode_ptr entry_storage::create_inode() { return impl_->make_inode(); }

} // namespace dwarfs::writer::internal

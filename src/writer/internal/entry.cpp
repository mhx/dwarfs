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

#include <algorithm>
#include <cstring>
#include <ostream>
#include <utility>

#include <fmt/format.h>

#include <dwarfs/checksum.h>
#include <dwarfs/error.h>
#include <dwarfs/file_type.h>
#include <dwarfs/file_view.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/entry_storage.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/inode.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/scanner_progress.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view const kHashContext{"[hashing] "};

} // namespace

entry::entry(fs::path const& path, file_stat const& st, entry_id parent_id)
#ifdef _WIN32
    : path_{parent_id ? path.filename() : path}
    , name_{path_to_utf8_string_sanitized(path_)}
#else
    : name_{path_to_utf8_string_sanitized(parent_id ? path.filename() : path)}
#endif
    , parent_id_{parent_id}
    , stat_{st} {
}

bool entry::has_parent() const { return parent_id_.valid(); }

fs::path entry::name_as_path() const {
#ifdef _WIN32
  return path_;
#else
  return name_;
#endif
}

std::string_view entry::name() const { return name_; }

void entry::update(global_entry_data& data) const {
  stat_.ensure_valid(file_stat::uid_valid | file_stat::gid_valid |
                     file_stat::mode_valid | file_stat::atime_valid |
                     file_stat::mtime_valid | file_stat::ctime_valid);
  data.add_uid(stat_.uid_unchecked());
  data.add_gid(stat_.gid_unchecked());
  data.add_mode(stat_.mode_unchecked());
  data.add_atime(stat_.atime_unchecked());
  data.add_mtime(stat_.mtime_unchecked());
  data.add_ctime(stat_.ctime_unchecked());
}

void entry::pack(thrift::metadata::inode_data& entry_v2,
                 global_entry_data const& data,
                 time_resolution_converter const& timeres) const {
  data.pack_inode_stat(entry_v2, stat_, timeres);
}

file_size_t entry::size() const { return stat_.size(); }

file_size_t entry::allocated_size() const { return stat_.allocated_size(); }

unique_inode_id entry::inode_id() const {
  return unique_inode_id{stat_.dev(), stat_.ino()};
}

uint64_t entry::num_hard_links() const { return stat_.nlink(); }

void entry::set_empty() {
  stat_.set_size(0);
  stat_.set_allocated_size(0);
}

entry::type_t file::type() const { return entry_type::E_FILE; }

std::string_view file::hash(entry_storage& storage) const {
  auto& h = get_data(storage).hash;
  return {h.data(), h.size()};
}

void file::set_inode(inode_ptr ino) {
  DWARFS_CHECK(!inode_, "inode already set for file");
  inode_ = ino;
}

inode_ptr file::get_inode() const { return inode_; }

void file::scan(entry_storage& /*storage*/, entry_id /*self_id*/,
                os_access const& /*os*/, progress& /*prog*/) {
  DWARFS_PANIC("file::scan() without hash_alg is not used");
}

void file::scan(entry_storage& storage, entry_id self_id, file_view const& mm,
                progress& prog, std::optional<std::string> const& hash_alg) {
  auto const s = size();

  if (hash_alg) {
    progress::scan_updater supd(prog.hash, s);
    checksum cs(*hash_alg);

    if (s > 0) {
      std::shared_ptr<scanner_progress> pctx;
      auto const chunk_size = prog.hash.chunk_size.load();

      if (std::cmp_greater_equal(s, 4 * chunk_size)) {
        pctx = prog.create_context<scanner_progress>(
            termcolor::MAGENTA, kHashContext,
            const_file_handle{storage, self_id}.path_as_string(), s);
      }

      assert(mm);

      for (auto const& ext : mm.extents()) {
        // TODO; See if we need to handle hole extents differently.
        //       I guess not, since we can just make holes generate
        //       zeroes efficiently in the file_view abstraction.
        for (auto const& seg : ext.segments(chunk_size)) {
          auto data = seg.span();
          cs.update(data);
          if (pctx) {
            pctx->advance(data.size());
          }
        }
      }
    }

    auto& data = get_data(storage);

    data.hash.resize(cs.digest_size());

    DWARFS_CHECK(cs.finalize(data.hash.data()), "checksum computation failed");
  }
}

uint32_t file::unique_file_id() const { return inode_->num(); }

void file::set_inode_num(entry_storage& storage, uint32_t inode_num) {
  auto& data = get_data(storage);
  DWARFS_CHECK(!data.inode_num, "attempt to set inode number more than once");
  data.inode_num = inode_num;
}

std::optional<uint32_t> const& file::inode_num(entry_storage& storage) const {
  auto const& data = get_data(storage);
  return data.inode_num;
}

void file::create_data(entry_storage& storage) {
  assert(data_index_ == file::kInvalidDataIndex);
  data_index_ = storage.create_file_data();
}

file_data& file::get_data(entry_storage& storage) const {
  DWARFS_CHECK(data_index_ != file::kInvalidDataIndex, "file data unset");
  return storage.get_file_data(data_index_);
}

void file::hardlink(entry_storage& storage, file* other, progress& prog) {
  assert(data_index_ == kInvalidDataIndex);
  assert(other->data_index_ != kInvalidDataIndex);

  prog.hardlink_size += size();
  prog.allocated_hardlink_size += allocated_size();
  ++prog.hardlinks;

  data_index_ = other->data_index_;

  auto& data = get_data(storage);
  ++data.hardlink_count;
}

entry::type_t dir::type() const { return entry_type::E_DIR; }

void dir::add(entry_handle e) {
  if (lookup_) {
    auto r [[maybe_unused]] = lookup_->emplace(e.name(), e.id());
    assert(r.second);
  }
  entries_.emplace_back(e.id());
}

void dir::for_each_child(std::function<void(entry_id)> const& f) const {
  for (auto const e : entries_) {
    f(e);
  }
}

void dir::sort(entry_storage& storage) {
  std::ranges::sort(entries_, [&](entry_id a, entry_id b) {
    return entry_handle(storage, a).name() < entry_handle(storage, b).name();
  });
}

void dir::scan(entry_storage&, entry_id, os_access const&, progress&) {}

void dir::pack_entry(entry_storage& storage, thrift::metadata::metadata& mv2,
                     global_entry_data const& data,
                     time_resolution_converter const& timeres) const {
  auto& de = mv2.dir_entries()->emplace_back();
  de.name_index() = has_parent() ? data.get_name_index(name()) : 0;
  de.inode_num() = DWARFS_NOTHROW(inode_num(storage).value());
  entry::pack(DWARFS_NOTHROW(mv2.inodes()->at(de.inode_num().value())), data,
              timeres);
}

void dir::remove_empty_dirs(entry_storage& storage, progress& prog) {
  auto last =
      // NOLINTNEXTLINE(modernize-use-ranges)
      std::remove_if(entries_.begin(), entries_.end(), [&](entry_id eid) {
        auto e = entry_handle(storage, eid);
        if (auto d = e.as_dir()) {
          d.remove_empty_dirs(prog);
          return d.empty();
        }
        return false;
      });

  if (last != entries_.end()) {
    auto num = std::distance(last, entries_.end());
    prog.dirs_scanned -= num;
    prog.dirs_found -= num;
    entries_.erase(last, entries_.end());
  }

  lookup_.reset();
}

entry_id dir::find(entry_storage& storage, fs::path const& path) {
  auto name = path_to_utf8_string_sanitized(path.filename());

  if (!lookup_ && entries_.size() >= kLookupTableSizeThreshold) {
    populate_lookup_table(storage);
  }

  if (lookup_) {
    if (auto it = lookup_->find(name); it != lookup_->end()) {
      return it->second;
    }
  } else {
    auto it = std::ranges::find_if(entries_, [&](auto const eid) {
      return entry_handle(storage, eid).name() == name;
    });
    if (it != entries_.end()) {
      return *it;
    }
  }

  return {};
}

void dir::populate_lookup_table(entry_storage& storage) {
  assert(!lookup_);

  lookup_ = std::make_unique<lookup_table>();
  lookup_->reserve(entries_.size());

  for (auto const eid : entries_) {
    auto e = entry_handle(storage, eid);
    auto r [[maybe_unused]] = lookup_->emplace(e.name(), eid);
    assert(r.second);
  }
}

entry::type_t link::type() const { return entry_type::E_LINK; }

std::string const& link::linkname() const { return link_; }

void link::scan(entry_storage& storage, entry_id self_id, os_access const& os,
                progress& prog) {
  link_ = path_to_utf8_string_sanitized(
      os.read_symlink(link_handle{storage, self_id}.fs_path()));
  prog.original_size += size();
  prog.allocated_original_size += allocated_size();
  prog.symlink_size += size();
}

entry::type_t device::type() const { return entry_type::E_DEVICE; }

void device::scan(entry_storage&, entry_id, os_access const&, progress&) {}

uint64_t device::device_id() const { return status().rdev(); }

entry::type_t other::type() const { return entry_type::E_OTHER; }

void other::scan(entry_storage&, entry_id, os_access const&, progress&) {}

} // namespace dwarfs::writer::internal

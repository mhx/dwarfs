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

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/entry_storage.h>
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

entry::entry(file_stat const& st)
    : stat_{st} {}

void entry::pack(thrift::metadata::inode_data& entry_v2,
                 global_entry_data const& data,
                 time_resolution_converter const& timeres) const {
  data.pack_inode_stat(entry_v2, stat_, timeres);
}

file_size_t entry::size() const { return stat_.size(); }

file_size_t entry::allocated_size() const { return stat_.allocated_size(); }

unique_inode_id entry::get_unique_inode_id() const {
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

void file::set_inode(inode_id ino) {
  DWARFS_CHECK(!inode_, "inode already set for file");
  inode_ = ino;
}

inode_id file::get_inode() const { return inode_; }

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

void dir::scan(entry_storage&, entry_id, os_access const&, progress&) {}

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

uint64_t device::device_id() const { return stat_.rdev(); }

entry::type_t other::type() const { return entry_type::E_OTHER; }

void other::scan(entry_storage&, entry_id, os_access const&, progress&) {}

} // namespace dwarfs::writer::internal

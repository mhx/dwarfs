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

entry::entry(file_stat const& st)
    : stat_{st} {}

file_size_t entry::size() const { return stat_.size(); }

file_size_t entry::allocated_size() const { return stat_.allocated_size(); }

void entry::set_empty() {
  stat_.set_size(0);
  stat_.set_allocated_size(0);
}

entry::type_t file::type() const { return entry_type::E_FILE; }

void file::set_inode(inode_id ino) {
  DWARFS_CHECK(!inode_, "inode already set for file");
  inode_ = ino;
}

inode_id file::get_inode() const { return inode_; }

void file::scan(entry_storage& /*storage*/, entry_id /*self_id*/,
                os_access const& /*os*/, progress& /*prog*/) {
  DWARFS_PANIC("file::scan() without hash_alg is not used");
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

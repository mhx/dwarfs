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

#include <algorithm>
#include <cassert>
#include <numeric>
#include <queue>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/reader/metadata_types.h>
#include <dwarfs/util.h>

#include <dwarfs/reader/internal/metadata_types.h>

namespace dwarfs::reader {

inode_view::mode_type inode_view::mode() const { return iv_->mode(); }

std::string inode_view::mode_string() const { return iv_->mode_string(); }

std::string inode_view::perm_string() const { return iv_->perm_string(); }

posix_file_type::value inode_view::type() const { return iv_->type(); }

bool inode_view::is_regular_file() const {
  return iv_->type() == posix_file_type::regular;
}

bool inode_view::is_directory() const {
  return iv_->type() == posix_file_type::directory;
}

bool inode_view::is_symlink() const {
  return iv_->type() == posix_file_type::symlink;
}

inode_view::uid_type inode_view::getuid() const { return iv_->getuid(); }

inode_view::gid_type inode_view::getgid() const { return iv_->getgid(); }

uint32_t inode_view::inode_num() const { return iv_->inode_num(); }

std::string dir_entry_view::name() const { return impl_->name(); }

inode_view dir_entry_view::inode() const {
  return inode_view{impl_->inode_shared()};
}

bool dir_entry_view::is_root() const { return impl_->is_root(); }

std::optional<dir_entry_view> dir_entry_view::parent() const {
  if (auto p = impl_->parent()) {
    return dir_entry_view{std::move(p)};
  }
  return std::nullopt;
}

std::string dir_entry_view::path() const { return impl_->path(); }

std::string dir_entry_view::unix_path() const { return impl_->unix_path(); }

std::filesystem::path dir_entry_view::fs_path() const {
  return impl_->fs_path();
}

std::wstring dir_entry_view::wpath() const { return impl_->wpath(); }

uint32_t directory_view::first_entry(uint32_t ino) const {
  return g_->first_dir_entry(ino);
}

uint32_t directory_view::parent_entry(uint32_t ino) const {
  return g_->parent_dir_entry(ino);
}

uint32_t directory_view::entry_count() const {
  return first_entry(inode_ + 1) - first_entry();
}

boost::integer_range<uint32_t> directory_view::entry_range() const {
  return boost::irange(first_entry(), first_entry(inode_ + 1));
}

uint32_t directory_view::parent_inode() const {
  if (inode_ == 0) {
    return 0;
  }

  auto ent = parent_entry(inode_);

  if (auto e = g_->meta().dir_entries()) {
    ent = (*e)[ent].inode_num();
  }

  return ent;
}

} // namespace dwarfs::reader

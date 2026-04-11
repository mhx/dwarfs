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
#include <cassert>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/entry_storage.h>

#include <dwarfs/writer/internal/provisional_entry.h>

namespace dwarfs::writer::internal {

provisional_entry::provisional_entry(os_access const& os,
                                     std::filesystem::path const& path,
                                     std::optional<entry_handle> parent)
    : path_{path}
    , stat_{os.symlink_info(path)}
    , parent_{parent} {
  if (!parent_ && stat_.type() != posix_file_type::directory) {
    DWARFS_THROW(
        runtime_error,
        fmt::format("root entry '{}' must be a directory", path.string()));
  }
}

entry_type provisional_entry::type() const {
  switch (stat_.type()) {
  case posix_file_type::regular:
    return entry_type::E_FILE;

  case posix_file_type::directory:
    return entry_type::E_DIR;

  case posix_file_type::symlink:
    return entry_type::E_LINK;

  case posix_file_type::character:
  case posix_file_type::block:
    return entry_type::E_DEVICE;

  case posix_file_type::fifo:
  case posix_file_type::socket:
    return entry_type::E_OTHER;

  default:
    DWARFS_PANIC(fmt::format("unknown file type for '{}'", path_.string()));
  }
}

std::string provisional_entry::name() const {
  return path_to_utf8_string_sanitized(parent_ ? path_.filename() : path_);
}

bool provisional_entry::is_directory() const {
  return type() == entry_type::E_DIR;
}

std::string provisional_entry::unix_dpath() const {
  static constexpr char kLocalPathSeparator{
      static_cast<char>(std::filesystem::path::preferred_separator)};

  auto path = path_to_utf8_string_sanitized(path_);

  if (kLocalPathSeparator != '/') {
    std::ranges::replace(path, kLocalPathSeparator, '/');
  }

  if (!path.empty() && is_directory() && path.back() != '/') {
    path += '/';
  }

  return path;
}

entry_handle provisional_entry::commit(entry_storage& tree) {
  switch (stat_.type()) {
  case posix_file_type::regular:
    return tree.create_file(path_, parent_.value(), stat_);

  case posix_file_type::directory:
    if (parent_) {
      return tree.create_dir(path_, *parent_, stat_);
    }
    return tree.create_root_dir(path_, stat_);

  case posix_file_type::symlink:
    return tree.create_link(path_, parent_.value(), stat_);

  case posix_file_type::character:
  case posix_file_type::block:
    return tree.create_device(path_, parent_.value(), stat_);

  case posix_file_type::fifo:
  case posix_file_type::socket:
    return tree.create_other(path_, parent_.value(), stat_);

  default:
    DWARFS_PANIC(fmt::format("unknown file type for '{}'", path_.string()));
  }
}

} // namespace dwarfs::writer::internal

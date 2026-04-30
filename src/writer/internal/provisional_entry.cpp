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
#include <dwarfs/util.h>

#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/provisional_entry.h>

namespace dwarfs::writer::internal {

namespace {

dir_entry
make_dir_entry(os_access const& os, std::filesystem::path const& path) {
  dir_entry entry;
  entry.name = path;
  entry.stat_hint = os.symlink_info(path);
  entry.type = entry.stat_hint->type();
  return entry;
}

} // namespace

provisional_entry::provisional_entry(os_access const& os,
                                     std::filesystem::path const& path,
                                     std::optional<dir_handle> parent)
    : provisional_entry(os, {}, make_dir_entry(os, path), parent) {}

provisional_entry::provisional_entry(os_access const& os, dir_descriptor dd,
                                     dir_entry const& entry,
                                     std::optional<dir_handle> parent)
    : dd_{std::move(dd)}
    , entry_{entry}
    , parent_{parent}
    , os_{os} {
  if (!parent_ && entry_.type != posix_file_type::directory) {
    DWARFS_THROW(runtime_error,
                 fmt::format("root entry '{}' must be a directory",
                             path_to_utf8_string_sanitized(entry_.name)));
  }
}

entry_type provisional_entry::type() const {
  switch (entry_.type) {
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
    DWARFS_PANIC(fmt::format("unknown file type for '{}'",
                             path_to_utf8_string_sanitized(entry_.name)));
  }
}

std::string provisional_entry::name() const {
  return path_to_utf8_string_sanitized(parent_ ? entry_.name.filename()
                                               : entry_.name);
}

bool provisional_entry::is_directory() const {
  return entry_.type == posix_file_type::directory;
}

std::string provisional_entry::unix_dpath() const {
  static constexpr char kLocalPathSeparator{
      static_cast<char>(std::filesystem::path::preferred_separator)};

  // TODO: must be adapted once `name` does no longer include the full path
  auto path = path_to_utf8_string_sanitized(entry_.name);

  if (kLocalPathSeparator != '/') {
    std::ranges::replace(path, kLocalPathSeparator, '/');
  }

  if (!path.empty() && is_directory() && path.back() != '/') {
    path += '/';
  }

  return path;
}

entry_handle provisional_entry::commit(entry_storage& tree) {
  auto const& path = entry_.name;
  file_stat stat;

  if (entry_.stat_hint) {
    stat = *entry_.stat_hint;
  } else if (dd_) {
    // TODO: entry_.name should really be a relative path
    stat = dd_.symlink_info(entry_.name.filename());
  } else {
    stat = os_.symlink_info(path);
  }

  switch (entry_.type) {
  case posix_file_type::regular:
    return tree.create_file(path, parent_.value(), stat);

  case posix_file_type::directory:
    if (parent_) {
      return tree.create_dir(path, *parent_, stat);
    }
    return tree.create_root_dir(path, stat);

  case posix_file_type::symlink:
    return tree.create_link(path, parent_.value(), stat);

  case posix_file_type::character:
  case posix_file_type::block:
    return tree.create_device(path, parent_.value(), stat);

  case posix_file_type::fifo:
  case posix_file_type::socket:
    return tree.create_other(path, parent_.value(), stat);

  default:
    DWARFS_PANIC(fmt::format("unknown file type for '{}'", path.string()));
  }
}

} // namespace dwarfs::writer::internal

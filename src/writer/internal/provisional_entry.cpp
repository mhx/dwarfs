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

#include <dwarfs/error.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/entry_storage.h>

#include <dwarfs/writer/internal/provisional_entry.h>

namespace dwarfs::writer::internal {

provisional_entry::provisional_entry(os_access const& os,
                                     std::filesystem::path const& path)
    : is_root_dir_{true} {
  auto st = os.symlink_info(path);

  if (!st.is_directory()) {
    DWARFS_THROW(runtime_error,
                 fmt::format("'{}' must be a directory",
                             path_to_utf8_string_sanitized(path)));
  }

  entry_.emplace<dir>(path, nullptr, st);
}

provisional_entry::provisional_entry(os_access const& os,
                                     std::filesystem::path const& path,
                                     entry_handle parent) {
  auto st = os.symlink_info(path);

  assert(parent);

  switch (st.type()) {
  case posix_file_type::regular:
    entry_.emplace<file>(path, parent.self_, st);
    break;

  case posix_file_type::directory:
    entry_.emplace<dir>(path, parent.self_, st);
    break;

  case posix_file_type::symlink:
    entry_.emplace<link>(path, parent.self_, st);
    break;

  case posix_file_type::character:
  case posix_file_type::block:
  case posix_file_type::fifo:
  case posix_file_type::socket:
    entry_.emplace<device>(path, parent.self_, st);
    break;

  default:
    break;
  }
}

const_entry_handle provisional_entry::handle() const {
  return entry_ |
         match{
             [](std::monostate const&) -> const_entry_handle {
               DWARFS_PANIC("cannot get handle of empty provisional_entry");
             },
             [](file const& f) -> const_entry_handle { return {&f}; },
             [](dir const& d) -> const_entry_handle { return {&d}; },
             [](link const& l) -> const_entry_handle { return {&l}; },
             [](device const& d) -> const_entry_handle { return {&d}; },
         };
}

entry_handle provisional_entry::commit(entry_storage& tree) {
  auto r =
      std::move(entry_) |
      match{
          // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
          [](std::monostate&&) -> entry_handle {
            DWARFS_PANIC("cannot commit empty provisional_entry");
          },
          [&tree](file&& f) -> entry_handle {
            return tree.add_file(std::move(f));
          },
          [this, &tree](dir&& d) -> entry_handle {
            if (is_root_dir_) {
              return tree.add_root_dir(std::move(d));
            }
            return tree.add_dir(std::move(d));
          },
          [&tree](link&& l) -> entry_handle {
            return tree.add_link(std::move(l));
          },
          [&tree](device&& d) -> entry_handle {
            return tree.add_device(std::move(d));
          },
      };

  entry_.emplace<std::monostate>();

  return r;
}

} // namespace dwarfs::writer::internal

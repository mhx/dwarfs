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

#include <dwarfs/error.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/entry_storage.h>

#include <fmt/format.h>

namespace dwarfs::writer {

namespace internal {

class entry_factory_ : public entry_factory::impl {
 public:
  entry_factory::node create(entry_storage& tree, os_access const& os,
                             std::filesystem::path const& path,
                             entry_factory::node parent) override {
    auto st = os.symlink_info(path);

    if (!parent) {
      if (st.is_directory()) {
        return tree.create_root_dir(path, st);
      }

      DWARFS_THROW(runtime_error,
                   fmt::format("'{}' must be a directory",
                               path_to_utf8_string_sanitized(path)));
    }

    switch (st.type()) {
    case posix_file_type::regular:
      return tree.create_file(path, parent, st);

    case posix_file_type::directory:
      return tree.create_dir(path, parent, st);

    case posix_file_type::symlink:
      return tree.create_link(path, parent, st);

    case posix_file_type::character:
    case posix_file_type::block:
    case posix_file_type::fifo:
    case posix_file_type::socket:
      return tree.create_device(path, parent, st);

    default:
      break;
    }

    return {};
  }
};

} // namespace internal

entry_factory::entry_factory()
    : impl_(std::make_unique<internal::entry_factory_>()) {}

} // namespace dwarfs::writer

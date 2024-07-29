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

#include <dwarfs/entry_factory.h>
#include <dwarfs/os_access.h>

#include <dwarfs/internal/entry.h>

namespace dwarfs {

namespace internal {

class entry_factory_ : public entry_factory::impl {
 public:
  std::shared_ptr<entry>
  create(os_access const& os, std::filesystem::path const& path,
         std::shared_ptr<entry> parent) override {
    // TODO: just use `path` directly (need to fix test helpers, tho)?
    std::filesystem::path p =
        parent ? parent->fs_path() / path.filename() : path;

    auto st = os.symlink_info(p);

    switch (st.type()) {
    case posix_file_type::regular:
      return std::make_shared<file>(path, std::move(parent), st);

    case posix_file_type::directory:
      return std::make_shared<dir>(path, std::move(parent), st);

    case posix_file_type::symlink:
      return std::make_shared<link>(path, std::move(parent), st);

    case posix_file_type::character:
    case posix_file_type::block:
    case posix_file_type::fifo:
    case posix_file_type::socket:
      return std::make_shared<device>(path, std::move(parent), st);

    default:
      // TODO: warn
      break;
    }

    return nullptr;
  }
};

} // namespace internal

entry_factory::entry_factory()
    : impl_(std::make_unique<internal::entry_factory_>()) {}

} // namespace dwarfs

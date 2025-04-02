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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <filesystem>
#include <memory>

namespace dwarfs {

class os_access;

namespace writer {

namespace internal {

class entry;

} // namespace internal

class entry_factory {
 public:
  using node = std::shared_ptr<internal::entry>;

  entry_factory();

  node create(os_access const& os, std::filesystem::path const& path,
              node parent = {}) {
    return impl_->create(os, path, std::move(parent));
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual node create(os_access const& os, std::filesystem::path const& path,
                        node parent) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer

} // namespace dwarfs

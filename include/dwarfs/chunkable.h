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

#pragma once

#include <span>
#include <string_view>

namespace dwarfs {

class file;

class chunkable {
 public:
  virtual ~chunkable() = default;

  virtual file const* get_file() const = 0;
  virtual size_t size() const = 0;
  virtual std::string description() const = 0;
  virtual std::span<uint8_t const> span() const = 0;
  virtual void add_chunk(size_t block, size_t offset, size_t size) = 0;
  virtual void release_until(size_t offset) = 0;
};

} // namespace dwarfs

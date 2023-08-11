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

#include <memory>

#include "dwarfs/chunkable.h"

namespace dwarfs {

class inode;
class mmif;
class os_access;

class inode_chunkable : public chunkable {
 public:
  inode_chunkable(inode& ino, os_access& os);
  ~inode_chunkable();

  size_t size() const override;
  std::string description() const override;
  std::span<uint8_t const> span() const override;
  void add_chunk(size_t block, size_t offset, size_t size) override;
  void release_until(size_t offset) override;

 private:
  inode& ino_;
  std::unique_ptr<mmif> mm_;
};

} // namespace dwarfs

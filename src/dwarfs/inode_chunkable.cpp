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

#include <fmt/format.h>

#include "dwarfs/entry.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_chunkable.h"
#include "dwarfs/mmap.h"
#include "dwarfs/os_access.h"

namespace dwarfs {

inode_chunkable::inode_chunkable(inode& ino, os_access& os)
    : ino_{ino} {
  auto e = ino_.any();
  if (auto size = e->size(); size > 0) {
    mm_ = os.map_file(e->fs_path(), size);
  }
}

inode_chunkable::~inode_chunkable() = default;

size_t inode_chunkable::size() const { return ino_.any()->size(); }

std::string inode_chunkable::description() const {
  return fmt::format("inode {} [{}] - size: {}", ino_.num(), ino_.any()->name(),
                     ino_.any()->size());
}

std::span<uint8_t const> inode_chunkable::span() const { return mm_->span(); }

void inode_chunkable::add_chunk(size_t block, size_t offset, size_t size) {
  ino_.add_chunk(block, offset, size);
}

void inode_chunkable::release_until(size_t offset) {
  mm_->release_until(offset);
}

} // namespace dwarfs

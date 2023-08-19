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

#include "dwarfs/categorizer.h"
#include "dwarfs/entry.h"
#include "dwarfs/fragment_chunkable.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_fragments.h"
#include "dwarfs/mmif.h"

namespace dwarfs {

fragment_chunkable::fragment_chunkable(inode const& ino,
                                       single_inode_fragment& frag,
                                       file_off_t offset, mmif& mm,
                                       categorizer_manager const* catmgr)
    : ino_{ino}
    , frag_{frag}
    , offset_{offset}
    , mm_{mm}
    , catmgr_{catmgr} {}

fragment_chunkable::~fragment_chunkable() = default;

file const* fragment_chunkable::get_file() const { return ino_.any(); }

size_t fragment_chunkable::size() const { return frag_.size(); }

std::string fragment_chunkable::description() const {
  return fmt::format("{}fragment at offset {} of inode {} [{}] - size: {}",
                     category_prefix(catmgr_, frag_.category()), offset_,
                     ino_.num(), ino_.any()->name(), size());
}

std::span<uint8_t const> fragment_chunkable::span() const {
  return mm_.span(offset_, frag_.size());
}

void fragment_chunkable::add_chunk(size_t block, size_t offset, size_t size) {
  frag_.add_chunk(block, offset, size);
}

void fragment_chunkable::release_until(size_t offset) {
  mm_.release_until(offset_ + offset);
}

} // namespace dwarfs

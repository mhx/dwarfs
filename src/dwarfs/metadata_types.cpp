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

#include "dwarfs/metadata_types.h"

#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs {

std::string_view entry_view::name() const {
  return meta_->names()[name_index()];
}

uint16_t entry_view::mode() const { return meta_->modes()[mode_index()]; }

uint16_t entry_view::getuid() const { return meta_->uids()[owner_index()]; }

uint16_t entry_view::getgid() const { return meta_->gids()[group_index()]; }

boost::integer_range<uint32_t> directory_view::entry_range() const {
  auto first = first_entry();
  return boost::irange(first, first + entry_count());
}

uint32_t directory_view::self_inode() {
  auto pos = getPosition().bitOffset;
  if (pos > 0) {
    // XXX: this is evil trickery...
    auto one = meta_->directories()[1].getPosition().bitOffset;
    assert(pos % one == 0);
    pos /= one;
  }
  return pos;
}

} // namespace dwarfs

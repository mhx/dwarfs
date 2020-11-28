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

::apache::thrift::frozen::View<thrift::metadata::directory>
directory_view::getdir() const {
  return getdir(inode());
}

::apache::thrift::frozen::View<thrift::metadata::directory>
directory_view::getdir(uint32_t ino) const {
  return meta_->directories()[ino];
}

uint32_t directory_view::entry_count() const { return getdir().entry_count(); }

boost::integer_range<uint32_t> directory_view::entry_range() const {
  auto d = getdir();
  auto first = d.first_entry();
  return boost::irange(first, first + d.entry_count());
}

uint32_t directory_view::first_entry() const { return getdir().first_entry(); }

uint32_t directory_view::parent_inode() const {
  return getdir().parent_inode();
}

} // namespace dwarfs

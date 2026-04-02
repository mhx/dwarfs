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

#pragma once

#include <compare>
#include <cstdint>
#include <iosfwd>

#include <boost/container_hash/hash.hpp>

#include <fmt/ostream.h>

#include <dwarfs/compiler.h>

namespace dwarfs::writer {

struct unique_inode_id {
  unique_inode_id() = default;
  unique_inode_id(std::uint64_t dev_id, std::uint64_t ino)
      : device_id{dev_id}
      , inode_num{ino} {}

  std::uint64_t device_id{0};
  std::uint64_t inode_num{0};

  DWARFS_PUSH_WARNING
  DWARFS_GCC14_DISABLE_WARNING("-Wnrvo")
  auto operator<=>(unique_inode_id const&) const = default;
  DWARFS_POP_WARNING

  friend std::ostream& operator<<(std::ostream& os, unique_inode_id const& id);
};

} // namespace dwarfs::writer

namespace std {

template <>
struct hash<dwarfs::writer::unique_inode_id> {
  std::size_t operator()(dwarfs::writer::unique_inode_id const& id) const {
    std::size_t seed = 0;
    boost::hash_combine(seed, id.device_id);
    boost::hash_combine(seed, id.inode_num);
    return seed;
  }
};

} // namespace std

namespace fmt {

template <>
struct formatter<dwarfs::writer::unique_inode_id> : ostream_formatter {};

} // namespace fmt

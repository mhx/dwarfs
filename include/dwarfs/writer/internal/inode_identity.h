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
#include <cstddef>
#include <functional>
#include <ostream>
#include <variant>

#include <boost/container_hash/hash.hpp>

#include <fmt/ostream.h>

#include <dwarfs/writer/internal/unique_inode_id.h>

namespace dwarfs::writer::internal {

// A dereferenced directory starts an independent hardlink scope. The mapping
// index is used to distinguish between different scopes.
struct scoped_inode_id {
  std::size_t mapping;
  unique_inode_id inode;

  auto operator<=>(scoped_inode_id const&) const = default;

  friend std::ostream& operator<<(std::ostream& os, scoped_inode_id const& id) {
    return os << id.mapping << ':' << id.inode;
  }
};

using inode_identity = std::variant<
    // an individually dereferenced entry with no source hardlinks
    std::monostate,
    // an ordinary source entry
    unique_inode_id,
    // a descendant of a dereferenced directory
    scoped_inode_id>;

} // namespace dwarfs::writer::internal

namespace std {

template <>
struct hash<dwarfs::writer::internal::scoped_inode_id> {
  std::size_t
  operator()(dwarfs::writer::internal::scoped_inode_id const& id) const {
    std::size_t seed = std::hash<std::size_t>{}(id.mapping);
    boost::hash_combine(
        seed, std::hash<dwarfs::writer::internal::unique_inode_id>{}(id.inode));
    return seed;
  }
};

} // namespace std

namespace fmt {

template <>
struct formatter<dwarfs::writer::internal::scoped_inode_id>
    : ostream_formatter {};

} // namespace fmt

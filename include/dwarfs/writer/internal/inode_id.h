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

#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include <dwarfs/container/packed_value_traits.h>

#include <dwarfs/writer/internal/entry_type.h>

namespace dwarfs::writer::internal {

class inode_id {
 public:
  static constexpr uint64_t kInvalidId{std::numeric_limits<uint64_t>::max()};

  inode_id() = default;
  explicit inode_id(uint64_t index)
      : index_{index} {}

  [[nodiscard]] bool valid() const { return index_ != kInvalidId; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] std::size_t object_hash() const {
    return std::hash<uint64_t>{}(index_);
  }

  [[nodiscard]] uint64_t index() const {
    assert(valid());
    return index_;
  }

  friend bool
  operator==(inode_id const& lhs, inode_id const& rhs) noexcept = default;
  friend std::strong_ordering
  operator<=>(inode_id const& lhs, inode_id const& rhs) noexcept = default;

 private:
  uint64_t index_{kInvalidId};
};

} // namespace dwarfs::writer::internal

template <>
struct std::hash<dwarfs::writer::internal::inode_id> {
  size_t
  operator()(dwarfs::writer::internal::inode_id const& id) const noexcept {
    return id.object_hash();
  }
};

namespace dwarfs::container {

template <>
struct packed_value_traits<dwarfs::writer::internal::inode_id> {
  using value_type = dwarfs::writer::internal::inode_id;
  using encoded_type = uint64_t;

  static encoded_type encode(value_type const& id) {
    return id.valid() ? id.index() + 1 : 0;
  }

  static value_type decode(encoded_type encoded) {
    if (encoded == 0) {
      return {};
    }

    return value_type{encoded - 1};
  }
};

} // namespace dwarfs::container

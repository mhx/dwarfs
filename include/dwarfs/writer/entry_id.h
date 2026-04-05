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

#include <dwarfs/writer/entry_type.h>

namespace dwarfs::writer {

class entry_id {
 public:
  static_assert(std::unsigned_integral<std::underlying_type_t<entry_type>>,
                "entry_type must be an unsigned enum");
  static constexpr uint64_t kInvalidId{std::numeric_limits<uint64_t>::max()};
  static constexpr size_t kEntryTypeBits{4};
  static constexpr uint64_t kEntryTypeShift{64 - kEntryTypeBits};
  static constexpr uint64_t kIndexMask{(1ULL << kEntryTypeShift) - 1};

  entry_id() = default;
  entry_id(entry_type type, uint64_t index)
      : id_{(static_cast<uint64_t>(type) << kEntryTypeShift) |
            (index & kIndexMask)} {
    assert((index & ~kIndexMask) == 0);
    assert(static_cast<uint64_t>(type) <= (1ULL << kEntryTypeBits) - 1);
  }

  [[nodiscard]] bool valid() const { return id_ != kInvalidId; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] std::size_t hash() const { return std::hash<uint64_t>{}(id_); }

  [[nodiscard]] entry_type type() const {
    assert(valid());
    return static_cast<entry_type>(id_ >> kEntryTypeShift);
  }

  [[nodiscard]] uint64_t index() const {
    assert(valid());
    return id_ & kIndexMask;
  }

  friend bool
  operator==(entry_id const& lhs, entry_id const& rhs) noexcept = default;
  friend std::strong_ordering
  operator<=>(entry_id const& lhs, entry_id const& rhs) noexcept = default;

 private:
  uint64_t id_{kInvalidId};
};

} // namespace dwarfs::writer

template <>
struct std::hash<dwarfs::writer::entry_id> {
  size_t operator()(dwarfs::writer::entry_id const& id) const noexcept {
    return id.hash();
  }
};

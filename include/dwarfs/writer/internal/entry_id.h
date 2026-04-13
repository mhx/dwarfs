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

  [[nodiscard]] std::size_t object_hash() const {
    return std::hash<uint64_t>{}(id_);
  }

  [[nodiscard]] entry_type type() const {
    assert(valid());
    return static_cast<entry_type>(id_ >> kEntryTypeShift);
  }

  [[nodiscard]] uint64_t index() const {
    assert(valid());
    return id_ & kIndexMask;
  }

  [[nodiscard]] bool is_file() const { return type() == entry_type::E_FILE; }
  [[nodiscard]] bool is_dir() const { return type() == entry_type::E_DIR; }
  [[nodiscard]] bool is_link() const { return type() == entry_type::E_LINK; }
  [[nodiscard]] bool is_device() const {
    return type() == entry_type::E_DEVICE;
  }
  [[nodiscard]] bool is_other() const { return type() == entry_type::E_OTHER; }

  friend bool
  operator==(entry_id const& lhs, entry_id const& rhs) noexcept = default;
  friend std::strong_ordering
  operator<=>(entry_id const& lhs, entry_id const& rhs) noexcept = default;

 private:
  uint64_t id_{kInvalidId};
};

template <entry_type Type>
class typed_entry_id {
 public:
  static constexpr uint64_t kInvalidIndex{std::numeric_limits<uint64_t>::max()};

  typed_entry_id() = default;

  explicit typed_entry_id(entry_id id)
      : index_{id.valid() && id.type() == Type ? id.index() : kInvalidIndex} {}

  explicit(false) operator entry_id() const {
    if (!valid()) {
      return {};
    }
    return entry_id{Type, index_};
  }

  [[nodiscard]] bool valid() const { return index_ != kInvalidIndex; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] std::size_t object_hash() const {
    return std::hash<uint64_t>{}(index_);
  }

  [[nodiscard]] entry_type type() const {
    assert(valid());
    return Type;
  }

  [[nodiscard]] uint64_t index() const {
    assert(valid());
    return index_;
  }

  [[nodiscard]] bool is_file() const {
    assert(valid());
    return Type == entry_type::E_FILE;
  }
  [[nodiscard]] bool is_dir() const {
    assert(valid());
    return Type == entry_type::E_DIR;
  }
  [[nodiscard]] bool is_link() const {
    assert(valid());
    return Type == entry_type::E_LINK;
  }
  [[nodiscard]] bool is_device() const {
    assert(valid());
    return Type == entry_type::E_DEVICE;
  }
  [[nodiscard]] bool is_other() const {
    assert(valid());
    return Type == entry_type::E_OTHER;
  }

  friend bool operator==(typed_entry_id const& lhs,
                         typed_entry_id const& rhs) noexcept = default;
  friend std::strong_ordering
  operator<=>(typed_entry_id const& lhs,
              typed_entry_id const& rhs) noexcept = default;

  friend bool
  operator==(typed_entry_id const& lhs, entry_id const& rhs) noexcept {
    return lhs.valid() && rhs.valid() && lhs.type() == rhs.type() &&
           lhs.index() == rhs.index();
  }

  friend bool
  operator==(entry_id const& lhs, typed_entry_id const& rhs) noexcept {
    return rhs == lhs;
  }

 private:
  uint64_t index_{kInvalidIndex};
};

using file_id = typed_entry_id<entry_type::E_FILE>;
using dir_id = typed_entry_id<entry_type::E_DIR>;
using link_id = typed_entry_id<entry_type::E_LINK>;
using device_id = typed_entry_id<entry_type::E_DEVICE>;
using other_id = typed_entry_id<entry_type::E_OTHER>;

} // namespace dwarfs::writer::internal

template <>
struct std::hash<dwarfs::writer::internal::entry_id> {
  size_t
  operator()(dwarfs::writer::internal::entry_id const& id) const noexcept {
    return id.object_hash();
  }
};

template <dwarfs::writer::internal::entry_type Type>
struct std::hash<dwarfs::writer::internal::typed_entry_id<Type>> {
  size_t operator()(
      dwarfs::writer::internal::typed_entry_id<Type> const& id) const noexcept {
    return id.object_hash();
  }
};

namespace dwarfs::container {

template <>
struct packed_value_traits<dwarfs::writer::internal::entry_id> {
  using value_type = dwarfs::writer::internal::entry_id;
  using encoded_type = uint64_t;
  static constexpr uint64_t kNumTypes{5};

  static encoded_type encode(value_type const& id) {
    if (!id.valid()) {
      return 0;
    }

    auto const index = id.index();
    auto const type = std::to_underlying(id.type());

    assert(type < kNumTypes);

    return index * kNumTypes + type + 1;
  }

  static value_type decode(encoded_type encoded) {
    if (encoded == 0) {
      return {};
    }

    auto const type = static_cast<dwarfs::writer::internal::entry_type>(
        (encoded - 1) % kNumTypes);
    auto const index = (encoded - 1) / kNumTypes;

    return {type, index};
  }
};

template <dwarfs::writer::internal::entry_type Type>
struct packed_value_traits<dwarfs::writer::internal::typed_entry_id<Type>> {
  using value_type = dwarfs::writer::internal::typed_entry_id<Type>;
  using encoded_type = uint64_t;

  static encoded_type encode(value_type const& id) {
    return id.valid() ? id.index() + 1 : 0;
  }

  static value_type decode(encoded_type encoded) {
    if (encoded == 0) {
      return {};
    }

    return value_type{dwarfs::writer::internal::entry_id{Type, encoded - 1}};
  }
};

} // namespace dwarfs::container

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

#include <cstdint>
#include <stdexcept>

#include <dwarfs/compiler.h>

namespace dwarfs::writer::internal {

class rsync_hash {
 public:
  using value_type = std::uint32_t;

  rsync_hash() = default;

  DWARFS_FORCE_INLINE value_type operator()() const {
    return (a_ & 0xffff) | (static_cast<value_type>(b_ & 0xffff) << 16);
  }

  DWARFS_FORCE_INLINE void set(value_type hash) {
    a_ = hash & 0xFFFF;
    b_ = (hash >> 16) & 0xFFFF;
  }

  DWARFS_FORCE_INLINE void update(std::uint8_t inbyte) {
    a_ += inbyte;
    b_ += a_;
    ++len_;
  }

  DWARFS_FORCE_INLINE void update(std::uint8_t outbyte, std::uint8_t inbyte) {
    a_ = a_ - outbyte + inbyte;
    b_ -= len_ * outbyte;
    b_ += a_;
  }

  DWARFS_FORCE_INLINE void clear() {
    a_ = 0;
    b_ = 0;
    len_ = 0;
  }

  static DWARFS_FORCE_INLINE constexpr value_type
  repeating_window(std::uint8_t byte, size_t length) {
    auto v = static_cast<std::uint16_t>(byte);
    auto a = static_cast<std::uint16_t>(v * length);
    auto b = static_cast<std::uint16_t>(v * (length * (length + 1)) / 2);
    return static_cast<value_type>(a) | (static_cast<value_type>(b) << 16);
  }

 private:
  std::uint_fast16_t a_{0};
  std::uint_fast16_t b_{0};
  std::int_fast32_t len_{0};
};

} // namespace dwarfs::writer::internal

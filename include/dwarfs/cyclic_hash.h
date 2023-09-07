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

#pragma once

#include <cstdint>
#include <stdexcept>

namespace dwarfs {

class rsync_hash {
 public:
  rsync_hash() = default;

  uint32_t operator()() const { return a_ | (uint32_t(b_) << 16); }

  void update(uint8_t inbyte) {
    a_ += inbyte;
    b_ += a_;
    ++len_;
  }

  void update(uint8_t outbyte, uint8_t inbyte) {
    a_ = a_ - outbyte + inbyte;
    b_ -= len_ * outbyte;
    b_ += a_;
  }

  void clear() {
    a_ = 0;
    b_ = 0;
    len_ = 0;
  }

  static constexpr uint32_t repeating_window(uint8_t byte, size_t length) {
    uint16_t v = static_cast<uint16_t>(byte);
    uint16_t a{static_cast<uint16_t>(v * length)};
    uint16_t b{static_cast<uint16_t>(v * (length * (length + 1)) / 2)};
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16);
  }

 private:
  uint16_t a_{0};
  uint16_t b_{0};
  int32_t len_{0};
};

} // namespace dwarfs

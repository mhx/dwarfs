/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <system_error>

namespace dwarfs::internal {

enum class device_id_layout {
  unknown = 0,
  unsupported = 1, /// Platform does not support devices (e.g. Windows)
  linux = 2,
  macos = 3,
  freebsd = 4,
};

class device_id {
 public:
  /// Returns the native layout of device IDs on this platform.
  static device_id_layout native_layout();
  static std::uint64_t
  convert_from_to(device_id_layout from, device_id_layout to,
                  std::uint64_t native, std::error_code& ec);

  device_id() = default;

  /// Constructs a device_id from a native representation.
  explicit device_id(std::uint64_t native)
      : native_{native} {}

  /// Constructs a device_id from a native representation using
  /// the specified layout. This involves a conversion from the
  /// specified layout to the native layout.
  device_id(device_id_layout layout, std::uint64_t native, std::error_code& ec);

  /// Constructs a device_id from major and minor numbers.
  device_id(std::uint32_t major, std::uint32_t minor, std::error_code& ec);

  /// Constructs from thrift type
  template <typename T>
  device_id(T const& id, std::error_code& ec)
    requires requires {
      { id.major() } -> std::convertible_to<std::uint32_t>;
      { id.minor() } -> std::convertible_to<std::uint32_t>;
    }
      : device_id{id.major(), id.minor(), ec} {}

  /// Constructs from frozen view type
  template <typename T>
  device_id(T const& id, std::error_code& ec)
    requires requires {
      { id.major().value() } -> std::convertible_to<std::uint32_t>;
      { id.minor().value() } -> std::convertible_to<std::uint32_t>;
    }
      : device_id{id.major().value(), id.minor().value(), ec} {}

  std::uint32_t major() const;
  std::uint32_t minor() const;
  std::uint64_t native() const { return native_; }

  /// Returns the native representation of this device ID in the
  /// specified layout.
  std::uint64_t native(device_id_layout layout, std::error_code& ec) const;

 private:
  std::uint64_t native_{0};
};

} // namespace dwarfs::internal

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

#include <concepts>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <system_error>

namespace dwarfs {

enum class device_number_layout {
  unknown = 0,
  unsupported = 1, /// Platform does not support devices (e.g. Windows)
  linux_dev_t = 2,
  macos_dev_t = 3,
  freebsd_dev_t = 4,
};

class device_number {
 public:
  /// Returns the native layout of device IDs on this platform.
  static device_number_layout native_layout();
  static std::uint64_t
  convert_from_to(device_number_layout from, device_number_layout to,
                  std::uint64_t native, std::error_code& ec);

  device_number() = default;

  /// Constructs a device_number from the native representation on this
  /// platform.
  explicit device_number(std::uint64_t native, std::error_code& ec);

  /// Constructs a device_number from a native representation using the
  /// specified layout.
  device_number(device_number_layout layout, std::uint64_t native,
                std::error_code& ec);

  /// Constructs a device_number from major_id and minor_id numbers.
  device_number(std::uint32_t major_id, std::uint32_t minor_id)
      : major_id_{major_id}
      , minor_id_{minor_id} {}

  /// Constructs from thrift type
  template <typename T>
  explicit device_number(T const& id)
    requires requires {
      { id.major_id() } -> std::convertible_to<std::uint32_t>;
      { id.minor_id() } -> std::convertible_to<std::uint32_t>;
    }
      : device_number{id.major_id(), id.minor_id()} {}

  /// Constructs from frozen view type
  template <typename T>
  explicit device_number(T const& id)
    requires requires {
      { id.major_id().value() } -> std::convertible_to<std::uint32_t>;
      { id.minor_id().value() } -> std::convertible_to<std::uint32_t>;
    }
      : device_number{id.major_id().value(), id.minor_id().value()} {}

  std::uint32_t major_id() const { return major_id_; }
  std::uint32_t minor_id() const { return minor_id_; }

  /// Returns the native representation of this device ID on this platform.
  std::uint64_t native(std::error_code& ec) const;

  /// Returns the native representation of this device ID in the
  /// specified layout.
  std::uint64_t native(device_number_layout layout, std::error_code& ec) const;

  std::string to_string() const;
  friend std::ostream& operator<<(std::ostream& os, device_number const& id);

 private:
  std::uint32_t major_id_{0};
  std::uint32_t minor_id_{0};
};

} // namespace dwarfs

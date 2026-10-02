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

#include <ostream>

#include <dwarfs/error.h>

#include <dwarfs/device_number.h>

namespace dwarfs {

namespace {

struct device_number_parts {
  std::uint32_t major_id;
  std::uint32_t minor_id;
};

device_number_parts
extract_major_minor(device_number_layout layout, std::uint64_t native,
                    std::error_code& ec) {
  DWARFS_CHECK(layout != device_number_layout::unknown,
               "unknown device ID layout");

  ec.clear();

  switch (layout) {
  case device_number_layout::linux_dev_t:
  case device_number_layout::unsupported:
    return {
        .major_id = static_cast<std::uint32_t>(
            ((native & UINT64_C(0x00000000000fff00)) >> 8) |
            ((native & UINT64_C(0xfffff00000000000)) >> 32)),
        .minor_id = static_cast<std::uint32_t>(
            (native & UINT64_C(0x00000000000000ff)) |
            ((native & UINT64_C(0x00000ffffff00000)) >> 12)),
    };

  case device_number_layout::macos_dev_t: {
    auto const value = static_cast<std::uint32_t>(native);
    auto const upper = static_cast<std::uint32_t>(native >> 32);

    // A macOS dev_t is a signed 32-bit value. Accept both zero extension and
    // the sign extension produced when a negative dev_t is converted to
    // uint64_t, but reject any other upper bits so decoding is never lossy.
    if (upper != 0 &&
        !(upper == UINT32_C(0xffffffff) && (value & UINT32_C(0x80000000)))) {
      ec = make_error_code(std::errc::result_out_of_range);
      return {};
    }

    return {
        .major_id = (value >> 24) & UINT32_C(0xff),
        .minor_id = value & UINT32_C(0x00ffffff),
    };
  }

  case device_number_layout::freebsd_dev_t:
    return {
        .major_id =
            static_cast<std::uint32_t>(((native >> 32) & UINT64_C(0xffffff00)) |
                                       ((native >> 8) & UINT64_C(0x000000ff))),
        .minor_id =
            static_cast<std::uint32_t>(((native >> 24) & UINT64_C(0x0000ff00)) |
                                       (native & UINT64_C(0xffff00ff))),
    };

  case device_number_layout::unknown:
    break;
  }

  DWARFS_PANIC("unreachable");
}

std::uint64_t
combine_major_minor(device_number_layout layout, std::uint32_t major_id,
                    std::uint32_t minor_id, std::error_code& ec) {
  DWARFS_CHECK(layout != device_number_layout::unknown,
               "unknown device ID layout");

  ec.clear();

  switch (layout) {
  case device_number_layout::linux_dev_t:
  case device_number_layout::unsupported:
    return (static_cast<std::uint64_t>(major_id & UINT32_C(0x00000fff)) << 8) |
           (static_cast<std::uint64_t>(major_id & UINT32_C(0xfffff000)) << 32) |
           (static_cast<std::uint64_t>(minor_id & UINT32_C(0x000000ff))) |
           (static_cast<std::uint64_t>(minor_id & UINT32_C(0xffffff00)) << 12);

  case device_number_layout::macos_dev_t: {
    if (major_id > UINT32_C(0xff) || minor_id > UINT32_C(0x00ffffff)) {
      ec = make_error_code(std::errc::result_out_of_range);
      return 0;
    }

    auto const value = (major_id << 24) | minor_id;

    // dev_t is int32_t on macOS. Reproduce the value obtained when native
    // makedev() returns dev_t and that value is subsequently converted to
    // uint64_t.
    std::uint64_t result = value;

    // Sign-extend the value if necessary.
    if (value & UINT32_C(0x80000000)) {
      result |= UINT64_C(0xffffffff00000000);
    }

    return result;
  }

  case device_number_layout::freebsd_dev_t:
    return (static_cast<std::uint64_t>(major_id & UINT32_C(0xffffff00)) << 32) |
           (static_cast<std::uint64_t>(major_id & UINT32_C(0x000000ff)) << 8) |
           (static_cast<std::uint64_t>(minor_id & UINT32_C(0x0000ff00)) << 24) |
           (static_cast<std::uint64_t>(minor_id & UINT32_C(0xffff00ff)));

  case device_number_layout::unknown:
    break;
  }

  DWARFS_PANIC("unreachable");
}

} // namespace

device_number_layout device_number::native_layout() {
#if defined(__linux__)
  return device_number_layout::linux_dev_t;
#elif defined(__APPLE__)
  return device_number_layout::macos_dev_t;
#elif defined(__FreeBSD__)
  return device_number_layout::freebsd_dev_t;
#elif defined(_WIN32)
  return device_number_layout::unsupported;
#else
  return device_number_layout::unknown;
#endif
}

std::uint64_t
device_number::convert_from_to(device_number_layout from,
                               device_number_layout to, std::uint64_t native,
                               std::error_code& ec) {
  DWARFS_CHECK(from != device_number_layout::unknown,
               "unknown source device ID layout");
  DWARFS_CHECK(to != device_number_layout::unknown,
               "unknown target device ID layout");

  auto const parts = extract_major_minor(from, native, ec);
  if (ec) {
    return 0;
  }

  return combine_major_minor(to, parts.major_id, parts.minor_id, ec);
}

device_number::device_number(std::uint64_t native, std::error_code& ec)
    : device_number{native_layout(), native, ec} {}

device_number::device_number(device_number_layout layout, std::uint64_t native,
                             std::error_code& ec) {
  DWARFS_CHECK(layout != device_number_layout::unknown,
               "unknown source device ID layout");

  auto const parts = extract_major_minor(layout, native, ec);
  if (!ec) {
    major_id_ = parts.major_id;
    minor_id_ = parts.minor_id;
  }
}

std::uint64_t device_number::native(std::error_code& ec) const {
  return native(native_layout(), ec);
}

std::uint64_t
device_number::native(device_number_layout layout, std::error_code& ec) const {
  DWARFS_CHECK(layout != device_number_layout::unknown,
               "unknown target device ID layout");
  return combine_major_minor(layout, major_id_, minor_id_, ec);
}

std::string device_number::to_string() const {
  return std::to_string(major_id()) + ":" + std::to_string(minor_id());
}

std::ostream& operator<<(std::ostream& os, device_number const& dev) {
  return os << dev.to_string();
}

} // namespace dwarfs

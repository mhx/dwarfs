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

#include <dwarfs/error.h>

#include <dwarfs/internal/device_id.h>

namespace dwarfs::internal {

namespace {

std::uint32_t extract_major(device_id_layout layout, std::uint64_t native) {
  DWARFS_CHECK(layout != device_id_layout::unknown, "unknown device ID layout");

  switch (layout) {
  case device_id_layout::linux:
  case device_id_layout::unsupported:
    return static_cast<std::uint32_t>(
        ((native & UINT64_C(0x00000000000fff00)) >> 8) |
        ((native & UINT64_C(0xfffff00000000000)) >> 32));

  case device_id_layout::macos: {
    // also handles macOS's sign-extended 32-bit dev_t
    auto const value = static_cast<std::uint32_t>(native);
    return (value >> 24) & UINT32_C(0xff);
  }

  case device_id_layout::freebsd:
    return static_cast<std::uint32_t>(((native >> 32) & UINT64_C(0xffffff00)) |
                                      ((native >> 8) & UINT64_C(0x000000ff)));

  case device_id_layout::unknown:
    break;
  }

  DWARFS_PANIC("unreachable");
}

std::uint32_t extract_minor(device_id_layout layout, std::uint64_t native) {
  DWARFS_CHECK(layout != device_id_layout::unknown, "unknown device ID layout");

  switch (layout) {
  case device_id_layout::linux:
  case device_id_layout::unsupported:
    return static_cast<std::uint32_t>(
        (native & UINT64_C(0x00000000000000ff)) |
        ((native & UINT64_C(0x00000ffffff00000)) >> 12));

  case device_id_layout::macos: {
    // also handles macOS's sign-extended 32-bit dev_t
    auto const value = static_cast<std::uint32_t>(native);
    return value & UINT32_C(0x00ffffff);
  }

  case device_id_layout::freebsd:
    return static_cast<std::uint32_t>(((native >> 24) & UINT64_C(0x0000ff00)) |
                                      (native & UINT64_C(0xffff00ff)));

  case device_id_layout::unknown:
    break;
  }

  DWARFS_PANIC("unreachable");
}

std::uint64_t combine_major_minor(device_id_layout layout, std::uint32_t major,
                                  std::uint32_t minor, std::error_code& ec) {
  DWARFS_CHECK(layout != device_id_layout::unknown, "unknown device ID layout");

  ec.clear();

  switch (layout) {
  case device_id_layout::linux:
  case device_id_layout::unsupported:
    return (static_cast<std::uint64_t>(major & UINT32_C(0x00000fff)) << 8) |
           (static_cast<std::uint64_t>(major & UINT32_C(0xfffff000)) << 32) |
           (static_cast<std::uint64_t>(minor & UINT32_C(0x000000ff))) |
           (static_cast<std::uint64_t>(minor & UINT32_C(0xffffff00)) << 12);

  case device_id_layout::macos: {
    if (major > UINT32_C(0xff) || minor > UINT32_C(0x00ffffff)) {
      ec = make_error_code(std::errc::result_out_of_range);
      return 0;
    }

    auto const value = (major << 24) | minor;

    // dev_t is int32_t on macOS. Reproduce the value obtained when
    // native makedev() returns dev_t and that value is subsequently
    // converted to uint64_t.
    std::uint64_t result = value;

    // sign-extend the value if necessary
    if (value & UINT32_C(0x80000000)) {
      result |= UINT64_C(0xffffffff00000000);
    }

    return result;
  }

  case device_id_layout::freebsd:
    return (static_cast<std::uint64_t>(major & UINT32_C(0xffffff00)) << 32) |
           (static_cast<std::uint64_t>(major & UINT32_C(0x000000ff)) << 8) |
           (static_cast<std::uint64_t>(minor & UINT32_C(0x0000ff00)) << 24) |
           (static_cast<std::uint64_t>(minor & UINT32_C(0xffff00ff)));

  case device_id_layout::unknown:
    break;
  }

  DWARFS_PANIC("unreachable");
}

} // namespace

device_id_layout device_id::native_layout() {
#if defined(__linux__)
  return device_id_layout::linux;
#elif defined(__APPLE__)
  return device_id_layout::macos;
#elif defined(__FreeBSD__)
  return device_id_layout::freebsd;
#elif defined(_WIN32)
  return device_id_layout::unsupported;
#else
  return device_id_layout::unknown;
#endif
}

std::uint64_t
device_id::convert_from_to(device_id_layout from, device_id_layout to,
                           std::uint64_t native, std::error_code& ec) {
  DWARFS_CHECK(from != device_id_layout::unknown,
               "unknown source device ID layout");
  DWARFS_CHECK(to != device_id_layout::unknown,
               "unknown target device ID layout");

  ec.clear();

  if (from == to) {
    return native;
  }

  auto const major = extract_major(from, native);
  auto const minor = extract_minor(from, native);

  return combine_major_minor(to, major, minor, ec);
}

device_id::device_id(device_id_layout layout, std::uint64_t native,
                     std::error_code& ec)
    : native_{convert_from_to(layout, native_layout(), native, ec)} {}

device_id::device_id(std::uint32_t major, std::uint32_t minor,
                     std::error_code& ec)
    : native_{combine_major_minor(native_layout(), major, minor, ec)} {}

std::uint32_t device_id::major() const {
  return extract_major(native_layout(), native_);
}

std::uint32_t device_id::minor() const {
  return extract_minor(native_layout(), native_);
}

std::uint64_t
device_id::native(device_id_layout layout, std::error_code& ec) const {
  return convert_from_to(native_layout(), layout, native_, ec);
}

} // namespace dwarfs::internal

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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <system_error>
#include <utility>

#if defined(__linux__)
#include <sys/sysmacros.h>
#elif !defined(_WIN32)
#include <sys/types.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#if !defined(_WIN32)

namespace {

// Keep the platform macros/functions behind wrappers to
// avoid conflicts with any major/minor/makedev names.
std::uint32_t native_major(dev_t dev) {
  return static_cast<std::uint32_t>(major(dev));
}

std::uint32_t native_minor(dev_t dev) {
  return static_cast<std::uint32_t>(minor(dev));
}

dev_t native_makedev(std::uint32_t major_value, std::uint32_t minor_value) {
  return makedev(major_value, minor_value);
}

std::uint64_t native_to_u64(dev_t dev) {
  return static_cast<std::uint64_t>(dev);
}

} // namespace

#endif

#ifdef major
#undef major
#endif
#ifdef minor
#undef minor
#endif
#ifdef makedev
#undef makedev
#endif

#include <dwarfs/device_number.h>

namespace {

using dwarfs::device_number;
using dwarfs::device_number_layout;

using ::testing::Eq;

struct encoding_case {
  std::uint32_t major_id;
  std::uint32_t minor_id;
  std::uint64_t linux;
  std::uint64_t macos;
  std::uint64_t freebsd;

  friend std::ostream& operator<<(std::ostream& os, encoding_case const& c) {
    return os << "major_id=" << c.major_id << ", minor_id=" << c.minor_id;
  }
};

constexpr std::array kPortableTestCases{
    encoding_case{UINT32_C(0x00000000), UINT32_C(0x00000000),
                  UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
                  UINT64_C(0x0000000000000000)},
    encoding_case{UINT32_C(0x00000001), UINT32_C(0x00000003),
                  UINT64_C(0x0000000000000103), UINT64_C(0x0000000001000003),
                  UINT64_C(0x0000000000000103)},
    encoding_case{UINT32_C(0x00000008), UINT32_C(0x00000001),
                  UINT64_C(0x0000000000000801), UINT64_C(0x0000000008000001),
                  UINT64_C(0x0000000000000801)},
    encoding_case{UINT32_C(0x0000007f), UINT32_C(0x00ffffff),
                  UINT64_C(0x0000000ffff07fff), UINT64_C(0x000000007fffffff),
                  UINT64_C(0x000000ff00ff7fff)},
    encoding_case{UINT32_C(0x00000080), UINT32_C(0x00000000),
                  UINT64_C(0x0000000000008000), UINT64_C(0xffffffff80000000),
                  UINT64_C(0x0000000000008000)},
    encoding_case{UINT32_C(0x00000080), UINT32_C(0x00000001),
                  UINT64_C(0x0000000000008001), UINT64_C(0xffffffff80000001),
                  UINT64_C(0x0000000000008001)},
    encoding_case{UINT32_C(0x000000ff), UINT32_C(0x00123456),
                  UINT64_C(0x000000012340ff56), UINT64_C(0xffffffffff123456),
                  UINT64_C(0x000000340012ff56)},
    encoding_case{UINT32_C(0x000000ff), UINT32_C(0x00ffffff),
                  UINT64_C(0x0000000ffff0ffff), UINT64_C(0xffffffffffffffff),
                  UINT64_C(0x000000ff00ffffff)},
};

struct wide_encoding_case {
  std::uint32_t major_id;
  std::uint32_t minor_id;
  std::uint64_t linux;
  std::uint64_t freebsd;

  friend std::ostream&
  operator<<(std::ostream& os, wide_encoding_case const& c) {
    return os << "major_id=" << c.major_id << ", minor_id=" << c.minor_id;
  }
};

constexpr std::array kWideTestCases{
    wide_encoding_case{UINT32_C(0x00000100), UINT32_C(0x00000000),
                       UINT64_C(0x0000000000010000),
                       UINT64_C(0x0000010000000000)},
    wide_encoding_case{UINT32_C(0x00000000), UINT32_C(0x01000000),
                       UINT64_C(0x0000001000000000),
                       UINT64_C(0x0000000001000000)},
    wide_encoding_case{UINT32_C(0x00000abc), UINT32_C(0x12345678),
                       UINT64_C(0x00000123456abc78),
                       UINT64_C(0x00000a561234bc78)},
    wide_encoding_case{UINT32_C(0x12345678), UINT32_C(0x9abcdef0),
                       UINT64_C(0x123459abcde678f0),
                       UINT64_C(0x123456de9abc78f0)},
    wide_encoding_case{UINT32_C(0xffffffff), UINT32_C(0xffffffff),
                       UINT64_C(0xffffffffffffffff),
                       UINT64_C(0xffffffffffffffff)},
};

constexpr std::array kLayouts{
    device_number_layout::linux_dev_t,
    device_number_layout::macos_dev_t,
    device_number_layout::freebsd_dev_t,
    device_number_layout::unsupported,
};

std::uint64_t encoded(encoding_case const& c, device_number_layout layout) {
  switch (layout) {
  case device_number_layout::linux_dev_t:
  case device_number_layout::unsupported:
    return c.linux;
  case device_number_layout::macos_dev_t:
    return c.macos;
  case device_number_layout::freebsd_dev_t:
    return c.freebsd;
  case device_number_layout::unknown:
    break;
  }

  std::abort();
}

std::uint64_t
encoded(wide_encoding_case const& c, device_number_layout layout) {
  switch (layout) {
  case device_number_layout::linux_dev_t:
  case device_number_layout::unsupported:
    return c.linux;
  case device_number_layout::freebsd_dev_t:
    return c.freebsd;
  case device_number_layout::macos_dev_t:
  case device_number_layout::unknown:
    break;
  }

  std::abort();
}

class device_number_portable : public ::testing::TestWithParam<encoding_case> {
};

class device_number_wide : public ::testing::TestWithParam<wide_encoding_case> {
};

std::uint32_t next_random(std::uint64_t& state) {
  state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
  return static_cast<std::uint32_t>(state >> 32);
}

} // namespace

TEST(device_number, native_layout_matches_platform) {
#if defined(__linux__)
  EXPECT_EQ(device_number::native_layout(), device_number_layout::linux_dev_t);
#elif defined(__APPLE__)
  EXPECT_EQ(device_number::native_layout(), device_number_layout::macos_dev_t);
#elif defined(__FreeBSD__)
  EXPECT_EQ(device_number::native_layout(),
            device_number_layout::freebsd_dev_t);
#elif defined(_WIN32)
  EXPECT_EQ(device_number::native_layout(), device_number_layout::unsupported);
#else
  EXPECT_EQ(device_number::native_layout(), device_number_layout::unknown);
#endif
}

TEST_P(device_number_portable, converts_between_all_layouts) {
  auto const& c = GetParam();

  for (auto const from : kLayouts) {
    for (auto const to : kLayouts) {
      SCOPED_TRACE(::testing::Message() << "from=" << static_cast<int>(from)
                                        << ", to=" << static_cast<int>(to));

      std::error_code ec = std::make_error_code(std::errc::invalid_argument);
      auto const result =
          device_number::convert_from_to(from, to, encoded(c, from), ec);

      EXPECT_FALSE(ec);
      EXPECT_EQ(result, encoded(c, to));
    }
  }
}

TEST_P(device_number_portable, constructs_from_each_layout) {
  auto const& c = GetParam();

  for (auto const from : kLayouts) {
    SCOPED_TRACE(::testing::Message() << "from=" << static_cast<int>(from));

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    device_number id{from, encoded(c, from), ec};

    ASSERT_FALSE(ec);
    EXPECT_EQ(id.major_id(), c.major_id);
    EXPECT_EQ(id.minor_id(), c.minor_id);
    ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(id.native(ec), encoded(c, device_number::native_layout()));
    EXPECT_FALSE(ec);

    for (auto const to : kLayouts) {
      SCOPED_TRACE(::testing::Message() << "to=" << static_cast<int>(to));

      ec = std::make_error_code(std::errc::invalid_argument);
      EXPECT_EQ(id.native(to, ec), encoded(c, to));
      EXPECT_FALSE(ec);
    }
  }
}

TEST_P(device_number_portable, constructs_from_major_and_minor) {
  auto const& c = GetParam();

  device_number id{c.major_id, c.minor_id};

  EXPECT_EQ(id.major_id(), c.major_id);
  EXPECT_EQ(id.minor_id(), c.minor_id);

  std::error_code ec = std::make_error_code(std::errc::invalid_argument);
  EXPECT_EQ(id.native(ec), encoded(c, device_number::native_layout()));
  EXPECT_FALSE(ec);

  for (auto const layout : kLayouts) {
    SCOPED_TRACE(::testing::Message() << "layout=" << static_cast<int>(layout));

    ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(id.native(layout, ec), encoded(c, layout));
    EXPECT_FALSE(ec);
  }
}

INSTANTIATE_TEST_SUITE_P(device_number, device_number_portable,
                         ::testing::ValuesIn(kPortableTestCases));

TEST_P(device_number_wide, converts_between_linux_and_freebsd) {
  auto const& c = GetParam();

  for (auto const from :
       {device_number_layout::linux_dev_t, device_number_layout::freebsd_dev_t,
        device_number_layout::unsupported}) {
    for (auto const to : {device_number_layout::linux_dev_t,
                          device_number_layout::freebsd_dev_t,
                          device_number_layout::unsupported}) {
      SCOPED_TRACE(::testing::Message() << "from=" << static_cast<int>(from)
                                        << ", to=" << static_cast<int>(to));

      std::error_code ec = std::make_error_code(std::errc::invalid_argument);
      auto const result =
          device_number::convert_from_to(from, to, encoded(c, from), ec);

      EXPECT_FALSE(ec);
      EXPECT_EQ(result, encoded(c, to));
    }
  }
}

TEST_P(device_number_wide, stores_components_without_native_layout_loss) {
  auto const& c = GetParam();
  device_number id{c.major_id, c.minor_id};

  EXPECT_EQ(id.major_id(), c.major_id);
  EXPECT_EQ(id.minor_id(), c.minor_id);

  for (auto const layout :
       {device_number_layout::linux_dev_t, device_number_layout::freebsd_dev_t,
        device_number_layout::unsupported}) {
    SCOPED_TRACE(::testing::Message() << "layout=" << static_cast<int>(layout));

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(id.native(layout, ec), encoded(c, layout));
    EXPECT_FALSE(ec);
  }

  std::error_code ec;
  EXPECT_EQ(id.native(device_number_layout::macos_dev_t, ec), UINT64_C(0));
  EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));
}

TEST_P(device_number_wide, constructs_from_foreign_layout_without_loss) {
  auto const& c = GetParam();

  for (auto const from :
       {device_number_layout::linux_dev_t, device_number_layout::freebsd_dev_t,
        device_number_layout::unsupported}) {
    SCOPED_TRACE(::testing::Message() << "from=" << static_cast<int>(from));

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    device_number id{from, encoded(c, from), ec};

    ASSERT_FALSE(ec);
    EXPECT_EQ(id.major_id(), c.major_id);
    EXPECT_EQ(id.minor_id(), c.minor_id);

    EXPECT_EQ(id.native(device_number_layout::linux_dev_t, ec), c.linux);
    EXPECT_FALSE(ec);
    EXPECT_EQ(id.native(device_number_layout::freebsd_dev_t, ec), c.freebsd);
    EXPECT_FALSE(ec);
  }
}

TEST_P(device_number_wide, macos_conversion_reports_out_of_range) {
  auto const& c = GetParam();

  for (auto const from :
       {device_number_layout::linux_dev_t, device_number_layout::freebsd_dev_t,
        device_number_layout::unsupported}) {
    SCOPED_TRACE(::testing::Message() << "from=" << static_cast<int>(from));

    std::error_code ec;
    EXPECT_EQ(device_number::convert_from_to(from,
                                             device_number_layout::macos_dev_t,
                                             encoded(c, from), ec),
              UINT64_C(0));
    EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));
  }
}

INSTANTIATE_TEST_SUITE_P(device_number, device_number_wide,
                         ::testing::ValuesIn(kWideTestCases));

TEST(device_number, unsupported_layout_is_linux_layout) {
  std::uint64_t state = UINT64_C(0xe2211ce4d3b27f31);

  for (unsigned i = 0; i < 4096; ++i) {
    auto const native = (static_cast<std::uint64_t>(next_random(state)) << 32) |
                        next_random(state);

    SCOPED_TRACE(::testing::Message() << "iteration=" << i);

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                             device_number_layout::unsupported,
                                             native, ec),
              native);
    EXPECT_FALSE(ec);

    ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(device_number::convert_from_to(device_number_layout::unsupported,
                                             device_number_layout::linux_dev_t,
                                             native, ec),
              native);
    EXPECT_FALSE(ec);
  }
}

TEST(device_number, linux_and_freebsd_are_bijective_over_all_64_bits) {
  std::uint64_t state = UINT64_C(0x243f6a8885a308d3);

  for (unsigned i = 0; i < 4096; ++i) {
    auto const linux = (static_cast<std::uint64_t>(next_random(state)) << 32) |
                       next_random(state);

    SCOPED_TRACE(::testing::Message() << "iteration=" << i);

    std::error_code ec;
    auto const freebsd = device_number::convert_from_to(
        device_number_layout::linux_dev_t, device_number_layout::freebsd_dev_t,
        linux, ec);
    ASSERT_FALSE(ec);

    auto const linux_round_trip = device_number::convert_from_to(
        device_number_layout::freebsd_dev_t, device_number_layout::linux_dev_t,
        freebsd, ec);
    ASSERT_FALSE(ec);

    EXPECT_EQ(linux_round_trip, linux);
  }
}

TEST(device_number, macos_accepts_zero_or_sign_extended_source_values) {
  constexpr std::uint64_t kZeroExtended = UINT64_C(0x0000000081234567);
  constexpr std::uint64_t kSignExtended = UINT64_C(0xffffffff81234567);
  constexpr std::uint64_t kExpectedLinux = UINT64_C(0x0000000234508167);
  constexpr std::uint64_t kExpectedFreeBsd = UINT64_C(0x0000004500238167);

  std::error_code ec;

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::macos_dev_t,
                                           device_number_layout::linux_dev_t,
                                           kZeroExtended, ec),
            kExpectedLinux);
  EXPECT_FALSE(ec);

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::macos_dev_t,
                                           device_number_layout::linux_dev_t,
                                           kSignExtended, ec),
            kExpectedLinux);
  EXPECT_FALSE(ec);

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::macos_dev_t,
                                           device_number_layout::freebsd_dev_t,
                                           kZeroExtended, ec),
            kExpectedFreeBsd);
  EXPECT_FALSE(ec);

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::macos_dev_t,
                                           device_number_layout::freebsd_dev_t,
                                           kSignExtended, ec),
            kExpectedFreeBsd);
  EXPECT_FALSE(ec);
}

TEST(device_number, macos_rejects_noncanonical_upper_bits) {
  for (auto const native : {
           UINT64_C(0x0000000181234567),
           UINT64_C(0xffffffff01234567),
           UINT64_C(0x1234567881234567),
       }) {
    SCOPED_TRACE(::testing::Message() << "native=" << native);

    std::error_code ec;
    device_number id{device_number_layout::macos_dev_t, native, ec};

    EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));
    EXPECT_EQ(id.major_id(), UINT32_C(0));
    EXPECT_EQ(id.minor_id(), UINT32_C(0));
  }
}

TEST(device_number, macos_output_is_sign_extended) {
  std::error_code ec;

  // major_id 0x7f leaves bit 31 clear
  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::macos_dev_t,
                                           UINT64_C(0x0000000ffff07fff), ec),
            UINT64_C(0x000000007fffffff));
  EXPECT_FALSE(ec);

  // major_id 0x80 sets bit 31
  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::macos_dev_t,
                                           UINT64_C(0x0000000000008000), ec),
            UINT64_C(0xffffffff80000000));
  EXPECT_FALSE(ec);

  // largest representable macOS device ID is -1 as a signed dev_t
  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::macos_dev_t,
                                           UINT64_C(0x0000000ffff0ffff), ec),
            UINT64_C(0xffffffffffffffff));
  EXPECT_FALSE(ec);
}

TEST(device_number, successful_conversion_clears_error_code) {
  std::error_code ec = std::make_error_code(std::errc::invalid_argument);

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::freebsd_dev_t,
                                           UINT64_C(0x00000123456abc78), ec),
            UINT64_C(0x00000a561234bc78));
  EXPECT_FALSE(ec);

  ec = std::make_error_code(std::errc::invalid_argument);
  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::linux_dev_t,
                                           UINT64_C(0x123456789abcdef0), ec),
            UINT64_C(0x123456789abcdef0));
  EXPECT_FALSE(ec);
}

TEST(device_number, conversion_recovers_after_out_of_range_error) {
  std::error_code ec;

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::macos_dev_t,
                                           UINT64_C(0x0000000000010000), ec),
            UINT64_C(0));
  EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));

  EXPECT_EQ(device_number::convert_from_to(device_number_layout::linux_dev_t,
                                           device_number_layout::macos_dev_t,
                                           UINT64_C(0x0000000000000801), ec),
            UINT64_C(0x0000000008000001));
  EXPECT_FALSE(ec);
}

#if !defined(_WIN32)

TEST(device_number, encoding_matches_native_device_number_functions) {
  constexpr std::array kCases{
      std::pair{UINT32_C(0x00), UINT32_C(0x000000)},
      std::pair{UINT32_C(0x01), UINT32_C(0x000003)},
      std::pair{UINT32_C(0x08), UINT32_C(0x000001)},
      std::pair{UINT32_C(0x7f), UINT32_C(0xffffff)},
      std::pair{UINT32_C(0x80), UINT32_C(0x000000)},
      std::pair{UINT32_C(0xff), UINT32_C(0x123456)},
      std::pair{UINT32_C(0xff), UINT32_C(0xffffff)},
  };

  for (auto const& [major_value, minor_value] : kCases) {
    SCOPED_TRACE(::testing::Message()
                 << "major_id=" << major_value << ", minor_id=" << minor_value);

    auto const native = native_makedev(major_value, minor_value);

    device_number from_components{major_value, minor_value};

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(from_components.native(ec), native_to_u64(native));
    ASSERT_FALSE(ec);

    device_number from_native{native_to_u64(native), ec};
    ASSERT_FALSE(ec);

    EXPECT_EQ(from_native.major_id(), native_major(native));
    EXPECT_EQ(from_native.minor_id(), native_minor(native));
    EXPECT_EQ(from_native.major_id(), major_value);
    EXPECT_EQ(from_native.minor_id(), minor_value);
  }
}

TEST(device_number,
     randomized_encoding_matches_native_device_number_functions) {
  std::uint64_t state = UINT64_C(0x13198a2e03707344);

  for (unsigned i = 0; i < 4096; ++i) {
    auto major_value = next_random(state);
    auto minor_value = next_random(state);

#if defined(__APPLE__)
    major_value &= UINT32_C(0x000000ff);
    minor_value &= UINT32_C(0x00ffffff);
#elif defined(__FreeBSD__)
    // FreeBSD's makedev() takes int arguments. Stay within the positive
    // range so the native comparison does not itself rely on an
    // implementation-defined uint32_t -> int conversion.
    major_value &= UINT32_C(0x7fffffff);
    minor_value &= UINT32_C(0x7fffffff);
#endif

    SCOPED_TRACE(::testing::Message()
                 << "iteration=" << i << ", major_id=" << major_value
                 << ", minor_id=" << minor_value);

    auto const native = native_makedev(major_value, minor_value);

    device_number from_components{major_value, minor_value};

    std::error_code ec = std::make_error_code(std::errc::invalid_argument);
    EXPECT_EQ(from_components.native(ec), native_to_u64(native));
    ASSERT_FALSE(ec);

    device_number from_native{native_to_u64(native), ec};
    ASSERT_FALSE(ec);

    EXPECT_EQ(from_native.major_id(), native_major(native));
    EXPECT_EQ(from_native.minor_id(), native_minor(native));
    EXPECT_EQ(from_native.major_id(), major_value);
    EXPECT_EQ(from_native.minor_id(), minor_value);
  }
}

#endif

#if defined(__APPLE__)

TEST(device_number, native_macos_rejects_unrepresentable_components) {
  device_number major_too_large{UINT32_C(0x100), UINT32_C(0)};
  EXPECT_EQ(major_too_large.major_id(), UINT32_C(0x100));
  EXPECT_EQ(major_too_large.minor_id(), UINT32_C(0));

  std::error_code ec;
  EXPECT_EQ(major_too_large.native(ec), UINT64_C(0));
  EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));

  device_number minor_too_large{UINT32_C(0), UINT32_C(0x01000000)};
  EXPECT_EQ(minor_too_large.major_id(), UINT32_C(0));
  EXPECT_EQ(minor_too_large.minor_id(), UINT32_C(0x01000000));

  EXPECT_EQ(minor_too_large.native(ec), UINT64_C(0));
  EXPECT_THAT(ec, Eq(std::make_error_code(std::errc::result_out_of_range)));
}

#endif

#if defined(_WIN32)

TEST(device_number, windows_native_representation_uses_linux_layout) {
  device_number id{UINT32_C(0x12345678), UINT32_C(0x9abcdef0)};

  EXPECT_EQ(id.major_id(), UINT32_C(0x12345678));
  EXPECT_EQ(id.minor_id(), UINT32_C(0x9abcdef0));

  std::error_code ec;
  EXPECT_EQ(id.native(ec), UINT64_C(0x123459abcde678f0));
  EXPECT_FALSE(ec);
}

#endif

TEST(device_number, rejects_unknown_source_layout) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        std::error_code ec;
        auto const r [[maybe_unused]] = device_number::convert_from_to(
            device_number_layout::unknown, device_number_layout::linux_dev_t, 0,
            ec);
      },
      "unknown source device ID layout");
}

TEST(device_number, rejects_unknown_target_layout) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        std::error_code ec;
        auto const r [[maybe_unused]] = device_number::convert_from_to(
            device_number_layout::linux_dev_t, device_number_layout::unknown, 0,
            ec);
      },
      "unknown target device ID layout");
}

TEST(device_number, constructor_rejects_unknown_layout) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        std::error_code ec;
        [[maybe_unused]] device_number id(device_number_layout::unknown, 0, ec);
      },
      "unknown source device ID layout");
}

TEST(device_number, native_rejects_unknown_layout) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        std::error_code ec;
        device_number id(UINT32_C(0), UINT32_C(0));
        auto const r [[maybe_unused]] =
            id.native(device_number_layout::unknown, ec);
      },
      "unknown target device ID layout");
}

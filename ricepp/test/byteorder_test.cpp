/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * ricepp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ricepp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ricepp.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <array>
#include <cstdint>
#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ricepp/byteswap.h>

namespace {
alignas(sizeof(uint8_t)) constexpr std::array<uint8_t, sizeof(uint8_t)> const
    kBE8 = {0x12};
alignas(sizeof(uint8_t)) constexpr std::array<uint8_t, sizeof(uint8_t)> const
    kLE8 = {0x12};

alignas(sizeof(uint16_t)) constexpr std::array<uint8_t, sizeof(uint16_t)> const
    kBE16 = {0x12, 0x34};
alignas(sizeof(uint16_t)) constexpr std::array<uint8_t, sizeof(uint16_t)> const
    kLE16 = {0x34, 0x12};

alignas(sizeof(uint32_t)) constexpr std::array<uint8_t, sizeof(uint32_t)> const
    kBE32 = {0x12, 0x34, 0x56, 0x78};
alignas(sizeof(uint32_t)) constexpr std::array<uint8_t, sizeof(uint32_t)> const
    kLE32 = {0x78, 0x56, 0x34, 0x12};

template <std::unsigned_integral ValueType>
ValueType load_data(std::array<uint8_t, sizeof(ValueType)> const& data) {
  ValueType value;
  std::memcpy(&value, data.data(), sizeof(ValueType));
  return value;
}

template <std::unsigned_integral ValueType>
bool compare_data(std::array<uint8_t, sizeof(ValueType)> const& data,
                  ValueType value) {
  ValueType tmp;
  std::memcpy(&tmp, data.data(), sizeof(ValueType));
  return tmp == value;
}
} // namespace

TEST(ricepp, byteswap_test) {
  EXPECT_EQ(0x12, ricepp::byteswap<uint8_t>(0x12, std::endian::big));
  EXPECT_EQ(0x12, ricepp::byteswap<uint8_t>(0x12, std::endian::little));
  auto const be16 = load_data<uint16_t>(kBE16);
  auto const le16 = load_data<uint16_t>(kLE16);
  EXPECT_EQ(0x1234, ricepp::byteswap<uint16_t>(be16, std::endian::big));
  EXPECT_EQ(0x1234, ricepp::byteswap<uint16_t>(le16, std::endian::little));
}

TEST(ricepp, byteswap_constexpr_test) {
  static constexpr uint8_t u8val{0x12};
  static constexpr uint16_t u16val{0x1234};
  static constexpr uint32_t u32val{0x12345678};

  static constexpr uint8_t const be8 =
      ricepp::byteswap<std::endian::big>(u8val);
  static constexpr uint16_t const be16 =
      ricepp::byteswap<std::endian::big>(u16val);
  static constexpr uint32_t const be32 =
      ricepp::byteswap<std::endian::big>(u32val);

  EXPECT_TRUE(compare_data(kBE8, be8));
  EXPECT_TRUE(compare_data(kBE16, be16));
  EXPECT_TRUE(compare_data(kBE32, be32));

  static constexpr uint8_t const le8 =
      ricepp::byteswap<std::endian::little>(u8val);
  static constexpr uint16_t const le16 =
      ricepp::byteswap<std::endian::little>(u16val);
  static constexpr uint32_t const le32 =
      ricepp::byteswap<std::endian::little>(u32val);

  EXPECT_TRUE(compare_data(kLE8, le8));
  EXPECT_TRUE(compare_data(kLE16, le16));
  EXPECT_TRUE(compare_data(kLE32, le32));
}

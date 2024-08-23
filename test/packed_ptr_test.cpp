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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/packed_ptr.h>

using namespace dwarfs::internal;

TEST(packed_ptr, initialize) {
  {
    packed_ptr<uint32_t> pp;

    EXPECT_EQ(pp.get(), nullptr);
    EXPECT_EQ(pp.get_data(), 0);
  }

  {
    alignas(8) uint32_t i = 42;
    packed_ptr<uint32_t> pp(&i);

    EXPECT_EQ(pp.get(), &i);
    EXPECT_EQ(pp.get_data(), 0);
  }

  {
    packed_ptr<uint32_t> pp(nullptr, 0x7);

    EXPECT_EQ(pp.get(), nullptr);
    EXPECT_EQ(pp.get_data(), 0x7);
  }

  {
    alignas(8) uint32_t i = 42;
    packed_ptr<uint32_t> pp(&i, 0x7);

    EXPECT_EQ(pp.get(), &i);
    EXPECT_EQ(pp.get_data(), 0x7);
    EXPECT_EQ(*pp, 42);
  }

  EXPECT_THAT(
      [] { packed_ptr<uint32_t> pp(nullptr, 0x8); },
      testing::ThrowsMessage<std::invalid_argument>("data out of bounds"));

  EXPECT_THAT(
      [] {
        packed_ptr<uint32_t> pp(reinterpret_cast<uint32_t*>(0x100004), 0x7);
      },
      testing::ThrowsMessage<std::invalid_argument>("pointer is not aligned"));
}

TEST(packed_ptr, integral) {
  using ptr_type = std::pair<int32_t, float>;
  packed_ptr<ptr_type, 2, uint8_t> pp;

  static_assert(std::is_same_v<decltype(pp.get_data()), uint8_t>);
  static_assert(std::is_same_v<decltype(pp.get()), ptr_type*>);
  static_assert(std::is_same_v<decltype(*pp), ptr_type&>);
  static_assert(std::is_same_v<decltype(pp[10]), ptr_type&>);
  static_assert(std::is_same_v<decltype(pp->first), int>);
  static_assert(std::is_same_v<decltype(pp->second), float>);

  alignas(4) ptr_type p = {42, 2.0f};
  pp.set(&p);

  EXPECT_EQ(pp.get(), &p);
  EXPECT_EQ(pp.get_data(), 0);
  EXPECT_EQ(pp->first, 42);
  EXPECT_EQ(pp->second, 2.0f);
  EXPECT_EQ(pp[0].first, 42);
  EXPECT_EQ(pp[0].second, 2.0f);

  EXPECT_THAT(
      [&] { pp.set_data(0x4); },
      testing::ThrowsMessage<std::invalid_argument>("data out of bounds"));

  pp.set_data(0x3);

  EXPECT_EQ(pp.get(), &p);
  EXPECT_EQ(pp.get_data(), 0x3);

  EXPECT_THAT(
      [&] { pp.set(reinterpret_cast<ptr_type*>(0x100001)); },
      testing::ThrowsMessage<std::invalid_argument>("pointer is not aligned"));
}

TEST(packed_ptr, enumeration) {
  using ptr_type = std::pair<int32_t, float>;
  enum class test_enum : unsigned { A = 1, B, C, D };
  packed_ptr<ptr_type, 2, test_enum> pp;

  static_assert(std::is_same_v<decltype(pp.get_data()), test_enum>);
  static_assert(std::is_same_v<decltype(pp.get()), ptr_type*>);
  static_assert(std::is_same_v<decltype(*pp), ptr_type&>);
  static_assert(std::is_same_v<decltype(pp[10]), ptr_type&>);
  static_assert(std::is_same_v<decltype(pp->first), int>);

  alignas(4) ptr_type p = {42, 2.0f};
  pp.set(&p);

  EXPECT_EQ(pp.get(), &p);
  EXPECT_EQ(pp.get_data(), static_cast<test_enum>(0));

  pp.set_data(test_enum::B);

  EXPECT_EQ(pp.get(), &p);
  EXPECT_EQ(pp.get_data(), test_enum::B);

  EXPECT_THAT(
      [&] { pp.set_data(test_enum::D); },
      testing::ThrowsMessage<std::invalid_argument>("data out of bounds"));
}

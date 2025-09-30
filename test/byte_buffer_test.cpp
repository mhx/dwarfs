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

#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/mapped_byte_buffer.h>

#include <dwarfs/reader/internal/block_cache_byte_buffer_factory.h>

#include "test_helpers.h"

TEST(byte_buffer_test, malloc_byte_buffer) {
  auto buf = dwarfs::malloc_byte_buffer::create();
  static_assert(std::same_as<decltype(buf), dwarfs::mutable_byte_buffer>);

  EXPECT_TRUE(buf);
  EXPECT_TRUE(buf.empty());

  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 0);
  EXPECT_EQ(buf.data(), nullptr);

  buf.reserve(20);
  EXPECT_EQ(buf.capacity(), 20);

  buf.resize(10);
  EXPECT_EQ(buf.size(), 10);
  EXPECT_EQ(buf.capacity(), 20);

  buf.clear();
  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 20);

  buf.append("Hello, World!", 13);
  EXPECT_EQ(buf.size(), 13);
  EXPECT_EQ(std::string_view(reinterpret_cast<char*>(buf.data()), buf.size()),
            "Hello, World!");

  buf.shrink_to_fit();
  EXPECT_EQ(buf.size(), 13);
  EXPECT_EQ(buf.capacity(), 13);

  buf.freeze_location();

  EXPECT_THAT([&] { buf.reserve(30); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on frozen buffer: reserve"));

  EXPECT_THAT(
      [&] { buf.resize(20); },
      ::testing::ThrowsMessage<std::runtime_error>(
          "operation not allowed on frozen buffer: resize beyond capacity"));

  EXPECT_NO_THROW(buf.resize(5));
  EXPECT_EQ(buf.size(), 5);
  EXPECT_NO_THROW(buf.append("!", 1));

  EXPECT_THAT(
      [&] { buf.append("Too much!", 9); },
      ::testing::ThrowsMessage<std::runtime_error>(
          "operation not allowed on frozen buffer: append beyond capacity"));

  EXPECT_THAT([&] { buf.clear(); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on frozen buffer: clear"));

  EXPECT_THAT([&] { buf.shrink_to_fit(); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on frozen buffer: shrink_to_fit"));

  auto buf2 = dwarfs::malloc_byte_buffer::create(buf.span());
  EXPECT_TRUE(buf2);
  EXPECT_FALSE(buf2.empty());
  EXPECT_EQ(buf2.size(), 6);
  EXPECT_NO_THROW(buf2.resize(30));
  EXPECT_TRUE(std::memcmp(buf.data(), buf2.data(), 6) == 0);

  buf = dwarfs::malloc_byte_buffer::create(13);

  EXPECT_TRUE(buf);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), 13);
}

TEST(byte_buffer_test, block_cache_byte_buffer_mmap) {
  using namespace dwarfs::reader;
  dwarfs::test::os_access_mock os;
  auto factory = internal::block_cache_byte_buffer_factory::create(
      os, block_cache_allocation_mode::MMAP);
  auto buf = factory.create_mutable_fixed_reserve(13);

  EXPECT_TRUE(buf);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 13);

  EXPECT_THAT([&] { buf.reserve(200); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on mmap buffer: reserve"));

  EXPECT_THAT([&] { buf.raw_buffer(); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on mmap buffer: raw_buffer"));

  buf.append("Hello, World!", 13);

  EXPECT_EQ(buf.size(), 13);

  EXPECT_THAT(
      [&] { buf.resize(20); },
      ::testing::ThrowsMessage<std::runtime_error>(
          "operation not allowed on mmap buffer: resize beyond capacity"));

  EXPECT_NO_THROW(buf.resize(12));

  EXPECT_THAT(
      [&] { buf.append("Too much!", 9); },
      ::testing::ThrowsMessage<std::runtime_error>(
          "operation not allowed on mmap buffer: append beyond capacity"));

  EXPECT_THAT([&] { buf.shrink_to_fit(); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on mmap buffer: shrink_to_fit"));

  EXPECT_THAT([&] { buf.clear(); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  "operation not allowed on mmap buffer: clear"));

  EXPECT_NO_THROW(buf.freeze_location());

  EXPECT_EQ(buf.span().size(), 12);

  auto shared = buf.share();

  EXPECT_FALSE(shared.empty());
  EXPECT_EQ(shared.size(), 12);
  EXPECT_EQ(shared.span().size(), 12);
}

TEST(byte_buffer_test, mapped_byte_buffer) {
  static constexpr std::string_view test_data = "Hello, World!";

  auto buf = dwarfs::mapped_byte_buffer::create(std::span(
      reinterpret_cast<uint8_t const*>(test_data.data()), test_data.size()));
  static_assert(std::same_as<decltype(buf), dwarfs::shared_byte_buffer>);

  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), test_data.size());
  EXPECT_EQ(buf.capacity(), test_data.size());
  EXPECT_EQ(buf.data(), reinterpret_cast<uint8_t const*>(test_data.data()));
}

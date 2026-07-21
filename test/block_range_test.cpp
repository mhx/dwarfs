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

#include <numeric>
#include <optional>
#include <span>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/block_range.h>

#include <dwarfs/reader/internal/cached_block.h>

using namespace dwarfs;

namespace {

class mock_cached_block : public reader::internal::cached_block {
 public:
  mock_cached_block() = default;
  explicit mock_cached_block(std::span<uint8_t const> span)
      : span_{span} {}

  size_t range_end() const override { return span_ ? span_->size() : 0; }
  uint8_t const* data() const override {
    return span_ ? span_->data() : nullptr;
  }
  std::span<uint8_t const> span() const override {
    return span_ ? *span_ : std::span<uint8_t const>();
  }
  void decompress_until(size_t) override {}
  size_t uncompressed_size() const override { return 0; }
  void touch() override {}
  bool last_used_before(std::chrono::steady_clock::time_point) const override {
    return false;
  }
  bool any_pages_swapped_out(std::vector<uint8_t>&) const override {
    return false;
  }

 private:
  std::optional<std::span<uint8_t const>> span_;
};

auto const kInvalidSpan =
    std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(0), 1);

} // namespace

TEST(block_range, uncompressed) {
  std::vector<uint8_t> data(100);
  std::iota(data.begin(), data.end(), 0);

  {
    reader::block_range range{data, 0, data.size()};
    EXPECT_EQ(range.data(), data.data());
    EXPECT_EQ(range.size(), 100);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin()));
  }

  {
    reader::block_range range{data, 10, 20};
    EXPECT_EQ(range.size(), 20);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin() + 10));
  }

  {
    reader::block_range range{data, 100, 0};
    EXPECT_EQ(range.size(), 0);
  }

  {
    reader::block_range range{data, 99, 1};
    EXPECT_EQ(range.size(), 1);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin() + 99));
  }

  EXPECT_THAT(
      [&data] { reader::block_range range(data, 101, 0); },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(::testing::HasSubstr(
          "block_range: offset out of range (101 > 100)")));

  EXPECT_THAT(
      [&data] { reader::block_range range(data, 100, 1); },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(::testing::HasSubstr(
          "block_range: size out of range (100 + 1 > 100)")));

  EXPECT_THAT(
      [] { reader::block_range range(kInvalidSpan, 0, 1); },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("block_range: non-empty block data is null")));
}

TEST(block_range, compressed) {
  std::vector<uint8_t> data(100);
  std::iota(data.begin(), data.end(), 0);

  {
    auto block = std::make_shared<mock_cached_block>(data);
    reader::block_range range{block, 0, data.size()};
    EXPECT_EQ(range.data(), data.data());
    EXPECT_EQ(range.size(), 100);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin()));
  }

  {
    auto block = std::make_shared<mock_cached_block>(data);
    reader::block_range range{block, 10, 20};
    EXPECT_EQ(range.size(), 20);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin() + 10));
  }

  {
    auto block = std::make_shared<mock_cached_block>();
    reader::block_range range{block, 0, 0};
    EXPECT_EQ(range.size(), 0);
  }

  EXPECT_THAT(
      [] {
        auto block = std::make_shared<mock_cached_block>(kInvalidSpan);
        reader::block_range range(block, 0, 1);
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("block_range: non-empty block data is null")));

  EXPECT_THAT(
      [&] {
        auto block = std::make_shared<mock_cached_block>(data);
        reader::block_range range(block, 101, 0);
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(::testing::HasSubstr(
          "block_range: offset out of range (101 > 100)")));

  EXPECT_THAT(
      [&] {
        auto block = std::make_shared<mock_cached_block>(data);
        reader::block_range range(block, 100, 1);
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(::testing::HasSubstr(
          "block_range: size out of range (100 + 1 > 100)")));
}

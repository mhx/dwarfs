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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/file_util.h>

#include <dwarfs/internal/mappable_file.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using namespace dwarfs::internal;
using namespace dwarfs::test;

namespace {

constexpr size_t kGranularity{64_KiB};

std::string_view to_sv(std::span<std::byte const> s) {
  return std::string_view(reinterpret_cast<char const*>(s.data()), s.size());
}

} // namespace

class map_read_tests : public ::testing::Test {
 protected:
  void SetUp() override {
    total_size_ = 5 * kGranularity + 123;

    data_ = create_random_string(total_size_, 0xC0FFEE);

    p_ = tmp_.path() / "map_read_test.bin";

    {
      std::ofstream os(p_, std::ios::binary);
      ASSERT_TRUE(os.is_open()) << "failed to open " << p_;
      os.write(data_.data(), static_cast<std::streamsize>(data_.size()));
      ASSERT_TRUE(os.good()) << "failed writing to " << p_;
    }

    std::error_code ec;
    mf_ = mappable_file::create(p_, ec);
    ASSERT_FALSE(ec) << "mappable_file::create: " << ec.message();

    auto sz = mf_.size(ec);
    ASSERT_FALSE(ec) << "mappable_file::size: " << ec.message();
    ASSERT_EQ(sz, static_cast<file_size_t>(total_size_));
  }

  std::string_view expect_slice(size_t off, size_t sz) const {
    return std::string_view(data_).substr(off, sz);
  }

  temporary_directory tmp_;
  std::filesystem::path p_;
  size_t total_size_{};
  std::string data_;
  mappable_file mf_;
};

// --- tests -------------------------------------------------------------------

TEST_F(map_read_tests, map_whole_file_default_range) {
  std::error_code ec;
  auto mm = mf_.map_readonly(ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(0, static_cast<file_size_t>(total_size_)));
  EXPECT_EQ(mm.size(), total_size_);

  EXPECT_EQ(to_sv(mm.const_span()), expect_slice(0, total_size_));
}

TEST_F(map_read_tests, map_unaligned_spans_multiple_granules) {
  size_t const off = (kGranularity / 2) + 37; // intentionally unaligned
  size_t const sz = (3 * kGranularity) + 10;  // crosses several granules
  ASSERT_LE(off + sz, total_size_);

  std::error_code ec;
  auto mm = mf_.map_readonly(static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(static_cast<file_off_t>(off),
                                   static_cast<file_size_t>(sz)));
  EXPECT_EQ(mm.size(), sz);
  EXPECT_EQ(to_sv(mm.const_span()), expect_slice(off, sz));
}

TEST_F(map_read_tests, map_boundary_cross_with_unaligned_offset) {
  size_t const off = 2 * kGranularity + (kGranularity / 2) + 7;
  size_t const sz = kGranularity + 91;
  ASSERT_LE(off + sz, total_size_);

  std::error_code ec;
  auto mm = mf_.map_readonly(static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(static_cast<file_off_t>(off),
                                   static_cast<file_size_t>(sz)));
  EXPECT_EQ(to_sv(mm.const_span()), expect_slice(off, sz));
}

TEST_F(map_read_tests, map_small_prefix) {
  size_t const off = 0;
  size_t const sz = 7;

  std::error_code ec;
  auto mm = mf_.map_readonly(static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(0, static_cast<file_size_t>(sz)));
  EXPECT_EQ(to_sv(mm.const_span()), expect_slice(off, sz));
}

TEST_F(map_read_tests, map_small_tail) {
  size_t const sz = 9;
  size_t const off = total_size_ - sz;

  std::error_code ec;
  auto mm = mf_.map_readonly(static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(static_cast<file_off_t>(off),
                                   static_cast<file_size_t>(sz)));
  EXPECT_EQ(to_sv(mm.const_span()), expect_slice(off, sz));
}

TEST_F(map_read_tests, map_zero_size_is_empty_with_precise_range) {
  size_t const off = (kGranularity / 2) + 1;
  size_t const sz = 0;

  std::error_code ec;
  auto mm = mf_.map_readonly(static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::map_readonly: " << ec.message();

  EXPECT_EQ(mm.range(), file_range(static_cast<file_off_t>(off), 0));
  EXPECT_TRUE(mm.const_span().empty());
  EXPECT_EQ(mm.size(), 0u);
  EXPECT_EQ(to_sv(mm.const_span()), std::string_view{});
}

TEST_F(map_read_tests, read_span_unaligned_offset_and_size) {
  size_t const off = (kGranularity / 3) + 55;
  size_t const sz = (2 * kGranularity) + 17;
  ASSERT_LE(off + sz, total_size_);

  std::vector<std::byte> buf(sz);
  std::error_code ec;
  auto n =
      mf_.read(std::span<std::byte>(buf), static_cast<file_off_t>(off), ec);
  ASSERT_FALSE(ec) << "mappable_file::read: " << ec.message();
  ASSERT_EQ(n, sz);

  EXPECT_EQ(std::string_view(reinterpret_cast<char const*>(buf.data()), n),
            expect_slice(off, sz));
}

TEST_F(map_read_tests, read_void_ptr_small_slice) {
  size_t const off = 13;
  size_t const sz = 31;

  std::vector<std::byte> buf(sz);
  std::error_code ec;
  auto n = mf_.read(buf.data(), static_cast<file_off_t>(off), sz, ec);
  ASSERT_FALSE(ec) << "mappable_file::read: " << ec.message();
  ASSERT_EQ(n, sz);

  EXPECT_EQ(std::string_view(reinterpret_cast<char const*>(buf.data()), n),
            expect_slice(off, sz));
}

TEST_F(map_read_tests, read_short_when_request_crosses_eof) {
  size_t const off = total_size_ - 10;
  size_t const req = 50; // extends past EOF by 40 bytes

  std::vector<std::byte> buf(req);
  std::error_code ec;
  auto n = mf_.read(buf.data(), static_cast<file_off_t>(off), req, ec);
  ASSERT_FALSE(ec) << "mappable_file::read: " << ec.message();
  ASSERT_EQ(n, 10u);

  EXPECT_EQ(std::string_view(reinterpret_cast<char const*>(buf.data()), n),
            expect_slice(off, n));
}

TEST_F(map_read_tests, read_at_eof_returns_zero) {
  size_t const off = total_size_;
  std::vector<std::byte> buf(8);

  std::error_code ec;
  auto n =
      mf_.read(std::span<std::byte>(buf), static_cast<file_off_t>(off), ec);
  ASSERT_FALSE(ec) << "mappable_file::read: " << ec.message();
  EXPECT_EQ(n, 0u);
}

TEST(zero_memory, basic) {
  auto zeroes = mappable_file::map_empty_readonly(8_MiB);
  auto span = zeroes.const_span<uint8_t>();

  EXPECT_EQ(zeroes.size(), 8_MiB);
  EXPECT_EQ(span.size(), 8_MiB);
  EXPECT_TRUE(std::ranges::all_of(span, [](auto b) { return b == 0; }));
}

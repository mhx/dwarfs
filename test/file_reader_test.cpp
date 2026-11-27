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
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_v2.h>

#include <dwarfs/reader/internal/metadata_types.h>

#include <dwarfs/gen-cpp-lite/metadata_layouts.h>

using namespace dwarfs;
using namespace dwarfs::reader;
using namespace testing;

namespace {

using read_result = std::vector<std::future<block_range>>;

constexpr auto kUnlimited = std::numeric_limits<size_t>::max();

std::future<block_range> ready_range(std::span<uint8_t const> data) {
  std::promise<block_range> p;
  p.set_value(block_range{data});
  return p.get_future();
}

class file_reader_backend : public filesystem_v2_lite::impl_lite {
 public:
  MOCK_METHOD(file_stat, getattr,
              (inode_view, getattr_options const&, std::error_code&),
              (const, override));
  MOCK_METHOD(read_result, readv,
              (uint32_t, size_t, file_off_t, size_t, std::error_code&),
              (const, override));

  filesystem_version version() const override { unexpected_call(); }
  void walk(std::function<void(dir_entry_view)> const&) const override {
    unexpected_call();
  }
  dir_entry_view_iterable directory_entries() const override {
    unexpected_call();
  }
  dir_entry_view_iterable entries_in_data_order() const override {
    unexpected_call();
  }
  dir_entry_view root() const override { unexpected_call(); }
  std::optional<dir_entry_view> find(std::string_view) const override {
    unexpected_call();
  }
  std::optional<inode_view> find(int) const override { unexpected_call(); }
  std::optional<dir_entry_view> find(int, std::string_view) const override {
    unexpected_call();
  }
  device_number get_device(inode_view, std::error_code&) const override {
    unexpected_call();
  }
  void access(inode_view, int, file_stat::uid_type, file_stat::gid_type,
              std::error_code&) const override {
    unexpected_call();
  }
  std::optional<directory_view> opendir(inode_view) const override {
    unexpected_call();
  }
  std::optional<dir_entry_view> readdir(directory_view, size_t) const override {
    unexpected_call();
  }
  size_t dirsize(directory_view) const override { unexpected_call(); }
  std::string
  readlink(inode_view, readlink_mode, std::error_code&) const override {
    unexpected_call();
  }
  void statvfs(vfs_stat*) const override { unexpected_call(); }
  int open(inode_view, std::error_code&) const override { unexpected_call(); }
  file_off_t
  seek(uint32_t, file_off_t, seek_whence, std::error_code&) const override {
    unexpected_call();
  }
  size_t
  read(uint32_t, char*, size_t, file_off_t, std::error_code&) const override {
    unexpected_call();
  }
  size_t readv(uint32_t, iovec_read_buf&, size_t, file_off_t, size_t,
               std::error_code&) const override {
    unexpected_call();
  }
  void set_num_workers(size_t) override { unexpected_call(); }
  void set_cache_tidy_config(cache_tidy_config const&) override {
    unexpected_call();
  }
  size_t num_blocks() const override { unexpected_call(); }
  size_t block_size() const override { unexpected_call(); }
  file_off_t image_offset() const override { unexpected_call(); }
  file_size_t image_size() const override { unexpected_call(); }
  bool has_symlinks() const override { unexpected_call(); }
  bool has_sparse_files() const override { unexpected_call(); }
  nlohmann::json get_inode_info(inode_view, size_t) const override {
    unexpected_call();
  }
  void cache_blocks_by_category(std::string_view) const override {
    unexpected_call();
  }
  void cache_all_blocks() const override { unexpected_call(); }

 private:
  [[noreturn]] static void unexpected_call() {
    throw std::logic_error("unexpected filesystem call");
  }
};

class file_reader_filesystem : public filesystem_v2_lite {
 public:
  explicit file_reader_filesystem(std::unique_ptr<impl_lite> backend)
      : filesystem_v2_lite(std::move(backend)) {}
};

auto make_inode_metadata() {
  thrift::metadata::metadata raw;
  raw.inodes()->resize(1);
  raw.modes()->push_back(posix_file_type::regular);
  return apache::thrift::frozen::freeze(raw);
}

} // namespace

class file_reader_test : public Test {
 protected:
  file_reader_test()
      : fs{std::unique_ptr<file_reader_backend>{backend}} {
    ON_CALL(*backend, getattr(_, _, _))
        .WillByDefault([this](auto, auto const&, auto&) {
          file_stat st;
          st.set_mode(posix_file_type::regular);
          st.set_size(size);
          return st;
        });
    sem.post(8);
  }

  reader::detail::file_reader make_reader() {
    auto iv = std::make_shared<reader::internal::inode_view_impl>(
        meta.inodes()[0], 42, meta);
    return {fs, inode_view{std::move(iv)}};
  }

  static auto as_span(block_range const& range) {
    return std::span{range.data(), range.size()};
  }

  static auto throws_error(std::string_view msg) {
    return ThrowsMessage<dwarfs::runtime_error>(HasSubstr(msg));
  }

  void expect_read(size_t n, file_off_t offset) {
    EXPECT_CALL(*backend, readv(42, n, offset, kUnlimited, _))
        .WillOnce([this](auto, size_t count, file_off_t off, auto, auto&) {
          read_result result;
          result.push_back(ready_range(
              std::span{data}.subspan(static_cast<size_t>(off), count)));
          return result;
        });
  }

  std::array<uint8_t, 8> const data{0, 1, 2, 3, 4, 5, 6, 7};
  file_size_t size{8};
  decltype(make_inode_metadata()) meta{make_inode_metadata()};
  counting_semaphore sem;
  NiceMock<file_reader_backend>* backend{new NiceMock<file_reader_backend>};
  file_reader_filesystem fs;
};

TEST_F(file_reader_test, readv_overload_error_propagation) {
  auto const error = std::make_error_code(std::errc::invalid_argument);
  EXPECT_CALL(*backend, readv(42, kUnlimited, 0, kUnlimited, _))
      .Times(2)
      .WillRepeatedly([&](auto, auto, auto, auto, std::error_code& ec) {
        ec = error;
        return read_result{};
      });

  std::error_code ec;
  EXPECT_TRUE(fs.readv(42, ec).empty());
  EXPECT_EQ(ec, error);
  EXPECT_THROW(fs.readv(42), std::system_error);
}

TEST_F(file_reader_test, readv_overload_explicit_maxiov) {
  EXPECT_CALL(*backend, readv(42, 4, 2, 1, _))
      .Times(2)
      .WillRepeatedly([&](auto, auto, auto, auto, std::error_code& ec) {
        ec.clear();
        read_result result;
        result.push_back(ready_range(std::span{data}.subspan(2, 4)));
        return result;
      });

  std::error_code ec;
  auto ranges = fs.readv(42, 4, 2, 1, ec);
  ASSERT_FALSE(ec);
  ASSERT_EQ(ranges.size(), 1);
  EXPECT_THAT(as_span(ranges.front().get()), ElementsAre(2, 3, 4, 5));

  ranges = fs.readv(42, 4, 2, 1);
  ASSERT_EQ(ranges.size(), 1);
  EXPECT_THAT(as_span(ranges.front().get()), ElementsAre(2, 3, 4, 5));
}

TEST_F(file_reader_test, invalid_byte_limit) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(_, _, _, _, _)).Times(0);
  for (file_size_t limit : {0, -1}) {
    SCOPED_TRACE(limit);
    EXPECT_THAT([&] { (void)fr.read_sequential(sem, limit); },
                throws_error("sequential read byte limit must be positive"));
  }
}

TEST_F(file_reader_test, invalid_ranges) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(_, _, _, _, _)).Times(0);
  constexpr auto max = std::numeric_limits<file_size_t>::max();

  for (auto range : {file_range{-1, 1}, file_range{0, -1}, file_range{9, 0},
                     file_range{8, 1}, file_range{1, max}}) {
    SCOPED_TRACE(testing::Message()
                 << "offset=" << range.offset() << ", size=" << range.size());
    EXPECT_THAT([&] { (void)fr.read_sequential(std::span{&range, 1}, sem, 8); },
                throws_error("sequential read range outside file"));
  }
}

TEST_F(file_reader_test, total_range_size_overflow) {
  size = std::numeric_limits<file_size_t>::max();
  auto fr = make_reader();
  std::array ranges{file_range{0, size}, file_range{0, 1}};
  EXPECT_CALL(*backend, readv(_, _, _, _, _)).Times(0);
  EXPECT_THAT([&] { (void)fr.read_sequential(ranges, sem, 8); },
              throws_error("invalid sequential read range size"));
}

TEST_F(file_reader_test, empty_ranges) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(_, _, _, _, _)).Times(0);

  std::array ranges{file_range{0, 0}, file_range{8, 0}};
  auto empty = fr.read_sequential(ranges, sem, 1);
  EXPECT_TRUE(empty.begin() == empty.end());

  auto no_ranges = fr.read_sequential(std::span<file_range const>{}, sem, 1);
  EXPECT_TRUE(no_ranges.begin() == no_ranges.end());

  size = 0;
  auto empty_fr = make_reader();
  auto whole_file = empty_fr.read_sequential(sem, 1);
  EXPECT_TRUE(whole_file.begin() == whole_file.end());
}

TEST_F(file_reader_test, mixed_empty_and_nonempty_ranges) {
  auto fr = make_reader();
  std::array ranges{file_range{0, 0}, file_range{2, 3}, file_range{5, 0},
                    file_range{0, 1}, file_range{8, 0}};
  InSequence seq;
  expect_read(2, 2);
  expect_read(1, 4);
  expect_read(1, 0);

  auto iterable = fr.read_sequential(ranges, sem, 2);
  auto it = iterable.begin();

  for (auto const& expected : {std::vector<uint8_t>{2, 3}, {4}, {0}}) {
    ASSERT_FALSE(it == iterable.end());
    EXPECT_THAT(as_span(*it), ElementsAreArray(expected));
    ++it;
  }

  EXPECT_TRUE(it == iterable.end());
}

TEST_F(file_reader_test, increment_without_dereferencing) {
  size = 4;
  auto fr = make_reader();

  {
    InSequence seq;
    EXPECT_CALL(*backend, readv(42, 2, 0, kUnlimited, _))
        .WillOnce([&](auto, auto, auto, auto, auto&) {
          read_result result;
          result.push_back(ready_range(std::span{data}.subspan(0, 1)));
          result.push_back(ready_range(std::span{data}.subspan(1, 1)));
          return result;
        });
    expect_read(1, 2);
    expect_read(1, 3);
  }

  auto iterable = fr.read_sequential(sem, 2);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  // consume first range without reading it
  ++it;
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT(as_span(*it), ElementsAre(1));
  // repeated dereference
  EXPECT_THAT(as_span(*it), ElementsAre(1));
  ++it;
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT(as_span(*it), ElementsAre(2));
  ++it;
  ASSERT_FALSE(it == iterable.end());
  // skip final item to reach the end
  it++;
  EXPECT_TRUE(it == iterable.end());
}

TEST_F(file_reader_test, skip_all_ranges_with_one_byte_budget) {
  auto fr = make_reader();
  InSequence seq;
  for (file_off_t offset = 0; offset < size; ++offset) {
    expect_read(1, offset);
  }

  auto iterable = fr.read_sequential(sem, 1);
  auto it = iterable.begin();

  for (file_off_t offset = 0; offset < size; ++offset) {
    ASSERT_FALSE(it == iterable.end());
    ++it;
  }

  EXPECT_TRUE(it == iterable.end());
}

TEST_F(file_reader_test, future_exception_when_skipping_range) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(42, 8, 0, kUnlimited, _))
      .WillOnce([](auto, auto, auto, auto, auto&) {
        std::promise<block_range> p;
        p.set_exception(
            std::make_exception_ptr(std::logic_error("read failed")));
        read_result result;
        result.push_back(p.get_future());
        return result;
      });

  auto iterable = fr.read_sequential(sem, 8);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT([&] { ++it; },
              ThrowsMessage<std::logic_error>(HasSubstr("read failed")));
}

TEST_F(file_reader_test, empty_read_result) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(42, 8, 0, kUnlimited, _))
      .WillOnce([](auto, auto, auto, auto, auto&) { return read_result{}; });
  EXPECT_THAT([&] { (void)fr.read_sequential(sem, 8); },
              throws_error("empty sequential read"));
}

TEST_F(file_reader_test, readv_exception_on_increment) {
  auto fr = make_reader();
  InSequence seq;
  expect_read(4, 0);
  EXPECT_CALL(*backend, readv(42, 4, 4, kUnlimited, _))
      .WillOnce([](auto, auto, auto, auto, auto&) -> read_result {
        throw std::logic_error("readv failed");
      });

  auto iterable = fr.read_sequential(sem, 4);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT(as_span(*it), ElementsAre(0, 1, 2, 3));
  EXPECT_THAT([&] { ++it; },
              ThrowsMessage<std::logic_error>(HasSubstr("readv failed")));
}

TEST_F(file_reader_test, short_read_result) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(42, 8, 0, kUnlimited, _))
      .WillOnce([&](auto, auto, auto, auto, auto&) {
        read_result result;
        result.push_back(ready_range(std::span{data}.first(7)));
        return result;
      });

  auto iterable = fr.read_sequential(sem, 8);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT(as_span(*it), ElementsAre(0, 1, 2, 3, 4, 5, 6));
  EXPECT_THAT([&] { ++it; }, throws_error("short sequential read"));
}

TEST_F(file_reader_test, zero_size_block_range) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(42, 8, 0, kUnlimited, _))
      .WillOnce([](auto, auto, auto, auto, auto&) {
        read_result result;
        result.push_back(ready_range({}));
        return result;
      });

  auto iterable = fr.read_sequential(sem, 8);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT([&] { (void)*it; }, throws_error("invalid sequential read size"));
}

TEST_F(file_reader_test, oversized_block_range) {
  auto fr = make_reader();
  EXPECT_CALL(*backend, readv(42, 4, 0, kUnlimited, _))
      .WillOnce([&](auto, auto, auto, auto, auto&) {
        read_result result;
        result.push_back(ready_range(std::span{data}.first(5)));
        return result;
      });

  auto iterable = fr.read_sequential(sem, 4);
  auto it = iterable.begin();
  ASSERT_FALSE(it == iterable.end());
  EXPECT_THAT([&] { (void)*it; }, throws_error("invalid sequential read size"));
}

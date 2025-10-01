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

#include <bit>
#include <limits>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/file_util.h>

#include <dwarfs/internal/memory_mapping_ops.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using dwarfs::internal::get_native_memory_mapping_ops;
using dwarfs::internal::memory_access;

#define EXPECT_NO_ERROR(ec)                                                    \
  EXPECT_FALSE(ec) << "Unexpected error: " << ec.message() << " ("             \
                   << ec.value() << ")"

#define ASSERT_NO_ERROR(ec)                                                    \
  ASSERT_FALSE(ec) << "Unexpected error: " << ec.message() << " ("             \
                   << ec.value() << ")"

#define EXPECT_EC_IMPL(ec, cat, val)                                           \
  do {                                                                         \
    EXPECT_TRUE(ec);                                                           \
    if (ec) {                                                                  \
      EXPECT_EQ(cat, (ec).category());                                         \
      EXPECT_THAT((ec).value(), ::testing::AnyOf val)                          \
          << ": " << (ec).message();                                           \
    }                                                                          \
  } while (0)

#ifdef _WIN32
#define EXPECT_EC_UNIX_BSD_MAC_WIN(ec, unix, bsd, mac, windows)                \
  EXPECT_EC_IMPL(ec, std::system_category(), windows)
#elif defined(__FreeBSD__)
#define EXPECT_EC_UNIX_BSD_MAC_WIN(ec, unix, bsd, mac, windows)                \
  EXPECT_EC_IMPL(ec, std::generic_category(), bsd)
#elif defined(__APPLE__)
#define EXPECT_EC_UNIX_BSD_MAC_WIN(ec, unix, bsd, mac, windows)                \
  EXPECT_EC_IMPL(ec, std::generic_category(), mac)
#else
#define EXPECT_EC_UNIX_BSD_MAC_WIN(ec, unix, bsd, mac, windows)                \
  EXPECT_EC_IMPL(ec, std::generic_category(), unix)
#endif

namespace {

void* const kBadPtr =
#ifdef __FreeBSD__
    nullptr
#else
    reinterpret_cast<void*>(UINTPTR_MAX)
#endif
    ;

} // namespace

class memory_mapping_ops_test : public ::testing::Test {
 protected:
  void SetUp() override { td.emplace("dwarfs_mmap_ops"); }

  void TearDown() override { td.reset(); }

  std::optional<temporary_directory> td;
  dwarfs::internal::memory_mapping_ops const& ops{
      get_native_memory_mapping_ops()};
};

TEST(memory_mapping_ops, invalid_handle) {
  auto const& ops = get_native_memory_mapping_ops();

  std::error_code ec;
  ops.size(123, ec);

  EXPECT_TRUE(ec);
  EXPECT_EQ(ec, std::errc::bad_file_descriptor);

  ec.clear();

  char buf[16];
  auto n = ops.pread(nullptr, buf, sizeof(buf), 0, ec);
  EXPECT_TRUE(ec);
  EXPECT_EQ(ec, std::errc::bad_file_descriptor);
  EXPECT_EQ(n, 0);

  ec.clear();

  ops.close("hello", ec);
  EXPECT_TRUE(ec);
  EXPECT_EQ(ec, std::errc::bad_file_descriptor);

  ec.clear();

  auto const p = ops.map(3.14, 0, 16, ec);
  EXPECT_TRUE(ec);
  EXPECT_EQ(ec, std::errc::bad_file_descriptor);
  EXPECT_EQ(p, nullptr);
}

TEST(memory_mapping_ops, granularity) {
  auto const& ops = get_native_memory_mapping_ops();

  auto const gran = ops.granularity();
  EXPECT_GT(gran, 0u);
  EXPECT_TRUE(std::has_single_bit(gran)); // power of two
}

TEST(memory_mapping_ops, virtual_alloc_readonly) {
  auto const& ops = get_native_memory_mapping_ops();

  static constexpr size_t kSize{256_KiB};
  std::error_code ec;

  auto const gran = ops.granularity();

  void* const p = ops.virtual_alloc(kSize, memory_access::readonly, ec);
  EXPECT_NO_ERROR(ec);
  EXPECT_NE(p, nullptr);

  auto const addr = reinterpret_cast<uintptr_t>(p);
  EXPECT_EQ(addr % gran, 0u);

  // check that the memory is completely zeroed
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(reinterpret_cast<uint8_t*>(p)[i], 0u);
  }

  ops.virtual_free(p, kSize, ec);
  EXPECT_NO_ERROR(ec);
}

TEST(memory_mapping_ops, virtual_alloc_readwrite) {
  auto const& ops = get_native_memory_mapping_ops();

  static constexpr size_t kSize{97_KiB};
  std::error_code ec;

  auto const gran = ops.granularity();

  void* const p = ops.virtual_alloc(kSize, memory_access::readwrite, ec);
  EXPECT_NO_ERROR(ec);
  EXPECT_NE(p, nullptr);

  auto const addr = reinterpret_cast<uintptr_t>(p);
  EXPECT_EQ(addr % gran, 0u);

  // check that the memory is completely zeroed
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(reinterpret_cast<uint8_t*>(p)[i], 0u);
  }

  // write some data
  for (size_t i = 0; i < kSize; ++i) {
    reinterpret_cast<uint8_t*>(p)[i] = static_cast<uint8_t>(i);
  }

  // check the data
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(reinterpret_cast<uint8_t*>(p)[i], static_cast<uint8_t>(i));
  }

  ops.virtual_free(p, kSize, ec);
  EXPECT_NO_ERROR(ec);
}

TEST(memory_mapping_ops, virtual_alloc_too_large) {
  auto const& ops = get_native_memory_mapping_ops();

  std::error_code ec;
  void* const p = ops.virtual_alloc(std::numeric_limits<size_t>::max(),
                                    memory_access::readwrite, ec);
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (ENOMEM), (ENOMEM), (EINVAL),
                             (ERROR_INVALID_PARAMETER));
  EXPECT_EQ(p, nullptr);
}

TEST(memory_mapping_ops, virtual_free_bad_ptr) {
  auto const& ops = get_native_memory_mapping_ops();

  std::error_code ec;
  ops.virtual_free(kBadPtr, 4096, ec);
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EINVAL), (EINVAL), (EINVAL),
                             (ERROR_INVALID_PARAMETER));
}

TEST(memory_mapping_ops, unmap_bad_ptr) {
  auto const& ops = get_native_memory_mapping_ops();

  std::error_code ec;
  ops.unmap(kBadPtr, 4096, ec);
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EINVAL), (EINVAL), (EINVAL),
                             (ERROR_INVALID_ADDRESS));
}

TEST(memory_mapping_ops, lock_bad_ptr) {
#if DWARFS_TEST_RUNNING_ON_ASAN || DWARFS_TEST_RUNNING_ON_TSAN
  GTEST_SKIP() << "bad pointer test won't fail with ASAN/TSAN";
#else
  auto const& ops = get_native_memory_mapping_ops();
  std::error_code ec;
  ops.lock(kBadPtr, 4096, ec);
  if (ec == std::errc::operation_not_permitted) {
    GTEST_SKIP() << "mlock not permitted";
  } else if (ec == test::kMlockQuotaError) {
    GTEST_SKIP() << "mlock quota exceeded";
  }
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (ENOMEM, EINVAL), (ENOMEM), (EINVAL),
                             (ERROR_INVALID_PARAMETER));
#endif
}

#ifndef _WIN32
// advice() isn't currently implemented on Windows
TEST(memory_mapping_ops, advise_bad_ptr) {
  auto const& ops = get_native_memory_mapping_ops();

  std::error_code ec;
  ops.advise(kBadPtr, 4096, io_advice::normal, ec);
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EINVAL), (EINVAL), (EINVAL),
                             (ERROR_INVALID_PARAMETER));
}
#endif

TEST(memory_mapping_ops, virtual_alloc_advise) {
  auto const& ops = get_native_memory_mapping_ops();

  static constexpr size_t kSize{128_KiB};
  std::error_code ec;

  void* const p = ops.virtual_alloc(kSize, memory_access::readwrite, ec);
  EXPECT_NO_ERROR(ec);
  EXPECT_NE(p, nullptr);

  ops.advise(p, kSize, io_advice::normal, ec);
  EXPECT_NO_ERROR(ec);

  ops.advise(p, kSize, io_advice::sequential, ec);
  EXPECT_NO_ERROR(ec);

  ops.advise(p, kSize, io_advice::random, ec);
  EXPECT_NO_ERROR(ec);

  ops.advise(p, kSize, io_advice::willneed, ec);
  EXPECT_NO_ERROR(ec);

  ops.advise(p, kSize, io_advice::dontneed, ec);
  EXPECT_NO_ERROR(ec);

  ops.virtual_free(p, kSize, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, open_size_close) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  EXPECT_NO_ERROR(ec);

  auto const size = ops.size(h, ec);
  EXPECT_NO_ERROR(ec);

  EXPECT_EQ(size, 13);

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, open_non_existing_file) {
  auto const p = td->path() / "non-existing-file.dat";

  std::error_code ec;
  auto h = ops.open(p, ec);

  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (ENOENT), (ENOENT), (ENOENT),
                             (ERROR_FILE_NOT_FOUND));
}

TEST_F(memory_mapping_ops_test, pread) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  EXPECT_NO_ERROR(ec);

  char buf[6] = {};
  auto n = ops.pread(h, buf, 5, 7, ec);
  EXPECT_NO_ERROR(ec);

  EXPECT_EQ(n, 5);
  EXPECT_STREQ(buf, "World");

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, pread_beyond_eof) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  EXPECT_NO_ERROR(ec);

  char buf[6] = {};
  auto n = ops.pread(h, buf, 5, 20, ec);
  EXPECT_NO_ERROR(ec);

  EXPECT_EQ(n, 0);
  EXPECT_STREQ(buf, "");

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, pread_bad_ptr) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  EXPECT_NO_ERROR(ec);

  auto n = ops.pread(h, nullptr, 5, 7, ec);
  EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EFAULT), (EFAULT), (EFAULT),
                             (ERROR_NOACCESS));
  EXPECT_EQ(n, 0);

  ec.clear();

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, map_readonly) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  ASSERT_NO_ERROR(ec);

  auto const gran = ops.granularity();

  void* const m = ops.map(h, 0, 13, ec);
  ASSERT_NO_ERROR(ec);
  EXPECT_NE(m, nullptr);

  auto const addr = reinterpret_cast<uintptr_t>(m);
  EXPECT_EQ(addr % gran, 0u);

  EXPECT_EQ(std::string_view(static_cast<char const*>(m) + 7, 5), "World");

  ops.unmap(m, 5, ec);
  EXPECT_NO_ERROR(ec);

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, map_errors) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  EXPECT_NO_ERROR(ec);

#ifndef __FreeBSD__
  // TODO: not totally sure what FreeBSD is doing here...
  {
    // mapping beyond EOF
    auto const m = ops.map(h, 20, 5, ec);
    EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EINVAL), (EINVAL), (EINVAL),
                               (ERROR_MAPPED_ALIGNMENT));
    EXPECT_EQ(m, nullptr);
    ec.clear();
  }
#endif

#ifndef _WIN32
  {
    // mapping with zero length
    auto const m = ops.map(h, 0, 0, ec);
    EXPECT_EC_UNIX_BSD_MAC_WIN(ec, (EINVAL), (EINVAL), (EINVAL),
                               (ERROR_INVALID_PARAMETER));
    EXPECT_EQ(m, nullptr);
    ec.clear();
  }
#endif

  {
    // negative offset
    auto const m = ops.map(h, -1, 5, ec);
    EXPECT_EQ(ec, std::errc::invalid_argument);
    EXPECT_EQ(m, nullptr);
    ec.clear();
  }

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

TEST_F(memory_mapping_ops_test, lock_mapping) {
  auto const p = td->path() / "file.dat";
  write_file(p, "Hello, World!");

  std::error_code ec;
  auto h = ops.open(p, ec);
  ASSERT_NO_ERROR(ec);

  void* const m = ops.map(h, 0, 13, ec);
  ASSERT_NO_ERROR(ec);
  EXPECT_NE(m, nullptr);

  ops.lock(m, 13, ec);
  if (ec == std::errc::operation_not_permitted) {
    GTEST_SKIP() << "mlock not permitted";
  } else if (ec == test::kMlockQuotaError) {
    GTEST_SKIP() << "mlock quota exceeded";
  }
  EXPECT_NO_ERROR(ec);

  ops.unmap(m, 13, ec);
  EXPECT_NO_ERROR(ec);

  ops.close(h, ec);
  EXPECT_NO_ERROR(ec);
}

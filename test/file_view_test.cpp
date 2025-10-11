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
#include <string>
#include <unordered_map>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/file_util.h>
#include <dwarfs/file_view.h>

#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/internal/memory_mapping_ops.h>
#include <dwarfs/internal/mmap_file_view.h>

#include "mmap_mock.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using namespace std::string_literals;
namespace fs = std::filesystem;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

namespace {

class fake_mm_ops_lowlevel {
 public:
  struct fake_handle {
    fs::path path;
    file_size_t size = 0;
    std::vector<dwarfs::detail::file_extent_info> extents;
  };

  using handle_type = std::shared_ptr<fake_handle>;

  explicit fake_mm_ops_lowlevel(size_t gran)
      : granularity_(gran) {}

  handle_type
  add_file(fs::path p, file_size_t sz,
           std::vector<dwarfs::detail::file_extent_info> exts = {}) {
    auto h = std::make_shared<fake_handle>();
    h->path = std::move(p);
    h->size = sz;
    if (exts.empty()) {
      exts.push_back({extent_kind::data, {0, sz}});
    }
    h->extents = std::move(exts);
    files_[h->path] = h;
    return h;
  }

  handle_type open(fs::path const& path, std::error_code& ec) const {
    ec.clear();
    auto it = files_.find(path);
    if (it == files_.end()) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return {};
    }
    return it->second;
  }

  void close(handle_type handle, std::error_code& ec) const {
    static_cast<void>(check_handle(handle, ec));
  }

  file_size_t size(handle_type handle, std::error_code& ec) const {
    if (auto h = check_handle(handle, ec)) {
      return h->size;
    }
    return 0;
  }

  size_t granularity() const { return granularity_; }

  std::vector<dwarfs::detail::file_extent_info>
  get_extents(handle_type handle, std::error_code& ec) const {
    if (auto h = check_handle(handle, ec)) {
      return h->extents;
    }
    return {};
  }

  size_t pread(handle_type /*handle*/, void* /*buf*/, size_t /*size*/,
               file_off_t /*offset*/, std::error_code& /*ec*/) const {
    std::abort(); // not implemented
  }

  void* virtual_alloc(size_t size, internal::memory_access /*access*/,
                      std::error_code& ec) const {
    ec.clear();
    return ::operator new(size, std::nothrow);
  }

  void virtual_free(void* addr, size_t /*size*/, std::error_code& ec) const {
    ec.clear();
    ::operator delete(addr);
  }

  void* map(handle_type handle, file_off_t /*offset*/, size_t size,
            std::error_code& ec) const {
    if (check_handle(handle, ec)) {
      return ::operator new(size, std::nothrow);
    }
    return nullptr;
  }

  void unmap(void* addr, size_t, std::error_code& ec) const {
    ec.clear();
    ::operator delete(addr);
  }

  void advise(void* /*addr*/, size_t /*size*/, io_advice /*advice*/,
              std::error_code& /*ec*/) const {}

  void lock(void* /*addr*/, size_t /*size*/, std::error_code& /*ec*/) const {}

 private:
  handle_type check_handle(handle_type handle, std::error_code& ec) const {
    if (handle) {
      ec.clear();
    } else {
      ec = std::make_error_code(std::errc::bad_file_descriptor);
    }
    return handle;
  }

  size_t granularity_;
  std::unordered_map<fs::path, handle_type> files_;
};

class mm_ops_lowlevel_mock {
 public:
  using handle_type = fake_mm_ops_lowlevel::handle_type;

  MOCK_METHOD(handle_type, open, (fs::path const&, std::error_code&), (const));
  MOCK_METHOD(void, close, (handle_type const&, std::error_code&), (const));
  MOCK_METHOD(file_size_t, size, (handle_type const&, std::error_code&),
              (const));
  MOCK_METHOD(size_t, granularity, (), (const));
  MOCK_METHOD(std::vector<dwarfs::detail::file_extent_info>, get_extents,
              (handle_type const&, std::error_code&), (const));
  MOCK_METHOD(size_t, pread,
              (handle_type const&, void*, size_t, file_off_t, std::error_code&),
              (const));
  MOCK_METHOD(void*, virtual_alloc,
              (size_t, internal::memory_access, std::error_code&), (const));
  MOCK_METHOD(void, virtual_free, (void*, size_t, std::error_code&), (const));
  MOCK_METHOD(void*, map,
              (handle_type const&, file_off_t, size_t, std::error_code&),
              (const));
  MOCK_METHOD(void, unmap, (void*, size_t, std::error_code&), (const));
  MOCK_METHOD(void, advise, (void*, size_t, io_advice, std::error_code&),
              (const));
  MOCK_METHOD(void, lock, (void*, size_t, std::error_code&), (const));

  void delegate_to(fake_mm_ops_lowlevel* fake) {
    using ::testing::_;
    using ::testing::Invoke;

    ON_CALL(*this, open)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::open));
    ON_CALL(*this, close)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::close));
    ON_CALL(*this, size)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::size));
    ON_CALL(*this, granularity)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::granularity));
    ON_CALL(*this, get_extents)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::get_extents));
    ON_CALL(*this, pread)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::pread));
    ON_CALL(*this, virtual_alloc)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::virtual_alloc));
    ON_CALL(*this, virtual_free)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::virtual_free));
    ON_CALL(*this, map).WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::map));
    ON_CALL(*this, unmap)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::unmap));
    ON_CALL(*this, advise)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::advise));
    ON_CALL(*this, lock)
        .WillByDefault(Invoke(fake, &fake_mm_ops_lowlevel::lock));
  }
};

class fake_mm_ops_adapter : public internal::memory_mapping_ops {
 public:
  explicit fake_mm_ops_adapter(mm_ops_lowlevel_mock& ll)
      : ll_{ll} {}

  std::any open(fs::path const& path, std::error_code& ec) const override {
    return {ll_.open(path, ec)};
  }

  void close(std::any const& handle, std::error_code& ec) const override {
    ll_.close(get_handle(handle), ec);
  }

  file_size_t size(std::any const& handle, std::error_code& ec) const override {
    return ll_.size(get_handle(handle), ec);
  }

  size_t granularity() const override { return ll_.granularity(); }

  std::vector<dwarfs::detail::file_extent_info>
  get_extents(std::any const& handle, std::error_code& ec) const override {
    return ll_.get_extents(get_handle(handle), ec);
  }

  size_t pread(std::any const& handle, void* buf, size_t size,
               file_off_t offset, std::error_code& ec) const override {
    return ll_.pread(get_handle(handle), buf, size, offset, ec);
  }

  void* virtual_alloc(size_t size, internal::memory_access access,
                      std::error_code& ec) const override {
    return ll_.virtual_alloc(size, access, ec);
  }

  void
  virtual_free(void* addr, size_t size, std::error_code& ec) const override {
    ll_.virtual_free(addr, size, ec);
  }

  void* map(std::any const& handle, file_off_t offset, size_t size,
            std::error_code& ec) const override {
    return ll_.map(get_handle(handle), offset, size, ec);
  }

  void unmap(void* addr, size_t size, std::error_code& ec) const override {
    ll_.unmap(addr, size, ec);
  }

  void advise(void* addr, size_t size, io_advice advice,
              std::error_code& ec) const override {
    ll_.advise(addr, size, advice, ec);
  }

  void lock(void* addr, size_t size, std::error_code& ec) const override {
    ll_.lock(addr, size, ec);
  }

 private:
  std::shared_ptr<fake_mm_ops_lowlevel::fake_handle>
  get_handle(std::any const& handle) const {
    auto h = std::any_cast<std::shared_ptr<fake_mm_ops_lowlevel::fake_handle>>(
        &handle);
    return h ? *h : nullptr;
  }

  mm_ops_lowlevel_mock& ll_;
};

constexpr auto align_down(file_off_t x, size_t a) -> file_off_t {
  return (x / a) * a;
}

} // namespace

TEST(mock_file_view, basic) {
  auto view = test::make_mock_file_view(
      "Hello, World!"s,
      test::mock_file_view_options{.support_raw_bytes = true});

  {
    std::vector<std::string> parts;
    std::vector<file_off_t> offsets;
    std::vector<file_size_t> sizes;

    for (auto const& ext : view.extents()) {
      for (auto const& seg : ext.segments(4)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        offsets.push_back(seg.offset());
        sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(parts, testing::ElementsAre("Hell", "o, W", "orld", "!"));
    EXPECT_THAT(offsets, testing::ElementsAre(0, 4, 8, 12));
    EXPECT_THAT(sizes, testing::ElementsAre(4, 4, 4, 1));
  }

  {
    std::vector<std::string> parts;
    std::vector<file_off_t> offsets;
    std::vector<file_size_t> sizes;

    for (auto const& ext : view.extents()) {
      for (auto const& seg : ext.segments(4, 1)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        offsets.push_back(seg.offset());
        sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(parts, testing::ElementsAre("Hell", "lo, ", " Wor", "rld!"));
    EXPECT_THAT(offsets, testing::ElementsAre(0, 3, 6, 9));
    EXPECT_THAT(sizes, testing::ElementsAre(4, 4, 4, 4));
  }

  {
    std::vector<std::string> parts;
    std::vector<file_off_t> offsets;
    std::vector<file_size_t> sizes;

    for (auto const& seg : view.segments({2, 9}, 4, 1)) {
      auto span = seg.span<char>();
      parts.emplace_back(span.begin(), span.end());
      offsets.push_back(seg.offset());
      sizes.push_back(seg.size());
    }

    EXPECT_THAT(parts, testing::ElementsAre("llo,", ", Wo", "orl"));
    EXPECT_THAT(offsets, testing::ElementsAre(2, 5, 8));
    EXPECT_THAT(sizes, testing::ElementsAre(4, 4, 3));
  }

  ASSERT_TRUE(view.supports_raw_bytes());

  auto raw = view.raw_bytes();
  std::string raw_str(raw.size(), '\0');
  std::ranges::transform(raw, raw_str.begin(),
                         [](auto b) { return static_cast<char>(b); });

  EXPECT_EQ(raw_str, "Hello, World!");
}

TEST(mock_file_view, extents) {
  auto view = test::make_mock_file_view("Hello,\0\0\0\0World!"s,
                                        {
                                            {extent_kind::data, {0, 6}},
                                            {extent_kind::hole, {6, 4}},
                                            {extent_kind::data, {10, 6}},
                                        });

  {
    std::vector<std::vector<std::string>> extent_parts;
    std::vector<file_off_t> extent_offsets;
    std::vector<file_size_t> extent_sizes;
    std::vector<file_off_t> segment_offsets;
    std::vector<file_size_t> segment_sizes;

    for (auto const& ext : view.extents()) {
      auto& parts = extent_parts.emplace_back();
      extent_offsets.push_back(ext.offset());
      extent_sizes.push_back(ext.size());
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        segment_offsets.push_back(seg.offset());
        segment_sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("Hel"s, "lo,"s),
                                     testing::ElementsAre("\0\0\0"s, "\0"s),
                                     testing::ElementsAre("Wor"s, "ld!"s)));
    EXPECT_THAT(extent_offsets, testing::ElementsAre(0, 6, 10));
    EXPECT_THAT(extent_sizes, testing::ElementsAre(6, 4, 6));
    EXPECT_THAT(segment_offsets, testing::ElementsAre(0, 3, 6, 9, 10, 13));
    EXPECT_THAT(segment_sizes, testing::ElementsAre(3, 3, 3, 1, 3, 3));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;
    std::vector<file_off_t> extent_offsets;
    std::vector<file_size_t> extent_sizes;
    std::vector<file_off_t> segment_offsets;
    std::vector<file_size_t> segment_sizes;

    for (auto const& ext : view.extents({4, 10})) {
      auto& parts = extent_parts.emplace_back();
      extent_offsets.push_back(ext.offset());
      extent_sizes.push_back(ext.size());
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        segment_offsets.push_back(seg.offset());
        segment_sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("o,"s),
                                     testing::ElementsAre("\0\0\0"s, "\0"s),
                                     testing::ElementsAre("Wor"s, "l"s)));
    EXPECT_THAT(extent_offsets, testing::ElementsAre(4, 6, 10));
    EXPECT_THAT(extent_sizes, testing::ElementsAre(2, 4, 4));
    EXPECT_THAT(segment_offsets, testing::ElementsAre(4, 6, 9, 10, 13));
    EXPECT_THAT(segment_sizes, testing::ElementsAre(2, 3, 1, 3, 1));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;
    std::vector<file_off_t> extent_offsets;
    std::vector<file_size_t> extent_sizes;
    std::vector<file_off_t> segment_offsets;
    std::vector<file_size_t> segment_sizes;

    for (auto const& ext : view.extents({1, 4})) {
      auto& parts = extent_parts.emplace_back();
      extent_offsets.push_back(ext.offset());
      extent_sizes.push_back(ext.size());
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        segment_offsets.push_back(seg.offset());
        segment_sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("ell"s, "o"s)));
    EXPECT_THAT(extent_offsets, testing::ElementsAre(1));
    EXPECT_THAT(extent_sizes, testing::ElementsAre(4));
    EXPECT_THAT(segment_offsets, testing::ElementsAre(1, 4));
    EXPECT_THAT(segment_sizes, testing::ElementsAre(3, 1));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;
    std::vector<file_off_t> extent_offsets;
    std::vector<file_size_t> extent_sizes;
    std::vector<file_off_t> segment_offsets;
    std::vector<file_size_t> segment_sizes;

    for (auto const& ext : view.extents({9, 2})) {
      auto& parts = extent_parts.emplace_back();
      extent_offsets.push_back(ext.offset());
      extent_sizes.push_back(ext.size());
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        segment_offsets.push_back(seg.offset());
        segment_sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(extent_parts, testing::ElementsAre(testing::ElementsAre("\0"s),
                                                   testing::ElementsAre("W"s)));
    EXPECT_THAT(extent_offsets, testing::ElementsAre(9, 10));
    EXPECT_THAT(extent_sizes, testing::ElementsAre(1, 1));
    EXPECT_THAT(segment_offsets, testing::ElementsAre(9, 10));
    EXPECT_THAT(segment_sizes, testing::ElementsAre(1, 1));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;
    std::vector<file_off_t> extent_offsets;
    std::vector<file_size_t> extent_sizes;
    std::vector<file_off_t> segment_offsets;
    std::vector<file_size_t> segment_sizes;

    for (auto const& ext : view.extents({2, 4})) {
      auto& parts = extent_parts.emplace_back();
      extent_offsets.push_back(ext.offset());
      extent_sizes.push_back(ext.size());
      for (auto const& seg : ext.segments(3, 1)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
        segment_offsets.push_back(seg.offset());
        segment_sizes.push_back(seg.size());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("llo"s, "o,"s)));
    EXPECT_THAT(extent_offsets, testing::ElementsAre(2));
    EXPECT_THAT(extent_sizes, testing::ElementsAre(4));
    EXPECT_THAT(segment_offsets, testing::ElementsAre(2, 4));
    EXPECT_THAT(segment_sizes, testing::ElementsAre(3, 2));
  }
}

TEST(mock_file_view, extents_raw_bytes) {
  auto view = test::make_mock_file_view("Hello,\0\0\0\0World!"s,
                                        {
                                            {extent_kind::data, {0, 6}},
                                            {extent_kind::hole, {6, 4}},
                                            {extent_kind::data, {10, 6}},
                                        },
                                        {.support_raw_bytes = true});

  {
    std::vector<std::string> extents;

    for (auto const& ext : view.extents({2, 11})) {
      auto raw = ext.raw_bytes();
      auto& dest = extents.emplace_back();
      dest.resize(raw.size());
      std::ranges::transform(raw, dest.begin(),
                             [](auto b) { return static_cast<char>(b); });
    }

    EXPECT_THAT(extents, testing::ElementsAre("llo,"s, "\0\0\0\0"s, "Wor"s));
  }
}

TEST(mock_file_view, test_file_data) {
  test::test_file_data data;
  data.add_data("Hello,"s);
  data.add_hole(4);
  data.add_data("World!"s);

  auto view = test::make_mock_file_view(data);

  EXPECT_FALSE(view.supports_raw_bytes());
  EXPECT_EQ(view.size(), 16);

  {
    std::vector<std::string> extents;

    for (auto const& ext : view.extents({2, 11})) {
      EXPECT_FALSE(ext.supports_raw_bytes());
      EXPECT_GT(ext.size(), 0);

      auto& buf = extents.emplace_back();

      for (auto const& seg : ext.segments(2)) {
        auto span = seg.span<char>();
        buf.append(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extents, testing::ElementsAre("llo,"s, "\0\0\0\0"s, "Wor"s));
  }

  EXPECT_EQ(view.read_string(0, view.size()), "Hello,\0\0\0\0World!"s);
  EXPECT_EQ(view.read_string(2, 11), "llo,\0\0\0\0Wor"s);
  EXPECT_EQ(view.read_string(6, 4), "\0\0\0\0"s);
  EXPECT_EQ(view.read_string(15, 1), "!"s);
}

TEST(mock_file_view, random_test_file_data) {
  test::test_file_data data;
  std::string ref_data(10200, '\0');
  std::mt19937_64 rng{42};

  for (int i = 0; i < 100; ++i) {
    data.add_hole(i + 1);
    data.add_data(100 - i, &rng);
  }
  data.add_hole(100);

  for (auto const& ext : data.extents) {
    if (ext.info.kind == extent_kind::data) {
      std::copy_n(ext.data.begin(), ext.data.size(),
                  ref_data.begin() + ext.info.range.offset());
    }
  }

  EXPECT_LT(std::ranges::count(ref_data, '\0'), 6000);

  auto view = test::make_mock_file_view(data);

  EXPECT_FALSE(view.supports_raw_bytes());
  EXPECT_EQ(view.size(), 10200);

  for (file_size_t window_size : {1, 2, 5, 13, 64, 711}) {
    auto const last_offset = view.size() - window_size;

    for (file_off_t offset = 0; offset <= last_offset; ++offset) {
      EXPECT_EQ(view.read_string(offset, window_size),
                ref_data.substr(offset, window_size));

      std::string buf;

      for (auto const& ext : view.extents({offset, window_size})) {
        EXPECT_GT(ext.size(), 0);

        for (auto const& seg : ext.segments(27)) {
          auto span = seg.span<char>();
          buf.append(span.begin(), span.end());
        }
      }

      EXPECT_EQ(buf, ref_data.substr(offset, window_size));
    }
  }
}

TEST(mmap_file_view, basic) {
  temporary_directory td;

  auto path = td.path() / "testfile";

  write_file(path, "Hello, World!");

  auto const& ops = internal::get_native_memory_mapping_ops();
  auto mm = internal::create_mmap_file_view(ops, path);

  EXPECT_TRUE(mm);
  EXPECT_TRUE(mm.valid());

  EXPECT_EQ(mm.size(), 13);
  EXPECT_EQ(mm.path(), path);

  auto const range = mm.range();

  EXPECT_EQ(range.offset(), 0);
  EXPECT_EQ(range.size(), 13);

  EXPECT_TRUE(mm.supports_raw_bytes());
  {
    auto const data = mm.raw_bytes<char>();
    EXPECT_EQ(std::string_view(data.data(), data.size()), "Hello, World!"s);
  }

  {
    auto const data = mm.read_string(1, 10);
    EXPECT_EQ(data, "ello, Worl"s);
  }

  {
    auto const data = mm.read_string(5, 0);
    EXPECT_EQ(data, ""s);
  }

  EXPECT_THAT(([&] {
                std::array<char, 8> buf;
                mm.copy_to(buf, 10, 10);
              }),
              ::testing::Throws<std::system_error>(::testing::Property(
                  &std::system_error::code, std::errc::result_out_of_range)));

  {
    std::array<char, 8> buf;
    EXPECT_NO_THROW(mm.copy_to(buf, 2));
    EXPECT_EQ(std::string_view(buf.data(), buf.size()), "llo, Wor"s);
  }

  {
    auto const buf = mm.read<std::array<char, 7>>(3);
    EXPECT_EQ(std::string_view(buf.data(), buf.size()), "lo, Wor"s);
  }

  {
    auto seg = mm.segment_at(20, 10);
    EXPECT_FALSE(seg.valid());
  }
}

TEST(mmap_file_view, ref_segment) {
  temporary_directory td;

  auto path = td.path() / "testfile";

  write_file(path, "Hello, World!");

  auto const& ops = internal::get_native_memory_mapping_ops();
  auto mm = internal::create_mmap_file_view(
      ops, path, {.max_eager_map_size = std::nullopt});

  EXPECT_TRUE(mm);
  EXPECT_TRUE(mm.valid());
  EXPECT_TRUE(mm.supports_raw_bytes());

  auto seg = mm.segment_at(2, 10);

  EXPECT_EQ(seg.range(), file_range(2, 10));
  EXPECT_EQ(seg.offset(), 2);
  EXPECT_EQ(seg.size(), 10);
  EXPECT_FALSE(seg.is_zero());

  {
    auto const data = seg.span<char>();
    EXPECT_EQ(std::string_view(data.data(), data.size()), "llo, World"s);
  }
}

TEST(mmap_file_view, mapped_segment) {
  temporary_directory td;

  auto path = td.path() / "testfile";

  write_file(path, "Hello, World!");

  auto const& ops = internal::get_native_memory_mapping_ops();
  auto mm =
      internal::create_mmap_file_view(ops, path, {.max_eager_map_size = 1});

  EXPECT_TRUE(mm);
  EXPECT_TRUE(mm.valid());
  EXPECT_FALSE(mm.supports_raw_bytes());

  auto seg = mm.segment_at(2, 10);

  EXPECT_EQ(seg.range(), file_range(2, 10));
  EXPECT_EQ(seg.offset(), 2);
  EXPECT_EQ(seg.size(), 10);
  EXPECT_FALSE(seg.is_zero());

  {
    auto const data = seg.span<char>();
    EXPECT_EQ(std::string_view(data.data(), data.size()), "llo, World"s);
  }
}

TEST(mmap_file_view, memory_ops_mapped_segment) {
  static constexpr size_t kGran{4096};
  static constexpr size_t kFileSize{1_MiB};
  fake_mm_ops_lowlevel fake(kGran);
  StrictMock<mm_ops_lowlevel_mock> mock_ops;
  mock_ops.delegate_to(&fake);
  fake_mm_ops_adapter ops{mock_ops};

  fs::path const path{"/tmp/testfile"};
  auto handle = fake.add_file(path, kFileSize);

  static constexpr file_off_t kWantOffset{12345};
  static constexpr size_t kWantSize{7000};
  static constexpr file_off_t kReleaseOffset{19000};

  static constexpr file_off_t kExpectedMapBase = align_down(kWantOffset, kGran);
  static constexpr file_off_t kMisalignment = kWantOffset - kExpectedMapBase;
  static constexpr size_t kExpectedMapLength = kWantSize + kMisalignment;

  EXPECT_CALL(mock_ops, open(path, _)).Times(1);
  EXPECT_CALL(mock_ops, size(handle, _)).Times(1);
  EXPECT_CALL(mock_ops, get_extents(handle, _)).Times(1);

  internal::mmap_file_view_options const opts{
      .max_eager_map_size = 0,
  };

  auto fv = create_mmap_file_view(ops, path, opts);

  void* mapped_base{nullptr};

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, granularity()).Times(1);

  EXPECT_CALL(mock_ops, map(handle, kExpectedMapBase, kExpectedMapLength, _))
      .WillOnce(Invoke([&](fake_mm_ops_lowlevel::handle_type const& h,
                           file_off_t off, size_t sz, std::error_code& ec) {
        void* p = fake.map(h, off, sz, ec);
        mapped_base = p;
        return p;
      }));

  auto seg = fv.segment_at(kWantOffset, kWantSize);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops,
              advise(mapped_base, kExpectedMapLength, io_advice::sequential, _))
      .Times(1);

  seg.advise(io_advice::sequential);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(
      mock_ops,
      lock(static_cast<std::byte*>(mapped_base) + kMisalignment, kWantSize, _))
      .Times(1);

  seg.lock();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  // The following should be a no-op since the file_view doesn't
  // actually own the mapping

  std::error_code ec;
  fv.release_until(kReleaseOffset, ec);
  EXPECT_FALSE(ec);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, unmap(mapped_base, kExpectedMapLength, _)).Times(1);

  seg.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, close(handle, _)).Times(1);

  fv.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);
}

TEST(mmap_file_view, memory_ops_ref_segment) {
  static constexpr size_t kGran{4096};
  static constexpr size_t kFileSize{256_KiB};
  fake_mm_ops_lowlevel fake(kGran);
  StrictMock<mm_ops_lowlevel_mock> mock_ops;
  mock_ops.delegate_to(&fake);
  fake_mm_ops_adapter ops{mock_ops};

  fs::path const path{"/tmp/testfile"};
  auto handle = fake.add_file(path, kFileSize);

  static constexpr file_off_t kWantOffset{12345};
  static constexpr size_t kWantSize{7000};
  static constexpr file_off_t kReleaseOffset{19000};

  static constexpr file_off_t kExpectedMapBase = align_down(kWantOffset, kGran);
  static constexpr size_t kExpectedAdviseLength =
      kWantSize + (kWantOffset - kExpectedMapBase);

  EXPECT_CALL(mock_ops, open(path, _)).Times(1);

  EXPECT_CALL(mock_ops, size(handle, _)).Times(1);

  EXPECT_CALL(mock_ops, granularity()).Times(1);

  EXPECT_CALL(mock_ops, get_extents(handle, _)).Times(1);

  void* mapped_base{nullptr};

  EXPECT_CALL(mock_ops, map(handle, 0, kFileSize, _))
      .WillOnce(Invoke([&](fake_mm_ops_lowlevel::handle_type const& h,
                           file_off_t off, size_t sz, std::error_code& ec) {
        void* p = fake.map(h, off, sz, ec);
        mapped_base = p;
        return p;
      }));

  internal::mmap_file_view_options const opts{
      .max_eager_map_size = 1_MiB, // map the whole test file eagerly
  };

  auto fv = create_mmap_file_view(ops, path, opts);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  // Getting a segment is expected to not trigger any further mapping calls,
  // since it's just referencing the mapping owned by the file_view
  auto seg = fv.segment_at(kWantOffset, kWantSize);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops,
              advise(static_cast<std::byte*>(mapped_base) + kExpectedMapBase,
                     kExpectedAdviseLength, io_advice::sequential, _))
      .Times(1);

  seg.advise(io_advice::sequential);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, lock(static_cast<std::byte*>(mapped_base) + kWantOffset,
                             kWantSize, _))
      .Times(1);

  seg.lock();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops,
              advise(static_cast<std::byte*>(mapped_base) + kExpectedMapBase,
                     kExpectedAdviseLength, io_advice::dontneed, _))
      .Times(1);

  seg.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, advise(mapped_base, align_down(kReleaseOffset, kGran),
                               io_advice::dontneed, _))
      .Times(1);

  std::error_code ec;
  fv.release_until(kReleaseOffset, ec);
  EXPECT_FALSE(ec);

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, unmap(mapped_base, kFileSize, _)).Times(1);
  EXPECT_CALL(mock_ops, close(handle, _)).Times(1);

  fv.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);
}

TEST(mappable_file, virtual_alloc_free) {
  static constexpr size_t kGran{4096};
  fake_mm_ops_lowlevel fake(kGran);
  StrictMock<mm_ops_lowlevel_mock> mock_ops;
  mock_ops.delegate_to(&fake);
  fake_mm_ops_adapter ops{mock_ops};

  static constexpr size_t kAllocSize{10000};

  void* allocated_ptr{nullptr};

  EXPECT_CALL(mock_ops, granularity()).Times(1);

  EXPECT_CALL(mock_ops,
              virtual_alloc(kAllocSize, internal::memory_access::readwrite, _))
      .WillOnce(Invoke(
          [&](size_t sz, internal::memory_access acc, std::error_code& ec) {
            void* p = fake.virtual_alloc(sz, acc, ec);
            allocated_ptr = p;
            return p;
          }));

  auto mapping = internal::mappable_file::map_empty(ops, kAllocSize);

  EXPECT_TRUE(mapping.valid());

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, virtual_free(allocated_ptr, kAllocSize, _))
      .WillOnce(Invoke([&](void* p, size_t sz, std::error_code& ec) {
        EXPECT_EQ(p, allocated_ptr);
        EXPECT_EQ(sz, kAllocSize);
        fake.virtual_free(p, sz, ec);
      }));

  mapping.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);
}

TEST(mappable_file, virtual_alloc_free_readonly) {
  static constexpr size_t kGran{4096};
  fake_mm_ops_lowlevel fake(kGran);
  StrictMock<mm_ops_lowlevel_mock> mock_ops;
  mock_ops.delegate_to(&fake);
  fake_mm_ops_adapter ops{mock_ops};

  static constexpr size_t kAllocSize{10000};

  void* allocated_ptr{nullptr};

  EXPECT_CALL(mock_ops, granularity()).Times(1);

  EXPECT_CALL(mock_ops,
              virtual_alloc(kAllocSize, internal::memory_access::readonly, _))
      .WillOnce(Invoke(
          [&](size_t sz, internal::memory_access acc, std::error_code& ec) {
            void* p = fake.virtual_alloc(sz, acc, ec);
            allocated_ptr = p;
            return p;
          }));

  auto mapping = internal::mappable_file::map_empty_readonly(ops, kAllocSize);

  EXPECT_TRUE(mapping.valid());

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);

  EXPECT_CALL(mock_ops, virtual_free(allocated_ptr, kAllocSize, _))
      .WillOnce(Invoke([&](void* p, size_t sz, std::error_code& ec) {
        EXPECT_EQ(p, allocated_ptr);
        EXPECT_EQ(sz, kAllocSize);
        fake.virtual_free(p, sz, ec);
      }));

  mapping.reset();

  ::testing::Mock::VerifyAndClearExpectations(&mock_ops);
}

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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/reader/internal/sparse_file_seeker.h>

using ::testing::Eq;
using ::testing::Ne;

using namespace dwarfs;

using dwarfs::reader::seek_whence;
using dwarfs::reader::internal::sparse_file_seeker;

namespace {

struct test_chunk {
  bool hole{};
  file_size_t n{};

  [[nodiscard]] bool is_hole() const { return hole; }
  [[nodiscard]] file_size_t size() const { return n; }
};

enum class seg { H, D }; // Hole / Data

[[nodiscard]] std::vector<test_chunk>
make_chunks(std::initializer_list<std::pair<seg, file_size_t>> spec) {
  std::vector<test_chunk> v;
  v.reserve(spec.size());
  for (auto [k, n] : spec) {
    v.push_back(test_chunk{.hole = (k == seg::H), .n = n});
  }
  return v;
}

struct seek_result {
  file_off_t ret{-1};
  std::error_code ec{};
};

[[nodiscard]] seek_result
call_static_seek(std::vector<test_chunk> const& chunks, file_off_t off,
                 seek_whence whence) {
  std::error_code ec;
  auto r = sparse_file_seeker::seek(chunks, off, whence, ec);
  return {r, ec};
}

} // namespace

// -----------------------------
// Basic sanity and error cases
// -----------------------------

TEST(sparse_file_seeker, negative_offset_is_enxio_static) {
  auto chunks = make_chunks({{seg::D, 10}});
  auto res = call_static_seek(chunks, -1, seek_whence::hole);
  EXPECT_EQ(res.ret, -1);
  EXPECT_EQ(res.ec, std::errc::no_such_device_or_address);
}

TEST(sparse_file_seeker, negative_offset_is_enxio_instance) {
  auto chunks = make_chunks({{seg::D, 10}});
  sparse_file_seeker s(chunks);
  std::error_code ec;
  auto r = s.seek(-1, seek_whence::hole, ec);
  EXPECT_EQ(r, -1);
  EXPECT_EQ(ec, std::errc::no_such_device_or_address);
}

TEST(sparse_file_seeker, offset_at_or_beyond_size_is_enxio_instance) {
  auto chunks = make_chunks({{seg::D, 10}});
  sparse_file_seeker s(chunks);
  std::error_code ec;

  auto r_eq = s.seek(10, seek_whence::hole, ec);
  EXPECT_EQ(r_eq, -1);
  EXPECT_EQ(ec, std::errc::no_such_device_or_address);

  ec.clear();
  auto r_gt = s.seek(11, seek_whence::data, ec);
  EXPECT_EQ(r_gt, -1);
  EXPECT_EQ(ec, std::errc::no_such_device_or_address);
}

TEST(sparse_file_seeker, offset_at_or_beyond_size_is_enxio_static) {
  auto chunks = make_chunks({{seg::D, 10}});

  {
    auto res = call_static_seek(chunks, 10, seek_whence::data);
    EXPECT_EQ(res.ret, -1);
    EXPECT_EQ(res.ec, std::errc::no_such_device_or_address);
  }

  {
    auto res = call_static_seek(chunks, 11, seek_whence::data);
    EXPECT_EQ(res.ret, -1);
    EXPECT_EQ(res.ec, std::errc::no_such_device_or_address);
  }
}

// ------------------------------------
// All data, no holes (single chunk)
// ------------------------------------

TEST(sparse_file_seeker, all_data_seek_hole_returns_eof) {
  auto chunks = make_chunks({{seg::D, 10}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 3, 9}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 10);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 10);
  }
}

TEST(sparse_file_seeker, all_data_seek_data_returns_same_offset) {
  auto chunks = make_chunks({{seg::D, 10}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 4, 9}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, off);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, off);
  }
}

// ------------------------------------
// One leading hole, then data
// ------------------------------------

TEST(sparse_file_seeker, leading_hole_seek_hole_stays_in_hole) {
  auto chunks = make_chunks({{seg::H, 5}, {seg::D, 10}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 2, 4}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, off);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, off);
  }

  for (file_off_t off : {0, 3, 4}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 5);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 5);
  }
}

TEST(sparse_file_seeker, at_hole_end_is_in_data) {
  auto chunks = make_chunks({{seg::H, 5}, {seg::D, 10}});
  sparse_file_seeker s(chunks);

  {
    auto s1 = call_static_seek(chunks, 5, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 15);

    std::error_code ec;
    auto r = s.seek(5, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 15);
  }
  {
    auto s1 = call_static_seek(chunks, 5, seek_whence::data);
    EXPECT_FALSE(s1.ec);

    std::error_code ec;
    auto r = s.seek(5, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 5);
  }
}

// -------------------------------------------------
// Alternating: data, hole, data (multi data chunks)
// -------------------------------------------------

TEST(sparse_file_seeker, data_then_hole_then_data_seek_hole_and_data) {
  // data [0..4), hole [4..7), data [7..20)
  auto chunks = make_chunks({{seg::D, 4}, {seg::H, 3}, {seg::D, 13}});

  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 2, 3}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 4);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 4);
  }

  for (file_off_t off : {4, 5, 6}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 7);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 7);
  }

  for (file_off_t off : {7, 10, 19}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 20);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 20);
  }

  for (file_off_t off : {0, 3, 7, 12, 19}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, off);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, off);
  }
}

// -------------------------------------------
// Trailing hole (no data after the last hole)
// -------------------------------------------

TEST(sparse_file_seeker, trailing_hole_seek_hole_and_seek_data) {
  // data [0..8), hole [8..12)
  auto chunks = make_chunks({{seg::D, 8}, {seg::H, 4}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {8, 9, 11}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, off);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, off);
  }

  for (file_off_t off : {8, 10, 11}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_EQ(s1.ret, -1);
    EXPECT_EQ(s1.ec, std::errc::no_such_device_or_address);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(ec, std::errc::no_such_device_or_address);
  }
}

// -------------------------------------------
// Entire file is a single hole (no data ever)
// -------------------------------------------

TEST(sparse_file_seeker, all_hole_seek_hole_and_seek_data) {
  // hole [0..10)
  auto chunks = make_chunks({{seg::H, 10}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 5, 9}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, off);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, off);
  }

  for (file_off_t off : {0, 4, 9}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_EQ(s1.ret, -1);
    EXPECT_EQ(s1.ec, std::errc::no_such_device_or_address);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(ec, std::errc::no_such_device_or_address);
  }
}

// ---------------------------------------------------
// Multiple consecutive data chunks behave as one data
// ---------------------------------------------------

TEST(sparse_file_seeker, consecutive_data_chunks_behave_as_one_extent) {
  // data [0..5), data [5..11), hole [11..14), data [14..20)
  auto chunks =
      make_chunks({{seg::D, 5}, {seg::D, 6}, {seg::H, 3}, {seg::D, 6}});
  sparse_file_seeker s(chunks);

  for (file_off_t off : {0, 4, 5, 10}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::hole);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 11);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::hole, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 11);
  }

  for (file_off_t off : {11, 12, 13}) {
    auto s1 = call_static_seek(chunks, off, seek_whence::data);
    EXPECT_FALSE(s1.ec);
    EXPECT_EQ(s1.ret, 14);

    std::error_code ec;
    auto r = s.seek(off, seek_whence::data, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, 14);
  }
}

// -------------------------------------------
// Boundary: immediately before/after holes
// -------------------------------------------

TEST(sparse_file_seeker, at_hole_begin_and_end_boundaries) {
  // data [0..6), hole [6..9), data [9..12)
  auto chunks = make_chunks({{seg::D, 6}, {seg::H, 3}, {seg::D, 3}});
  sparse_file_seeker s(chunks);

  {
    auto s1 = call_static_seek(chunks, 6, seek_whence::hole);
    auto s2 = call_static_seek(chunks, 6, seek_whence::data);

    EXPECT_FALSE(s1.ec);
    EXPECT_FALSE(s2.ec);
    EXPECT_EQ(s1.ret, 6); // in hole
    EXPECT_EQ(s2.ret, 9); // next data
  }

  {
    std::error_code ec_h, ec_d;
    auto r_h = s.seek(6, seek_whence::hole, ec_h);
    auto r_d = s.seek(6, seek_whence::data, ec_d);
    EXPECT_FALSE(ec_h);
    EXPECT_FALSE(ec_d);
    EXPECT_EQ(r_h, 6); // in hole
    EXPECT_EQ(r_d, 9); // next data
  }

  {
    auto s1 = call_static_seek(chunks, 9, seek_whence::hole);
    auto s2 = call_static_seek(chunks, 9, seek_whence::data);

    EXPECT_FALSE(s1.ec);
    EXPECT_FALSE(s2.ec);
    EXPECT_EQ(s1.ret, 12); // EOF as virtual hole
    EXPECT_EQ(s2.ret, 9);  // already data
  }

  {
    std::error_code ec_h, ec_d;
    auto r_h = s.seek(9, seek_whence::hole, ec_h);
    auto r_d = s.seek(9, seek_whence::data, ec_d);
    EXPECT_FALSE(ec_h);
    EXPECT_FALSE(ec_d);
    EXPECT_EQ(r_h, 12); // EOF as virtual hole
    EXPECT_EQ(r_d, 9);  // already data
  }
}

// -------------------------------------------
// Static vs instance equivalence (and reuse)
// -------------------------------------------

TEST(sparse_file_seeker,
     static_and_instance_equivalence_on_various_layouts_with_reuse) {
  std::vector<std::vector<test_chunk>> layouts = {
      make_chunks({{seg::D, 10}}),
      make_chunks({{seg::H, 5}, {seg::D, 7}}),
      make_chunks(
          {{seg::D, 3}, {seg::H, 2}, {seg::D, 4}, {seg::H, 1}, {seg::D, 5}}),
      make_chunks({{seg::D, 1}, {seg::D, 1}, {seg::D, 1}, {seg::H, 2}}),
      make_chunks({{seg::H, 3}, {seg::D, 1}, {seg::H, 3}, {seg::D, 2}}),
  };

  for (auto const& chunks : layouts) {
    // Build a reusable instance once.
    sparse_file_seeker s(chunks);

    // compute size
    file_off_t size = 0;
    for (auto const& c : chunks) {
      size += c.size();
    }

    // Sweep offsets reusing the same instance across many calls.
    for (file_off_t off = 0; off < size; ++off) {
      for (auto wh : {seek_whence::hole, seek_whence::data}) {
        auto expected = call_static_seek(chunks, off, wh);

        std::error_code ec;
        auto got = s.seek(off, wh, ec);
        EXPECT_EQ(got, expected.ret) << "off=" << off;
        EXPECT_EQ(ec, expected.ec) << "off=" << off;
        if (expected.ec) {
          EXPECT_EQ(ec, std::errc::no_such_device_or_address);
        }
      }
    }
  }
}

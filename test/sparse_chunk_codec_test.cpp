/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <dwarfs/internal/sparse_chunk_codec.h>
#include <dwarfs/reader/internal/sparse_file_seeker.h>

using namespace dwarfs;
using dwarfs::internal::large_hole_size_view;
using dwarfs::internal::sparse_chunk;
using dwarfs::internal::sparse_chunk_codec;
using dwarfs::internal::sparse_chunk_size_accumulator;

namespace {

constexpr uint32_t kBlockSize{uint32_t{1} << 20};
constexpr uint32_t kHoleIx{42};

sparse_chunk_codec new_codec(uint32_t block_size = kBlockSize) {
  return sparse_chunk_codec{
      block_size, kHoleIx,
      sparse_chunk_codec::hole_marker_mode::block_size_based};
}

sparse_chunk_codec old_codec(uint32_t block_size = kBlockSize) {
  return sparse_chunk_codec{
      block_size, kHoleIx, sparse_chunk_codec::hole_marker_mode::legacy_compat};
}

static_assert(reader::internal::chunk_like<sparse_chunk>);

} // namespace

TEST(sparse_chunk_codec, marker_resolution) {
  for (uint32_t bits = 16; bits <= 26; bits += 2) {
    auto const bs = uint32_t{1} << bits;
    EXPECT_EQ(bs - 1, new_codec(bs).large_hole_marker());
    EXPECT_EQ(UINT32_MAX, old_codec(bs).large_hole_marker());
    EXPECT_EQ(kChunkOffsetIsLargeHoleCompat, old_codec(bs).large_hole_marker());
  }
}

TEST(sparse_chunk_codec, hole_block_detection) {
  auto const codec = new_codec();
  EXPECT_TRUE(codec.is_hole_block(kHoleIx));
  EXPECT_FALSE(codec.is_hole_block(kHoleIx - 1));
  EXPECT_FALSE(codec.is_hole_block(kHoleIx + 1));

  sparse_chunk_codec no_holes{kBlockSize};
  EXPECT_FALSE(no_holes.is_hole_block(kHoleIx));
  EXPECT_FALSE(no_holes.is_hole_block(0));
}

TEST(sparse_chunk_codec, data_chunk_passthrough) {
  auto const codec = new_codec();
  auto const r = codec.classify(3, 4096, 100);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->is_data());
  EXPECT_FALSE(r->is_hole());
  EXPECT_EQ(3, r->block());
  EXPECT_EQ(4096, r->offset());
  EXPECT_EQ(100, r->size());
  EXPECT_EQ(sparse_chunk::make_data(3, 4096, 100), *r);
}

TEST(sparse_chunk_codec, no_hole_block_index_means_all_data) {
  // Without a hole block index, nothing is a hole, and marker-valued
  // offsets on data chunks are meaningless (but must be in bounds).
  sparse_chunk_codec codec{kBlockSize};
  auto const r = codec.classify(kHoleIx, codec.large_hole_marker(), 1);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->is_data());
  EXPECT_EQ(kBlockSize - 1, r->offset());
}

TEST(sparse_chunk_codec, marker_offset_on_data_block_is_data) {
  // The marker is only meaningful for chunks in the hole block.
  auto const codec = new_codec();
  auto const r = codec.classify(7, codec.large_hole_marker(), 1);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->is_data());
}

TEST(sparse_chunk_codec, data_chunk_bounds) {
  using enum sparse_chunk_codec::error;
  auto const codec = new_codec();
  auto classify = [&](uint32_t o, uint32_t s) {
    return codec.classify(0, o, s);
  };

  EXPECT_TRUE(classify(0, kBlockSize).has_value());
  EXPECT_TRUE(classify(kBlockSize - 1, 1).has_value());
  EXPECT_TRUE(classify(0, 0).has_value());

  {
    auto const r = classify(kBlockSize, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(data_offset_out_of_range, r.error());
  }
  {
    auto const r = classify(kBlockSize - 1, 2);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(data_size_out_of_range, r.error());
  }
  {
    // must not overflow internally
    auto const r = classify(kBlockSize - 1, UINT32_MAX);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(data_size_out_of_range, r.error());
  }
}

TEST(sparse_chunk_codec, direct_hole_decode) {
  auto const codec = new_codec();
  auto classify = [&](uint32_t o, uint32_t s) {
    return codec.classify(kHoleIx, o, s);
  };

  {
    auto const r = classify(0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_hole());
    EXPECT_EQ(0, r->size());
  }
  {
    auto const r = classify(4096, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_hole());
    EXPECT_EQ(uint64_t{3} * kBlockSize + 4096,
              static_cast<uint64_t>(r->size()));
    EXPECT_EQ(sparse_chunk::make_hole(uint64_t{3} * kBlockSize + 4096), *r);
  }
  {
    // largest direct remainder under the new convention
    auto const r = classify(kBlockSize - 2, 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(uint64_t{1} * kBlockSize + kBlockSize - 2,
              static_cast<uint64_t>(r->size()));
  }
}

TEST(sparse_chunk_codec, old_convention_boundary_remainder_is_direct) {
  // Under the old convention, offset == block_size - 1 is a perfectly
  // valid direct encoding (e.g. a file ending in a hole of byte-granular
  // length); it must not be mistaken for a large hole reference.
  auto const codec = old_codec();
  auto const r = codec.classify(kHoleIx, kBlockSize - 1, 7);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->is_hole());
  EXPECT_EQ(uint64_t{7} * kBlockSize + kBlockSize - 1,
            static_cast<uint64_t>(r->size()));
}

TEST(sparse_chunk_codec, large_hole_reference) {
  using enum sparse_chunk_codec::error;
  std::vector<uint64_t> const large{10, uint64_t{1} << 40};

  {
    sparse_chunk_codec const codec{
        kBlockSize, kHoleIx,
        sparse_chunk_codec::hole_marker_mode::block_size_based,
        large_hole_size_view::by_ref(large)};
    auto const marker = codec.large_hole_marker();

    auto const r0 = codec.classify(kHoleIx, marker, 0);
    ASSERT_TRUE(r0.has_value());
    EXPECT_TRUE(r0->is_hole());
    EXPECT_EQ(10, r0->size());

    auto const r1 = codec.classify(kHoleIx, marker, 1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(uint64_t{1} << 40, static_cast<uint64_t>(r1->size()));

    auto const oob = codec.classify(kHoleIx, marker, 2);
    ASSERT_FALSE(oob.has_value());
    EXPECT_EQ(large_hole_index_out_of_range, oob.error());
  }

  {
    auto const codec = new_codec();
    auto const missing = codec.classify(kHoleIx, codec.large_hole_marker(), 0);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(large_hole_list_missing, missing.error());
  }

  {
    sparse_chunk_codec const codec{
        kBlockSize, kHoleIx,
        sparse_chunk_codec::hole_marker_mode::legacy_compat,
        large_hole_size_view::by_ref(large)};
    auto const r = codec.classify(kHoleIx, UINT32_MAX, 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(uint64_t{1} << 40, static_cast<uint64_t>(r->size()));
  }
}

TEST(sparse_chunk_codec, large_hole_size_view_by_value) {
  std::vector<uint64_t> const large{123};
  sparse_chunk_codec const codec{
      kBlockSize, kHoleIx,
      sparse_chunk_codec::hole_marker_mode::block_size_based,
      large_hole_size_view::by_value(std::span{large})};

  auto const r = codec.classify(kHoleIx, codec.large_hole_marker(), 0);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(123, r->size());
}

TEST(sparse_chunk_codec, conventions_are_mutually_exclusive) {
  using enum sparse_chunk_codec::error;
  // The old marker is out of range under the new convention; this is
  // what makes the metadata check reject flag/marker mismatches.
  auto const r = new_codec().classify(kHoleIx, UINT32_MAX, 0);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(hole_remainder_out_of_range, r.error());
}

TEST(sparse_chunk_codec, large_hole_size_mask_limit) {
  using enum sparse_chunk_codec::error;
  std::vector<uint64_t> const ok{kChunkBitsSizeMask};
  sparse_chunk_codec const ok_codec{
      kBlockSize, kHoleIx,
      sparse_chunk_codec::hole_marker_mode::block_size_based,
      large_hole_size_view::by_ref(ok)};
  auto const r = ok_codec.classify(kHoleIx, ok_codec.large_hole_marker(), 0);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(kChunkBitsSizeMask, static_cast<uint64_t>(r->size()));

  // an entry with the hole bit set would silently corrupt the size
  std::vector<uint64_t> const bad{kChunkBitsSizeMask + 1};
  sparse_chunk_codec const bad_codec{
      kBlockSize, kHoleIx,
      sparse_chunk_codec::hole_marker_mode::block_size_based,
      large_hole_size_view::by_ref(bad)};
  auto const b = bad_codec.classify(kHoleIx, bad_codec.large_hole_marker(), 0);
  ASSERT_FALSE(b.has_value());
  EXPECT_EQ(large_hole_size_out_of_range, b.error());
}

TEST(sparse_chunk_codec, encode_decode_round_trip) {
  constexpr uint64_t kLimit{uint64_t{1} << 41};
  auto const codec = old_codec();
  std::vector<uint64_t> const sizes{
      0,
      1,
      kBlockSize - 2,
      kBlockSize - 1, // collides with the new marker, still decodable
      kBlockSize,
      kBlockSize + 1,
      uint64_t{5} * kBlockSize + 123,
      (uint64_t{1} << 40) + kBlockSize - 1,
  };

  for (auto const h : sizes) {
    SCOPED_TRACE(fmt::format("h = {}", h));
    auto const e = codec.encode_direct(h, kLimit);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(h % kBlockSize, e->offset);
    EXPECT_EQ(h / kBlockSize, e->size);
    ASSERT_LE(e->size, UINT32_MAX);
    EXPECT_EQ(h, codec.decode_direct(e->offset, e->size));
  }
}

TEST(sparse_chunk_codec, requires_large_hole) {
  constexpr uint64_t kLimit{uint64_t{1} << 40};

  {
    auto const codec = new_codec();
    // above the limit
    EXPECT_FALSE(codec.encode_direct(kLimit + 1, kLimit));
    // below the limit, no marker collision
    EXPECT_TRUE(codec.encode_direct(kBlockSize, kLimit));
    EXPECT_TRUE(codec.encode_direct(0, kLimit));
    // marker collision forces the list even below the limit
    EXPECT_FALSE(codec.encode_direct(kBlockSize - 1, kLimit));
    EXPECT_FALSE(
        codec.encode_direct(uint64_t{7} * kBlockSize + kBlockSize - 1, kLimit));
  }

  {
    // the old marker can never collide with a direct encoding; only the
    // limit matters (this matches the historical writer behavior)
    auto const codec = old_codec();
    EXPECT_TRUE(codec.encode_direct(kBlockSize - 1, kLimit));
    EXPECT_FALSE(codec.encode_direct(kLimit + 1, kLimit));
  }
}

TEST(sparse_chunk_codec, power_of_two_minus_one_sweep) {
  // Mirrors the boundary hole sizes in the sparse-holes compat image:
  // 2^b - 1 sized holes collide with the new marker exactly for
  // b >= log2(block_size).
  auto const codec = new_codec();
  constexpr uint64_t kHugeLimit{UINT64_MAX};

  for (unsigned b = 1; b <= 40; ++b) {
    auto const h = (uint64_t{1} << b) - 1;
    auto const e = codec.encode_direct(h, kHugeLimit);

    EXPECT_EQ(b < 20, e.has_value()) << "b=" << b;

    if (e) {
      auto const r = codec.classify(kHoleIx, e->offset, e->size);
      ASSERT_TRUE(r.has_value()) << "b=" << b;
      EXPECT_EQ(h, static_cast<uint64_t>(r->size())) << "b=" << b;
    }
  }
}

TEST(sparse_chunk_codec, seeker_integration) {
  // sparse_chunk satisfies chunk_like, so codec output feeds directly
  // into sparse_file_seeker.
  std::vector<sparse_chunk> const chunks{
      sparse_chunk::make_data(0, 0, 100),
      sparse_chunk::make_hole(50),
      sparse_chunk::make_data(0, 100, 25),
  };

  using reader::seek_whence;
  using reader::internal::sparse_file_seeker;

  auto seek = [&](file_off_t off, seek_whence whence) {
    std::error_code ec;
    auto const r = sparse_file_seeker::seek(chunks, off, whence, ec);
    EXPECT_FALSE(ec) << ec.message();
    return r;
  };

  EXPECT_EQ(0, seek(0, seek_whence::data));
  EXPECT_EQ(100, seek(0, seek_whence::hole));
  EXPECT_EQ(105, seek(105, seek_whence::hole));
  EXPECT_EQ(150, seek(105, seek_whence::data));

  sparse_file_seeker const seeker{chunks};
  std::error_code ec;
  EXPECT_EQ(150, seeker.seek(105, seek_whence::data, ec));
  EXPECT_FALSE(ec);
}

TEST(sparse_chunk_codec, size_accumulator) {
  std::vector<sparse_chunk> const chunks{
      sparse_chunk::make_data(0, 0, 100),
      sparse_chunk::make_hole(50),
      sparse_chunk::make_data(0, 100, 25),
  };

  sparse_chunk_size_accumulator sparse;
  sparse_chunk_size_accumulator non_sparse{/*holes_are_allocated=*/true};

  for (auto const& chunk : chunks) {
    sparse.add(chunk);
    non_sparse.add(chunk);
  }

  EXPECT_EQ(175, sparse.size());
  EXPECT_EQ(125, sparse.allocated_size());
  EXPECT_EQ(175, non_sparse.size());
  EXPECT_EQ(175, non_sparse.allocated_size());
}

TEST(sparse_chunk_codec, error_strings) {
  using enum sparse_chunk_codec::error;
  for (auto const e :
       {data_offset_out_of_range, data_size_out_of_range,
        hole_remainder_out_of_range, large_hole_list_missing,
        large_hole_index_out_of_range, large_hole_size_out_of_range}) {
    EXPECT_FALSE(to_string(e).empty());
  }
}

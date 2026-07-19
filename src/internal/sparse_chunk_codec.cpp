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

#include <bit>

#include <dwarfs/internal/features.h>
#include <dwarfs/internal/sparse_chunk_codec.h>

namespace dwarfs::internal {

sparse_chunk_codec::hole_marker_mode
sparse_chunk_codec::mode_from(feature_set const& features) {
  return features.has(feature::sparsefiles_new_lhm)
             ? hole_marker_mode::block_size_based
             : hole_marker_mode::legacy_compat;
}

sparse_chunk_codec::sparse_chunk_codec(uint32_t block_size,
                                       std::optional<uint32_t> hole_block_index,
                                       hole_marker_mode mode,
                                       large_hole_size_view large_hole_sizes)
    : block_size_{block_size}
    , block_size_bits_{static_cast<unsigned>(std::countr_zero(block_size))}
    , large_hole_marker_{mode == hole_marker_mode::legacy_compat
                             ? kChunkOffsetIsLargeHoleCompat
                             : block_size - 1}
    , hole_block_index_{hole_block_index}
    , large_hole_sizes_{std::move(large_hole_sizes)} {
  assert(block_size > 0);
  assert(std::has_single_bit(block_size));
}

sparse_chunk_codec::sparse_chunk_codec(uint32_t block_size,
                                       std::optional<uint32_t> hole_block_index,
                                       feature_set const& features,
                                       large_hole_size_view large_hole_sizes)
    : sparse_chunk_codec{block_size, hole_block_index, mode_from(features),
                         std::move(large_hole_sizes)} {}

std::expected<sparse_chunk, sparse_chunk_codec::error>
sparse_chunk_codec::classify_large_hole(uint32_t index) const {
  if (!large_hole_sizes_.valid()) {
    return std::unexpected{error::large_hole_list_missing};
  }
  if (std::cmp_greater_equal(index, large_hole_sizes_.size())) {
    return std::unexpected{error::large_hole_index_out_of_range};
  }
  auto const hole_size = large_hole_sizes_[index];
  if (hole_size > kChunkBitsSizeMask) {
    return std::unexpected{error::large_hole_size_out_of_range};
  }
  return sparse_chunk::make_hole(hole_size);
}

std::string_view to_string(sparse_chunk_codec::error e) {
  using enum sparse_chunk_codec::error;
  switch (e) {
  case data_offset_out_of_range:
    return "data chunk offset out of range";
  case data_size_out_of_range:
    return "data chunk size out of range";
  case hole_remainder_out_of_range:
    return "hole chunk size remainder out of range";
  case large_hole_list_missing:
    return "large hole chunk, but no large_hole_size list";
  case large_hole_index_out_of_range:
    return "large hole index out of range";
  case large_hole_size_out_of_range:
    return "large hole size out of range";
  }
  return "<unknown sparse chunk error>";
}

} // namespace dwarfs::internal

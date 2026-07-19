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

#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include <dwarfs/metadata_defs.h>
#include <dwarfs/types.h>

namespace dwarfs::internal {

class feature_set;

// Anything that looks like the `large_hole_size` list from the metadata.
template <typename T>
concept large_hole_size_list = requires(T const& list, std::size_t i) {
  { list.size() } -> std::convertible_to<std::size_t>;
  { list[i] } -> std::convertible_to<uint64_t>;
};

// Wraps a list of large hole sizes, either by value or by reference.
class large_hole_size_view {
 public:
  large_hole_size_view() = default;

  // stores a copy of a lightweight list view
  template <large_hole_size_list ListView>
  static large_hole_size_view by_value(ListView view) {
    auto const size = static_cast<std::size_t>(view.size());
    return {size, [view = std::move(view)](std::size_t index) -> uint64_t {
              return static_cast<uint64_t>(view[index]);
            }};
  }

  // borrows a stable container and snapshots its size
  template <large_hole_size_list List>
  static large_hole_size_view by_ref(List const& list) {
    return {static_cast<std::size_t>(list.size()),
            [&list](std::size_t index) -> uint64_t {
              return static_cast<uint64_t>(list[index]);
            }};
  }

  // explicitly disallow temporaries
  template <large_hole_size_list List>
  static large_hole_size_view by_ref(List const&&) = delete;

  bool valid() const noexcept { return static_cast<bool>(lookup_); }

  std::size_t size() const noexcept {
    assert(valid());
    return size_;
  }

  uint64_t operator[](std::size_t index) const {
    assert(valid());
    assert(index < size_);
    return lookup_(index);
  }

 private:
  using lookup_function = std::function<uint64_t(std::size_t)>;

  large_hole_size_view(std::size_t size, lookup_function lookup)
      : size_{size}
      , lookup_{std::move(lookup)} {}

  std::size_t size_{0};
  lookup_function lookup_;
};

// A single classified chunk of a (possibly sparse) regular file.
class sparse_chunk {
 public:
  sparse_chunk() = default;

  static sparse_chunk
  make_data(uint32_t block, uint32_t offset, uint32_t size) {
    return {block, offset, uint64_t{size}};
  }

  static sparse_chunk make_hole(uint64_t size) {
    assert(size <= kChunkBitsSizeMask);
    return {0, 0, size | kChunkBitsHoleBit};
  }

  bool is_data() const { return (bits_ & kChunkBitsHoleBit) == 0; }

  bool is_hole() const {
    return (bits_ & kChunkBitsHoleBit) == kChunkBitsHoleBit;
  }

  uint32_t block() const {
    assert(is_data());
    return block_;
  }

  uint32_t offset() const {
    assert(is_data());
    return offset_;
  }

  file_off_t size() const {
    return static_cast<file_off_t>(bits_ & kChunkBitsSizeMask);
  }

  friend bool operator==(sparse_chunk const&, sparse_chunk const&) = default;

 private:
  sparse_chunk(uint32_t block, uint32_t offset, uint64_t bits)
      : block_{block}
      , offset_{offset}
      , bits_{bits} {}

  uint32_t block_{0};
  uint32_t offset_{0};
  uint64_t bits_{0};
};

// Anything exposing the decoded chunk interface used by size accounting.
template <typename T>
concept sparse_chunk_like = requires(T const& chunk) {
  { chunk.is_data() } -> std::same_as<bool>;
  { chunk.size() } -> std::convertible_to<file_off_t>;
};

// Accumulate the logical and allocated sizes of a sequence of (sparse) chunks.
class sparse_chunk_size_accumulator {
 public:
  explicit sparse_chunk_size_accumulator(bool holes_are_allocated = false)
      : holes_are_allocated_{holes_are_allocated} {}

  template <sparse_chunk_like Chunk>
  void add(Chunk const& chunk) {
    auto const size = static_cast<uint64_t>(chunk.size());
    size_ += size;
    if (holes_are_allocated_ || chunk.is_data()) {
      allocated_size_ += size;
    }
  }

  uint64_t size() const { return size_; }

  uint64_t allocated_size() const { return allocated_size_; }

 private:
  bool holes_are_allocated_{false};
  uint64_t size_{0};
  uint64_t allocated_size_{0};
};

template <typename T>
concept raw_thrift_chunk = requires(T const& chunk) {
  { chunk.block().value() } -> std::convertible_to<uint32_t>;
  { chunk.offset().value() } -> std::convertible_to<uint32_t>;
  { chunk.size().value() } -> std::convertible_to<uint64_t>;
};

template <typename T>
concept raw_frozen_chunk = requires(T const& chunk) {
  { chunk.block() } -> std::convertible_to<uint32_t>;
  { chunk.offset() } -> std::convertible_to<uint32_t>;
  { chunk.size() } -> std::convertible_to<uint64_t>;
};

// Codec for the serialized representation of (sparse) file chunks.
class sparse_chunk_codec {
 public:
  enum class error {
    data_offset_out_of_range,      // data: offset >= block_size
    data_size_out_of_range,        // data: offset + size > block_size
    hole_remainder_out_of_range,   // direct hole: offset >= block_size
    large_hole_list_missing,       // marker used, but no large_hole_size list
    large_hole_index_out_of_range, // marker used, index >= list size
    large_hole_size_out_of_range,  // list entry exceeds kChunkBitsSizeMask
  };

  enum class hole_marker_mode : unsigned {
    legacy_compat,    // kChunkOffsetIsLargeHoleCompat
    block_size_based, // block_size - 1
  };

  struct direct_hole_encoding {
    uint32_t offset{0};
    uint32_t size{0};

    friend bool operator==(direct_hole_encoding const&,
                           direct_hole_encoding const&) = default;
  };

  static hole_marker_mode mode_from(feature_set const& features);

  explicit sparse_chunk_codec(
      uint32_t block_size,
      std::optional<uint32_t> hole_block_index = std::nullopt,
      hole_marker_mode mode = hole_marker_mode::block_size_based,
      large_hole_size_view large_hole_sizes = {});

  sparse_chunk_codec(uint32_t block_size,
                     std::optional<uint32_t> hole_block_index,
                     feature_set const& features,
                     large_hole_size_view large_hole_sizes = {});

  std::optional<uint32_t> hole_block_index() const { return hole_block_index_; }

  uint32_t large_hole_marker() const { return large_hole_marker_; }

  bool is_hole_block(uint32_t block) const {
    return hole_block_index_.has_value() && block == *hole_block_index_;
  }

  // classify a raw chunk into a data or hole chunk
  std::expected<sparse_chunk, error>
  classify(uint32_t block, uint32_t offset, uint32_t size) const {
    if (!is_hole_block(block)) {
      if (offset >= block_size_) {
        return std::unexpected{error::data_offset_out_of_range};
      }
      if (uint64_t{offset} + uint64_t{size} > block_size_) {
        return std::unexpected{error::data_size_out_of_range};
      }
      return sparse_chunk::make_data(block, offset, size);
    }

    if (offset == large_hole_marker_) [[unlikely]] {
      return classify_large_hole(size);
    }

    if (offset >= block_size_) {
      return std::unexpected{error::hole_remainder_out_of_range};
    }

    return sparse_chunk::make_hole(decode_direct(offset, size));
  }

  // convenience overload for raw thrift chunk
  std::expected<sparse_chunk, error>
  classify(raw_thrift_chunk auto const& chunk) const {
    return classify(chunk.block().value(), chunk.offset().value(),
                    chunk.size().value());
  }

  // convenience overload for raw frozen chunk
  std::expected<sparse_chunk, error>
  classify(raw_frozen_chunk auto const& chunk) const {
    return classify(chunk.block(), chunk.offset(), chunk.size());
  }

  // decode a directly encoded hole size
  uint64_t decode_direct(uint32_t offset, uint32_t size) const {
    assert(offset < block_size_);
    return (uint64_t{size} << block_size_bits_) + offset;
  }

  // try to encode a hole size into direct `offset` / `size` chunk fields,
  // if the hole size must be encoded as a large hole, return std::nullopt
  std::optional<direct_hole_encoding>
  encode_direct(uint64_t hole_size, uint64_t inline_size_limit) const {
    if (hole_size > inline_size_limit) [[unlikely]] {
      return std::nullopt;
    }

    auto const offset = static_cast<uint32_t>(hole_size & (block_size_ - 1));

    if (offset == large_hole_marker_) [[unlikely]] {
      return std::nullopt;
    }

    auto const size = hole_size >> block_size_bits_;

    assert(std::cmp_less_equal(size, UINT32_MAX));

    return direct_hole_encoding{offset, static_cast<uint32_t>(size)};
  }

 private:
  std::expected<sparse_chunk, error> classify_large_hole(uint32_t index) const;

  uint32_t block_size_;
  unsigned block_size_bits_;
  uint32_t large_hole_marker_;
  std::optional<uint32_t> hole_block_index_;
  large_hole_size_view large_hole_sizes_;
};

std::string_view to_string(sparse_chunk_codec::error e);

} // namespace dwarfs::internal

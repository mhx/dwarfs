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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include <dwarfs/container/detail/packed_vector_helpers.h>

namespace dwarfs::container::detail {

template <typename T>
struct packed_vector_heap_storage {
  static_assert(std::is_trivially_copyable_v<T>);

  enum class initialization {
    no_init,
    zero_init,
  };

  using size_type = std::size_t;

 private:
  struct heap_prefix {
    size_type size;
    size_type capacity_blocks;
  };

  static constexpr size_type payload_alignment = alignof(T);
  static constexpr size_type block_alignment =
      std::max(alignof(heap_prefix), payload_alignment);
  static constexpr size_type payload_offset =
      round_up_to_multiple(sizeof(heap_prefix), payload_alignment);

  static_assert(payload_offset % payload_alignment == 0);
  static_assert(block_alignment >= alignof(heap_prefix));
  static_assert(block_alignment >= payload_alignment);

  [[nodiscard]] static auto
  raw_block(T const* payload) noexcept -> std::byte const* {
    return reinterpret_cast<std::byte const*>(payload) - payload_offset;
  }

  [[nodiscard]] static auto raw_block(T* payload) noexcept -> std::byte* {
    return reinterpret_cast<std::byte*>(payload) - payload_offset;
  }

  [[nodiscard]] static auto
  header(T const* payload) noexcept -> heap_prefix const* {
    return payload ? std::launder(reinterpret_cast<heap_prefix const*>(
                         raw_block(payload)))
                   : nullptr;
  }

  [[nodiscard]] static auto header(T* payload) noexcept -> heap_prefix* {
    return const_cast<heap_prefix*>(header(static_cast<T const*>(payload)));
  }

 public:
  [[nodiscard]] static auto
  allocate(size_type size, size_type capacity_blocks, initialization init,
           bool force_allocation = false) -> T* {
    if (!force_allocation && size == 0 && capacity_blocks == 0) {
      return nullptr;
    }

    if (capacity_blocks >
        (std::numeric_limits<size_type>::max() - payload_offset) / sizeof(T)) {
      throw std::bad_array_new_length();
    }

    auto const payload_bytes = capacity_blocks * sizeof(T);
    auto const total_bytes = payload_offset + payload_bytes;

    auto* raw = static_cast<std::byte*>(
        ::operator new(total_bytes, std::align_val_t{block_alignment}));

    std::construct_at(reinterpret_cast<heap_prefix*>(raw),
                      heap_prefix{size, capacity_blocks});

    auto* payload = reinterpret_cast<T*>(raw + payload_offset);

    if (capacity_blocks > 0) {
      if (init == initialization::zero_init) {
        std::uninitialized_value_construct_n(payload, capacity_blocks);
      } else {
        std::uninitialized_default_construct_n(payload, capacity_blocks);
      }
    }

    return payload;
  }

  static void deallocate(T* payload) noexcept {
    if (payload) {
      ::operator delete(raw_block(payload), std::align_val_t{block_alignment});
    }
  }

  [[nodiscard]] static auto size(T const* payload) noexcept -> size_type {
    return payload ? header(payload)->size : 0;
  }

  [[nodiscard]] static auto
  capacity_blocks(T const* payload) noexcept -> size_type {
    return payload ? header(payload)->capacity_blocks : 0;
  }

  static void set_size(T* payload, size_type v) noexcept {
    assert(payload || v == 0);
    if (payload) {
      header(payload)->size = v;
    }
  }

  static void copy_n(T const* src, size_type n, T* dst) noexcept {
    assert(n == 0 || (src && dst));
    if (n > 0) {
      std::memcpy(dst, src, n * sizeof(T));
    }
  }
};

} // namespace dwarfs::container::detail

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

#include <cstddef>

namespace dwarfs::internal::detail {

template <typename SizeType = std::size_t>
struct default_packed_vector_metadata {
  using size_type = SizeType;

  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return size_;
  }

  [[nodiscard]] constexpr auto bits() const noexcept -> size_type {
    return bits_;
  }

  [[nodiscard]] constexpr auto capacity_blocks() const noexcept -> size_type {
    return capacity_blocks_;
  }

  constexpr void set_size(size_type v) noexcept { size_ = v; }
  constexpr void set_bits(size_type v) noexcept { bits_ = v; }
  constexpr void set_capacity_blocks(size_type v) noexcept {
    capacity_blocks_ = v;
  }

 private:
  size_type size_{0};
  size_type bits_{0};
  size_type capacity_blocks_{0};
};

} // namespace dwarfs::internal::detail

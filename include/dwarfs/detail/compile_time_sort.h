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

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace dwarfs::detail {

template <typename Compare>
constexpr inline bool is_builtin_less_v = false;

template <typename T>
constexpr inline bool is_builtin_less_v<std::less<T>> = true;

template <>
constexpr inline bool is_builtin_less_v<std::ranges::less> = true;

/**
 * Stable bottom-up merge sort for use during constant evaluation.
 *
 * The primary goal of this implementation is to use as few operations
 * as possible, to ensure that even large inputs can be sorted reliably
 * at compile time. In particular:
 *
 *  - The merge is iterative and ping-pongs between two buffers, so there
 *    is no recursion depth to exhaust and no wrapper machinery around the
 *    comparisons.
 *  - Projection and comparison are applied inline for the common cases
 *    instead of going through `std::invoke` and `std::less`, each of which
 *    costs an extra call (or several) per comparison.
 *  - An O(N) pre-pass short-circuits already sorted input entirely.
 */
template <typename T, std::size_t N, typename Compare = std::less<>,
          typename Proj = std::identity>
constexpr void
compile_time_sort(std::array<T, N>& arr, Compare comp = {}, Proj proj = {}) {
  if constexpr (N > 1) {
    auto less = [&](T const& a, T const& b) -> bool {
      if constexpr (std::is_member_object_pointer_v<Proj>) {
        if constexpr (is_builtin_less_v<Compare>) {
          return a.*proj < b.*proj;
        } else {
          return comp(a.*proj, b.*proj);
        }
      } else if constexpr (std::is_same_v<Proj, std::identity>) {
        if constexpr (is_builtin_less_v<Compare>) {
          return a < b;
        } else {
          return comp(a, b);
        }
      } else {
        return comp(std::invoke(proj, a), std::invoke(proj, b));
      }
    };

    bool sorted = true;

    for (std::size_t i = 1; i < N && sorted; ++i) {
      sorted = !less(arr[i], arr[i - 1]);
    }

    if (sorted) {
      return;
    }

    auto buf = arr;
    auto* src = &arr;
    auto* dst = &buf;

    for (std::size_t width = 1; width < N; width *= 2) {
      auto& s = *src;
      auto& d = *dst;

      for (std::size_t lo = 0; lo < N; lo += 2 * width) {
        auto const mid = lo + width < N ? lo + width : N;
        auto const hi = lo + 2 * width < N ? lo + 2 * width : N;
        auto i = lo, j = mid, k = lo;

        // only taking from the right half on a strict `less` keeps this stable
        while (i < mid && j < hi) {
          d[k++] = less(s[j], s[i]) ? s[j++] : s[i++];
        }

        while (i < mid) {
          d[k++] = s[i++];
        }

        while (j < hi) {
          d[k++] = s[j++];
        }
      }

      std::swap(src, dst);
    }

    if (src != &arr) {
      arr = *src;
    }
  }
}

} // namespace dwarfs::detail

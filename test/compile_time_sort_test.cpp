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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include <dwarfs/detail/compile_time_sort.h>

using namespace dwarfs::detail;
using namespace std::string_view_literals;

namespace {

template <typename T, std::size_t N, typename Compare = std::less<>,
          typename Proj = std::identity>
consteval std::array<T, N>
sorted(std::array<T, N> arr, Compare comp = {}, Proj proj = {}) {
  compile_time_sort(arr, comp, proj);
  return arr;
}

// degenerate sizes
static_assert(sorted(std::array<int, 0>{}) == std::array<int, 0>{});
static_assert(sorted(std::array{42}) == std::array{42});
static_assert(sorted(std::array{1, 2}) == std::array{1, 2});
static_assert(sorted(std::array{2, 1}) == std::array{1, 2});

// small inputs, all six permutations of three elements
static_assert(sorted(std::array{1, 2, 3}) == std::array{1, 2, 3});
static_assert(sorted(std::array{1, 3, 2}) == std::array{1, 2, 3});
static_assert(sorted(std::array{2, 1, 3}) == std::array{1, 2, 3});
static_assert(sorted(std::array{2, 3, 1}) == std::array{1, 2, 3});
static_assert(sorted(std::array{3, 1, 2}) == std::array{1, 2, 3});
static_assert(sorted(std::array{3, 2, 1}) == std::array{1, 2, 3});

// duplicates and equal elements
static_assert(sorted(std::array{5, 5, 5, 5}) == std::array{5, 5, 5, 5});
static_assert(sorted(std::array{3, 1, 3, 2, 1}) == std::array{1, 1, 2, 3, 3});

// every size from 0 to 32, worst case (reverse sorted) input
template <std::size_t N>
consteval bool sorts_reversed() {
  std::array<int, N> arr{};

  for (std::size_t i = 0; i < N; ++i) {
    arr[i] = static_cast<int>(N - i);
  }

  compile_time_sort(arr);

  for (std::size_t i = 0; i < N; ++i) {
    if (arr[i] != static_cast<int>(i + 1)) {
      return false;
    }
  }

  return true;
}

template <std::size_t... I>
consteval bool sorts_reversed_all(std::index_sequence<I...>) {
  return (sorts_reversed<I>() && ...);
}

static_assert(sorts_reversed_all(std::make_index_sequence<33>{}));

// custom comparator
static_assert(sorted(std::array{1, 3, 2}, std::greater{}) ==
              std::array{3, 2, 1});
static_assert(sorted(std::array{3, 2, 1}, std::greater{}) ==
              std::array{3, 2, 1});

// projections
struct kv {
  int key;
  int value;
  constexpr bool operator==(kv const&) const = default;
};

struct boxed {
  int v;
  constexpr int unboxed() const { return v; }
  constexpr bool operator==(boxed const&) const = default;
};

// pointer to member data
static_assert(sorted(std::array{kv{3, 30}, kv{1, 10}, kv{2, 20}}, std::less{},
                     &kv::key) == std::array{kv{1, 10}, kv{2, 20}, kv{3, 30}});

// pointer to member function (goes through the `std::invoke` fallback)
static_assert(sorted(std::array{boxed{3}, boxed{1}, boxed{2}}, std::less{},
                     &boxed::unboxed) ==
              std::array{boxed{1}, boxed{2}, boxed{3}});

// arbitrary callable
static_assert(sorted(std::array{-3, 1, -2}, std::less{}, [](int v) {
                return v < 0 ? -v : v;
              }) == std::array{1, -2, -3});

// projection combined with a custom comparator
static_assert(sorted(std::array{kv{1, 10}, kv{3, 30}, kv{2, 20}},
                     std::greater{},
                     &kv::key) == std::array{kv{3, 30}, kv{2, 20}, kv{1, 10}});

// stability
static_assert(sorted(std::array{kv{1, 1}, kv{0, 2}, kv{1, 3}, kv{0, 4},
                                kv{1, 5}},
                     std::less{}, &kv::key) ==
              std::array{kv{0, 2}, kv{0, 4}, kv{1, 1}, kv{1, 3}, kv{1, 5}});

// non-trivial element types
static_assert(sorted(std::array{"pear"sv, "apple"sv, "fig"sv}) ==
              std::array{"apple"sv, "fig"sv, "pear"sv});

// the implementation must never default construct `T`
struct no_default {
  int v;
  constexpr explicit no_default(int x)
      : v{x} {}
  constexpr bool operator==(no_default const&) const = default;
  constexpr auto operator<=>(no_default const&) const = default;
};

static_assert(sorted(std::array{no_default{3}, no_default{1}, no_default{2}}) ==
              std::array{no_default{1}, no_default{2}, no_default{3}});

// Sorting a shuffled 0..N-1 sequence must yield the identity, which verifies
// both ordering and that the result is a permutation of the input.
template <std::size_t N>
consteval std::array<int, N> shuffled(std::uint32_t seed) {
  std::array<int, N> arr{};

  for (std::size_t i = 0; i < N; ++i) {
    arr[i] = static_cast<int>(i);
  }

  for (std::size_t i = N; i > 1; --i) {
    seed = seed * 1664525u + 1013904223u;
    auto const j = (seed >> 8) % i;
    auto const tmp = arr[i - 1];
    arr[i - 1] = arr[j];
    arr[j] = tmp;
  }

  return arr;
}

template <std::size_t N>
consteval bool sorts_shuffled(std::uint32_t seed) {
  auto arr = shuffled<N>(seed);

  compile_time_sort(arr);

  for (std::size_t i = 0; i < N; ++i) {
    if (arr[i] != static_cast<int>(i)) {
      return false;
    }
  }

  return true;
}

static_assert(sorts_shuffled<1023>(1));
static_assert(sorts_shuffled<1024>(2));
static_assert(sorts_shuffled<1025>(3));

// Cross-check against the standard library on input with many duplicates,
// and verify stability by checking that equal keys keep their input order.
template <std::size_t N>
consteval bool matches_std_sort(std::uint32_t seed) {
  std::array<kv, N> input{};

  for (std::size_t i = 0; i < N; ++i) {
    seed = seed * 1664525u + 1013904223u;
    input[i] = kv{static_cast<int>((seed >> 16) % 8), static_cast<int>(i)};
  }

  auto actual = input;
  compile_time_sort(actual, std::less{}, &kv::key);

  auto expected = input;
  std::ranges::sort(expected, std::less{}, &kv::key);

  // keys must match the standard library's result
  for (std::size_t i = 0; i < N; ++i) {
    if (actual[i].key != expected[i].key) {
      return false;
    }
  }

  // equal keys retain their relative input order
  for (std::size_t i = 1; i < N; ++i) {
    if (actual[i - 1].key == actual[i].key &&
        actual[i - 1].value >= actual[i].value) {
      return false;
    }
  }

  return true;
}

static_assert(matches_std_sort<200>(0xdeadbeef));

} // namespace

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
#include <bit>
#include <limits>
#include <numeric>
#include <random>
#include <string_view>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <dwarfs/writer/internal/nilsimsa.h>
#include <dwarfs/writer/internal/similarity_ordering.h>

namespace {

using namespace dwarfs::writer::internal;
using namespace std::string_view_literals;

class nilsimsa_tester {
 public:
  void update(std::string_view data) {
    ns_.update(reinterpret_cast<uint8_t const*>(data.data()), data.size());
  }

  std::string digest() {
    nilsimsa::hash_type hash;
    ns_.finalize(hash);
    return fmt::format("{:016x}{:016x}{:016x}{:016x}", hash[3], hash[2],
                       hash[1], hash[0]);
  }

  static std::string hash(std::string_view data) {
    nilsimsa_tester ns;
    ns.update(data);
    return ns.digest();
  }

 private:
  nilsimsa ns_;
};

} // namespace

TEST(nilsimsa, empty) {
  EXPECT_EQ(nilsimsa_tester::hash(""),
            "0000000000000000000000000000000000000000000000000000000000000000");
}

TEST(nilsimsa, abcdefgh) {
  EXPECT_EQ(nilsimsa_tester::hash("abcdefgh"),
            "14c8118000000000030800000004042004189020001308014088003280000078");
}

TEST(nilsimsa, incremental) {
  nilsimsa_tester ns;

  ns.update("a");
  ns.update("bc");
  ns.update("defgh");
  EXPECT_EQ(ns.digest(),
            "14c8118000000000030800000004042004189020001308014088003280000078");

  ns.update("i");
  ns.update("jk");
  EXPECT_EQ(ns.digest(),
            "14c811840010000c0328200108040630041890200217582d4098103280000078");
}

TEST(nilsimsa, moreabc) {
  constexpr auto input = "abcdefghijklmnopqrstuvwxyz"sv;
  constexpr std::array<std::string_view, input.size()> expected{
      "0000000000000000000000000000000000000000000000000000000000000000",
      "0000000000000000000000000000000000000000000000000000000000000000",
      "0040000000000000000000000000000000000000000000000000000000000000",
      "0440000000000000000000000000000000100000000000000008000000000000",
      "0440008000000000000000000000000000100020001200000008001200000050",
      "04c0018000000000000000000000000004188020001200000088001280000058",
      "04c8118000000000030000000000002004188020001208004088001280000078",
      "14c8118000000000030800000004042004189020001308014088003280000078",
      "14c8118400000000030800010804043004189020021318094098003280000078",
      "14c81184000000000308200108040430041890200217580d4098103280000078",
      "14c811840010000c0328200108040630041890200217582d4098103280000078",
      "14c811840010000ca328200108044630041890200a17586d4298103280000078",
      "14ca11850010000ca328200188044630041898200a17586dc2d8103284000078",
      "14ca11850030004ca3a8200188044630041898200a17586dc2d8107284000078",
      "14ca11850032004ca3a8284188044730041898200a17586dc2d8107384000078",
      "94ca11850432005ca3a828418804473004199c200a17586dc2d8107384004178",
      "94ca11850433005ca3a82841880447341419be200a17586dc2d8107384004178",
      "94ca11850433005ca3a82841a88457341419be201a17586dc6d8107384084178",
      "94ca11850533005ca3b82841a88657361419be201a17586dc6d8107384084178",
      "94ca11850533005ca3b82841aa8657371419be201a17587dc6d81077840c4178",
      "94ca15850533005ca3b92841aa8657371419be201a17587dd6d81077844cc178",
      "94ca15850533005ca3b92849aa8657371419be201a17587fd6d81077844cc978",
      "94ca15850533045cabb92869aa8657371419bea01a17587fd6f81077c44cc978",
      "94ca95850533045cabb93869aa8657371499beb01a17587fd6f8107fc44cc978",
      "94ca95850733045cabb93869aa8657373499beb01a17587fd6f9107fc54cc978",
      "94ca95850773045cabb93869ba8657373499beb81a17587fd6f9107fc54cc978",
  };

  for (size_t i = 0; i < input.size(); ++i) {
    EXPECT_EQ(nilsimsa_tester::hash(input.substr(0, i + 1)), expected[i])
        << "i=" << i;
  }
}

namespace {

using rank_type = uint32_t;
constexpr auto invalid_rank = std::numeric_limits<rank_type>::max();
constexpr size_t exact_tail = 256;
using sort_key = std::pair<uint64_t, rank_type>;
using test_hash = nilsimsa::hash_type;

int hamming(test_hash const& a, test_hash const& b) {
  int d = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    d += std::popcount(a[i] ^ b[i]);
  }
  return d;
}

class reference_projection {
 public:
  explicit reference_projection(uint64_t& seed) {
    std::array<unsigned, 256> bits;
    std::iota(bits.begin(), bits.end(), 0);
    for (unsigned i = 0; i < selected_.size(); ++i) {
      auto z = (seed += 0x9e3779b97f4a7c15ULL);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
      z ^= z >> 31;
      std::swap(bits[i], bits[i + z % (bits.size() - i)]);
      selected_[i] = bits[i];
    }
  }

  uint64_t operator()(test_hash const& hash) const {
    uint64_t key = 0;
    for (auto bit : selected_) {
      key = (key << 1) | ((hash[bit / 64] >> (bit % 64)) & 1);
    }
    return key;
  }

  auto const& selected_bits() const { return selected_; }

 private:
  std::array<unsigned, 64> selected_{};
};

std::vector<test_hash const*>
hash_pointers(std::vector<test_hash> const& hashes) {
  std::vector<test_hash const*> ptrs;
  for (auto const& hash : hashes) {
    ptrs.push_back(&hash);
  }
  return ptrs;
}

std::vector<test_hash> random_hashes(size_t count) {
  std::mt19937_64 rng{42};
  std::vector<test_hash> hashes(count);
  for (auto& hash : hashes) {
    for (auto& word : hash) {
      word = rng();
    }
  }
  return hashes;
}

void check_ordering_path(std::vector<test_hash> const& hashes,
                         nilsimsa_ordering_result const& result) {
  ASSERT_EQ(hashes.size(), result.order.size());
  auto order = result.order;

  std::ranges::sort(order);

  for (size_t i = 0; i < order.size(); ++i) {
    ASSERT_EQ(i, order[i]);
  }

  uint64_t distance = 0;

  for (size_t i = 1; i < result.order.size(); ++i) {
    distance += hamming(hashes[result.order[i - 1]], hashes[result.order[i]]);
  }

  EXPECT_EQ(distance, result.total_distance);
}

} // namespace

TEST(nilsimsa_ordering, empty_and_single) {
  std::vector<test_hash> hashes;
  EXPECT_TRUE(order_nilsimsa_hashes(hash_pointers(hashes), 8, 8).order.empty());
  hashes.resize(1);
  auto result = order_nilsimsa_hashes(hash_pointers(hashes), 8, 8);
  ASSERT_EQ(result.order.size(), 1);
  EXPECT_EQ(result.order.get(0), 0);
  EXPECT_EQ(result.comparisons, 0);
}

TEST(nilsimsa_ordering, small_exact) {
  auto hashes = random_hashes(128);
  auto ptrs = hash_pointers(hashes);
  auto result = order_nilsimsa_hashes(ptrs, 8, 8);

  check_ordering_path(hashes, result);

  ASSERT_EQ(result.order.front(), 0);

  std::vector<bool> used(hashes.size());
  used[0] = true;

  for (size_t i = 1; i < result.order.size(); ++i) {
    auto const& query = hashes[result.order[i - 1]];
    auto best = std::pair{257, uint32_t{0}};

    for (uint32_t j = 0; j < hashes.size(); ++j) {
      if (!used[j]) {
        best = std::min(best, std::pair{hamming(query, hashes[j]), j});
      }
    }

    ASSERT_EQ(best.second, result.order[i]);

    used[best.second] = true;
  }

  EXPECT_EQ(result.comparisons, hashes.size() * (hashes.size() - 1) / 2);
}

TEST(nilsimsa_ordering, deterministic_and_bounded) {
  auto hashes = random_hashes(8192);
  auto ptrs = hash_pointers(hashes);
  auto a = order_nilsimsa_hashes(ptrs, 4, 8);
  auto b = order_nilsimsa_hashes(ptrs, 4, 8);
  check_ordering_path(hashes, a);
  EXPECT_EQ(a.order, b.order);
  EXPECT_EQ(a.comparisons, b.comparisons);
  EXPECT_EQ(a.total_distance, b.total_distance);
  EXPECT_LE(a.comparisons, 2 * 4 * 8 * hashes.size() + 256 * 257 / 2);
}

TEST(nilsimsa_ordering, identical_hashes) {
  std::vector<test_hash> hashes(10000);
  auto result = order_nilsimsa_hashes(hash_pointers(hashes), 8, 4);
  check_ordering_path(hashes, result);
  EXPECT_EQ(result.total_distance, 0);
  EXPECT_LE(result.comparisons, 2 * 8 * 4 * hashes.size() + 256 * 257 / 2);
  EXPECT_TRUE(std::is_sorted(result.order.begin(), result.order.end()));
}

TEST(nilsimsa_ordering, invalid_options) {
  std::vector<test_hash const*> empty;
  EXPECT_THROW(order_nilsimsa_hashes(empty, 0, 8), std::invalid_argument);
  EXPECT_THROW(order_nilsimsa_hashes(empty, 33, 8), std::invalid_argument);
  EXPECT_THROW(order_nilsimsa_hashes(empty, 8, 0), std::invalid_argument);
  EXPECT_THROW(order_nilsimsa_hashes(empty, 8, 129), std::invalid_argument);
  EXPECT_THROW(order_nilsimsa_hashes(empty, 8, 8, 9), std::invalid_argument);
}

namespace {

nilsimsa_ordering_result
reference_ordering(std::vector<test_hash> const& hashes, size_t passes,
                   size_t window) {
  auto const n = hashes.size();
  std::vector<std::vector<rank_type>> lists(passes);
  uint64_t seed = 0x6a09e667f3bcc909ULL;

  for (auto& list : lists) {
    reference_projection project(seed);
    std::vector<sort_key> keys;

    for (rank_type i = 0; i < n; ++i) {
      keys.push_back({project(hashes[i]), i});
    }

    std::sort(keys.begin(), keys.end());

    for (auto const& key : keys) {
      list.push_back(std::get<1>(key));
    }
  }

  nilsimsa_ordering_result result;

  result.order =
      dwarfs::container::packed_int_vector<rank_type>(std::bit_width(n));

  rank_type current = 0;

  while (result.order.size() < n) {
    result.order.push_back(current);
    auto const remaining = n - result.order.size();

    if (remaining == 0) {
      break;
    }

    bool const exact = remaining <= exact_tail;
    std::pair<int, rank_type> best{257, invalid_rank};

    for (size_t p = 0; p < lists.size(); ++p) {
      auto& list = lists[p];
      auto pos = static_cast<size_t>(
          std::find(list.begin(), list.end(), current) - list.begin());
      list.erase(list.begin() + pos);

      if (!exact || p == 0) {
        auto first = exact || pos < window ? 0 : pos - window;
        auto last = exact ? list.size() : std::min(list.size(), pos + window);
        for (auto i = first; i < last; ++i) {
          auto r = list[i];
          ++result.comparisons;
          best =
              std::min(best, std::pair{hamming(hashes[current], hashes[r]), r});
        }
      }
    }

    result.total_distance += best.first;
    current = best.second;
  }

  return result;
}

} // namespace

TEST(nilsimsa_ordering, packed_fixed_matches_reference) {
  for (size_t n : {511, 512, 513, 1024}) {
    SCOPED_TRACE(n);

    auto hashes = random_hashes(n);
    hashes[4] = hashes[1];
    hashes[6] = hashes[1];

    auto ptrs = hash_pointers(hashes);

    for (size_t window : {1, 8, 127}) {
      SCOPED_TRACE(window);

      auto expected = reference_ordering(hashes, 4, window);
      auto actual = order_nilsimsa_hashes(ptrs, 4, window);

      EXPECT_EQ(actual.order, expected.order);
      EXPECT_EQ(actual.comparisons, expected.comparisons);
      EXPECT_EQ(actual.total_distance, expected.total_distance);
      EXPECT_EQ(actual.rank_bits, std::bit_width(n));
      EXPECT_GE(actual.projection_bytes * 8,
                (actual.rank_bits + actual.offset_bits) * n * 4);
    }
  }
}

TEST(nilsimsa_ordering, packed_sentinel_boundary) {
  for (size_t n : {65535, 65536, 65537}) {
    SCOPED_TRACE(n);

    std::vector<test_hash> hashes(n);
    auto result = order_nilsimsa_hashes(hash_pointers(hashes), 1, 1);

    check_ordering_path(hashes, result);

    EXPECT_EQ(result.rank_bits, std::bit_width(n));
    EXPECT_EQ(result.total_distance, 0);
    EXPECT_TRUE(std::is_sorted(result.order.begin(), result.order.end()));
  }
}

TEST(nilsimsa_ordering, skewed_and_truncated_keys_match_reference) {
  auto hashes = random_hashes(1025);
  uint64_t seed = 0x6a09e667f3bcc909ULL;
  reference_projection project(seed);

  auto const key_bits = 64 - std::bit_width(hashes.size());

  for (auto& hash : hashes) {
    for (auto i = 0; i < key_bits; ++i) {
      auto const bit = project.selected_bits()[i];
      hash[bit / 64] &= ~(uint64_t{1} << (bit % 64));
    }
  }

  hashes[17] = hashes[0];
  auto const expected = reference_ordering(hashes, 4, 16);
  auto ptrs = hash_pointers(hashes);

  for (size_t offset : {0, 1, 2, 4, 8}) {
    SCOPED_TRACE(offset);

    auto actual = order_nilsimsa_hashes(ptrs, 4, 16, offset);
    EXPECT_EQ(actual.order, expected.order);
    EXPECT_EQ(actual.total_distance, expected.total_distance);
    EXPECT_EQ(actual.comparisons, expected.comparisons);
  }
}

TEST(nilsimsa_ordering, sparse_active_blocks_match_reference) {
  auto hashes = random_hashes(4097);
  auto const expected = reference_ordering(hashes, 1, 32);
  auto actual = order_nilsimsa_hashes(hash_pointers(hashes), 1, 32);
  EXPECT_EQ(actual.order, expected.order);
  EXPECT_EQ(actual.total_distance, expected.total_distance);
  EXPECT_EQ(actual.comparisons, expected.comparisons);
}

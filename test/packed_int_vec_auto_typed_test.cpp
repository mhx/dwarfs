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

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::container;
using ::testing::ElementsAreArray;

namespace {

constexpr std::array seeds{
    0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xdeadbeefdeadbeefULL,
    0x0badc0de0badc0deULL, 0xabcdef0123456789ULL, 0x0123456789abcdefULL,
};

template <typename Vec>
void expect_matches_model(Vec const& vec,
                          std::vector<typename Vec::value_type> const& model) {
  ASSERT_EQ(vec.size(), model.size());
  ASSERT_EQ(vec.empty(), model.empty());
  ASSERT_THAT(vec, ::testing::ElementsAreArray(model));

  for (std::size_t i = 0; i < model.size(); ++i) {
    ASSERT_EQ(vec[i], model[i]) << "index=" << i;
  }

  if (!model.empty()) {
    ASSERT_EQ(vec.front(), model.front());
    ASSERT_EQ(vec.back(), model.back());
  }
}

template <std::integral T>
struct auto_packed_vec {
  using type = auto_packed_int_vector<T>;
};

template <std::integral T>
struct compact_packed_vec {
  using type = compact_auto_packed_int_vector<T>;
};

template <std::integral T>
struct segmented_packed_vec {
  using type = segmented_packed_int_vector<T, 16 / sizeof(T)>;
};

} // namespace

template <typename Vec>
class auto_packed_int_vector_test : public ::testing::Test {};

using exact_value_preserving_vector_types =
    ::testing::Types<auto_packed_vec<int64_t>,       //
                     compact_packed_vec<int64_t>,    //
                     segmented_packed_vec<int64_t>,  //
                     auto_packed_vec<uint32_t>,      //
                     compact_packed_vec<uint32_t>,   //
                     segmented_packed_vec<uint32_t>, //
                     auto_packed_vec<uint16_t>,      //
                     compact_packed_vec<uint16_t>,   //
                     segmented_packed_vec<uint16_t>, //
                     auto_packed_vec<int8_t>,        //
                     compact_packed_vec<int8_t>,     //
                     segmented_packed_vec<int8_t>>;

TYPED_TEST_SUITE(auto_packed_int_vector_test,
                 exact_value_preserving_vector_types);

TYPED_TEST(auto_packed_int_vector_test, mixed_operations_stress) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;
  using rng_value_type =
      std::conditional_t<sizeof(value_type) == 1, int, value_type>;

  for (auto const seed : seeds) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<rng_value_type> value_dist(
        std::numeric_limits<value_type>::min(),
        std::numeric_limits<value_type>::max());

    vec_type vec;
    std::vector<value_type> model;

    for (std::size_t step = 0; step < 5000; ++step) {
      int const op = op_dist(rng);

      SCOPED_TRACE(::testing::Message() << "step=" << step << ", op=" << op
                                        << ", size=" << model.size());

      if (op < 30) {
        value_type const value = value_dist(rng);
        vec.push_back(value);
        model.push_back(value);

      } else if (op < 45) {
        if (!model.empty()) {
          vec.pop_back();
          model.pop_back();
        }

      } else if (op < 65) {
        if (!model.empty()) {
          std::size_t const i = rng() % model.size();
          auto const value = value_dist(rng);
          vec[i] = value;
          model[i] = value;
        }

      } else if (op < 80) {
        std::size_t const new_size = rng() % 128;
        auto const fill_value = value_dist(rng);

        vec.resize(new_size, fill_value);
        model.resize(new_size, fill_value);

      } else if (op < 90) {
        vec.optimize_storage();

      } else {
        vec.clear();
        model.clear();
      }

      expect_matches_model(vec, model);
    }
  }
}

TYPED_TEST(auto_packed_int_vector_test, copy_and_move_smoke) {
  using vec_type = typename TypeParam::type;

  vec_type vec;
  for (uint32_t v : {1, 2, 3, 31, 5, 6, 7}) {
    vec.push_back(v);
  }

  auto const expected = vec.unpack();

  vec_type copy{vec};
  EXPECT_THAT(copy.unpack(), ElementsAreArray(expected));

  copy[3] = 9;
  EXPECT_THAT(vec, ElementsAreArray(expected));

  vec_type moved{std::move(copy)};
  EXPECT_EQ(moved.size(), expected.size());
}

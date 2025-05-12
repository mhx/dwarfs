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

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dwarfs::reader {

enum class fsinfo_feature {
  version,
  history,
  metadata_summary,
  metadata_details,
  metadata_full_dump,
  frozen_analysis,
  frozen_layout,
  schema_raw_dump,
  directory_tree,
  section_details,
  chunk_details,
  num_fsinfo_feature_bits,
};

class fsinfo_features {
 public:
  constexpr fsinfo_features() = default;
  constexpr fsinfo_features(std::initializer_list<fsinfo_feature> features) {
    for (auto f : features) {
      set(f);
    }
  }

  static constexpr fsinfo_features all() { return fsinfo_features().set_all(); }

  static int max_level();
  static fsinfo_features for_level(int level);
  static fsinfo_features parse(std::string_view features);

  std::string to_string() const;
  std::vector<std::string_view> to_string_views() const;

  constexpr bool has(fsinfo_feature f) const {
    return features_ & feature_bit(f);
  }

  constexpr fsinfo_features& set(fsinfo_feature f) {
    features_ |= feature_bit(f);
    return *this;
  }

  constexpr fsinfo_features& set_all() {
    features_ = ~feature_type{} >>
                (max_feature_bits -
                 static_cast<size_t>(fsinfo_feature::num_fsinfo_feature_bits));
    return *this;
  }

  constexpr fsinfo_features& clear(fsinfo_feature f) {
    features_ &= ~feature_bit(f);
    return *this;
  }

  constexpr fsinfo_features& reset() {
    features_ = 0;
    return *this;
  }

  constexpr fsinfo_features& operator|=(fsinfo_features const& other) {
    features_ |= other.features_;
    return *this;
  }

  constexpr fsinfo_features& operator|=(fsinfo_feature f) {
    set(f);
    return *this;
  }

  constexpr bool operator&(fsinfo_feature f) const { return has(f); }

 private:
  // can be upgraded to std::bitset if needed and when it's constexpr
  using feature_type = uint64_t;

  static constexpr feature_type feature_bit(fsinfo_feature n) {
    return static_cast<feature_type>(1) << static_cast<size_t>(n);
  }

  static constexpr size_t max_feature_bits{
      std::numeric_limits<feature_type>::digits};
  static constexpr size_t num_feature_bits{
      static_cast<size_t>(fsinfo_feature::num_fsinfo_feature_bits)};
  static_assert(num_feature_bits <= max_feature_bits);

  feature_type features_{0};
};

} // namespace dwarfs::reader

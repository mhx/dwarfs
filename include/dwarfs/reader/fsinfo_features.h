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
  static fsinfo_features parse(std::string_view str);

  std::string to_string() const;
  std::vector<std::string_view> to_string_views() const;

  constexpr bool has(fsinfo_feature f) const {
    return features_ & (1 << static_cast<size_t>(f));
  }

  constexpr fsinfo_features& set(fsinfo_feature f) {
    features_ |= (1 << static_cast<size_t>(f));
    return *this;
  }

  constexpr fsinfo_features& set_all() {
    features_ = ~feature_type{} >>
                (max_feature_bits -
                 static_cast<size_t>(fsinfo_feature::num_fsinfo_feature_bits));
    return *this;
  }

  constexpr fsinfo_features& clear(fsinfo_feature f) {
    features_ &= ~(1 << static_cast<size_t>(f));
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
  static constexpr size_t max_feature_bits{
      std::numeric_limits<feature_type>::digits};
  static constexpr size_t num_feature_bits{
      static_cast<size_t>(fsinfo_feature::num_fsinfo_feature_bits)};
  static_assert(num_feature_bits <= max_feature_bits);

  feature_type features_{0};
};

} // namespace dwarfs::reader

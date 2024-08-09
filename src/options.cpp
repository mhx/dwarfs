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

#include <algorithm>
#include <array>
#include <ostream>
#include <string>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/options.h>
#include <dwarfs/string.h>

namespace dwarfs {

namespace {

constexpr std::array level_features{
    /* 0 */ fsinfo_features(),
    /* 1 */
    fsinfo_features(
        {fsinfo_feature::version, fsinfo_feature::metadata_summary}),
    /* 2 */
    fsinfo_features({fsinfo_feature::frozen_analysis, fsinfo_feature::history}),
    /* 3 */
    fsinfo_features(
        {fsinfo_feature::metadata_details, fsinfo_feature::section_details}),
    /* 4 */
    fsinfo_features(
        {fsinfo_feature::directory_tree, fsinfo_feature::frozen_layout}),
    /* 5 */ fsinfo_features({fsinfo_feature::chunk_details}),
    /* 6 */ fsinfo_features({fsinfo_feature::metadata_full_dump}),
};

constexpr std::array<std::pair<fsinfo_feature, std::string_view>,
                     static_cast<int>(fsinfo_feature::num_fsinfo_feature_bits)>
    fsinfo_feature_names{{
#define FSINFO_FEATURE_PAIR_(f) {fsinfo_feature::f, #f}
        FSINFO_FEATURE_PAIR_(version),
        FSINFO_FEATURE_PAIR_(history),
        FSINFO_FEATURE_PAIR_(metadata_summary),
        FSINFO_FEATURE_PAIR_(metadata_details),
        FSINFO_FEATURE_PAIR_(metadata_full_dump),
        FSINFO_FEATURE_PAIR_(frozen_analysis),
        FSINFO_FEATURE_PAIR_(frozen_layout),
        FSINFO_FEATURE_PAIR_(directory_tree),
        FSINFO_FEATURE_PAIR_(section_details),
        FSINFO_FEATURE_PAIR_(chunk_details),
#undef FSINFO_FEATURE_PAIR_
    }};

constexpr bool fsinfo_feature_names_in_order() {
  for (size_t i = 0; i < fsinfo_feature_names.size(); ++i) {
    if (fsinfo_feature_names[i].first != static_cast<fsinfo_feature>(i)) {
      return false;
    }
  }
  return true;
}

static_assert(fsinfo_feature_names_in_order());

} // namespace

std::ostream& operator<<(std::ostream& os, file_order_mode mode) {
  std::string modestr{"unknown"};

  switch (mode) {
  case file_order_mode::NONE:
    modestr = "none";
    break;
  case file_order_mode::PATH:
    modestr = "path";
    break;
  case file_order_mode::REVPATH:
    modestr = "revpath";
    break;
  case file_order_mode::SIMILARITY:
    modestr = "similarity";
    break;
  case file_order_mode::NILSIMSA:
    modestr = "nilsimsa";
    break;
  }

  return os << modestr;
}

std::ostream& operator<<(std::ostream& os, block_cache_options const& opts) {
  os << fmt::format(
      "max_bytes={}, num_workers={}, decompress_ratio={}, mm_release={}, "
      "init_workers={}, disable_block_integrity_check={}",
      opts.max_bytes, opts.num_workers, opts.decompress_ratio, opts.mm_release,
      opts.init_workers, opts.disable_block_integrity_check);
  return os;
}

mlock_mode parse_mlock_mode(std::string_view mode) {
  if (mode == "none") {
    return mlock_mode::NONE;
  }
  if (mode == "try") {
    return mlock_mode::TRY;
  }
  if (mode == "must") {
    return mlock_mode::MUST;
  }
  DWARFS_THROW(runtime_error, fmt::format("invalid lock mode: {}", mode));
}

int fsinfo_features::max_level() {
  return static_cast<int>(level_features.size()) - 1;
}

fsinfo_features fsinfo_features::for_level(int level) {
  fsinfo_features features;

  level = std::min<int>(level, level_features.size() - 1);

  for (int i = 0; i <= level; ++i) {
    features |= level_features[i];
  }

  return features;
}

fsinfo_features fsinfo_features::parse(std::string_view features) {
  fsinfo_features result;

  for (auto const& f : split_view<std::string_view>(features, ',')) {
    auto const it =
        std::find_if(fsinfo_feature_names.begin(), fsinfo_feature_names.end(),
                     [&f](auto const& p) { return f == p.second; });

    if (it == fsinfo_feature_names.end()) {
      DWARFS_THROW(runtime_error, fmt::format("invalid feature: \"{}\"", f));
    }

    result |= it->first;
  }

  return result;
}

std::string fsinfo_features::to_string() const {
  std::string result;

  for (size_t bit = 0; bit < num_feature_bits; ++bit) {
    if (has(static_cast<fsinfo_feature>(bit))) {
      if (!result.empty()) {
        result += ',';
      }
      result += fsinfo_feature_names[bit].second;
    }
  }

  return result;
}

std::vector<std::string_view> fsinfo_features::to_string_views() const {
  std::vector<std::string_view> result;

  for (size_t bit = 0; bit < num_feature_bits; ++bit) {
    if (has(static_cast<fsinfo_feature>(bit))) {
      result.push_back(fsinfo_feature_names[bit].second);
    }
  }

  return result;
}

} // namespace dwarfs

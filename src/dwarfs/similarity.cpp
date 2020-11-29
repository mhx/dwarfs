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
#include <utility>

#include <folly/Hash.h>

#include "dwarfs/similarity.h"

namespace dwarfs {

uint32_t get_similarity_hash(const uint8_t* data, size_t size) {
  constexpr size_t hist_bits = 8;
  std::array<std::pair<uint32_t, uint32_t>, 1 << hist_bits> vec;
  for (size_t i = 0; i < vec.size(); ++i) {
    vec[i].first = 0;
    vec[i].second = i;
  }
  uint32_t val = 0;
  constexpr uint32_t mask = (1 << hist_bits) - 1;
  for (size_t off = 0; off < size; ++off) {
    val = (val << 8) | data[off];
    if (off >= 3) {
      auto hv = folly::hash::jenkins_rev_mix32(val);
      ++vec[hv & mask].first;
    }
  }
  std::partial_sort(vec.begin(), vec.begin() + 4, vec.end(),
                    [](const auto& a, const auto& b) {
                      return a.first > b.first ||
                             (a.first == b.first && a.second < b.second);
                    });
  return (vec[0].second << 24) | (vec[1].second << 16) | (vec[2].second << 8) |
         (vec[3].second << 0);
}
} // namespace dwarfs

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

/**
 * Simple locality sensitive hashing function
 *
 * There are probably much better LSH algorithms available, but this
 * one is fast and easy enough to understand.
 *
 * It builds histogram of `2**hist_bits` buckets. The buckets are
 * indexed by the `hist_bits` LSBs of a rolling 32-bit hash, so we
 * end up inserting a value for every 4-byte substring of the data.
 *
 * Assuming that the distribution of these substrings are going to
 * be similar across similar files, the return value composed of the
 * indices of the 4 most common buckets.
 */

class similarity::impl {
  static constexpr size_t hist_bits = 8;
  static constexpr uint32_t mask = (1 << hist_bits) - 1;

 public:
  impl() {
    for (size_t i = 0; i < vec_.size(); ++i) {
      vec_[i].first = 0;
      vec_[i].second = i;
    }
  }

  void update(uint8_t const* data, size_t size) {
    for (size_t off = 0; off < size; ++off) {
      val_ = (val_ << 8) | data[off];
      if (size_ + off >= 3) {
        auto hv = folly::hash::jenkins_rev_mix32(val_);
        ++vec_[hv & mask].first;
      }
    }
    size_ += size;
  }

  uint32_t finalize() {
    std::partial_sort(vec_.begin(), vec_.begin() + 4, vec_.end(),
                      [](const auto& a, const auto& b) {
                        return a.first > b.first ||
                               (a.first == b.first && a.second < b.second);
                      });
    return (vec_[0].second << 24) | (vec_[1].second << 16) |
           (vec_[2].second << 8) | (vec_[3].second << 0);
  }

 private:
  std::array<std::pair<uint32_t, uint32_t>, 1 << hist_bits> vec_;
  uint32_t val_{0};
  size_t size_{0};
};

similarity::similarity()
    : impl_{std::make_unique<impl>()} {}
similarity::~similarity() = default;

void similarity::update(uint8_t const* data, size_t size) {
  impl_->update(data, size);
}

uint32_t similarity::finalize() const { return impl_->finalize(); }

} // namespace dwarfs

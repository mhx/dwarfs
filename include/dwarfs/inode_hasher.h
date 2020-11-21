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

#include <unordered_map>
#include <vector>

#include "cyclic_hash.h"
#include "logger.h"

namespace dwarfs {

template <typename LoggerPolicy, typename HashType>
class inode_hasher {
 public:
  using result_type =
      typename std::unordered_map<size_t, std::vector<HashType>>;

  inode_hasher(logger& lgr, byte_hash<HashType>& byte_hasher,
               const std::vector<size_t>& blockhash_window_size)
      : byte_hasher_(byte_hasher)
      , window_(blockhash_window_size)
      , log_(lgr) {}

  void operator()(result_type& m, const uint8_t* data, size_t size) const {
    auto tt = log_.timed_trace();

    for (size_t wsize : window_) {
      if (size >= wsize) {
        hashit(m[wsize], wsize, data, size);
      }
    }

    tt << "hashed " << size << " bytes";
  }

 private:
  void hashit(std::vector<HashType>& vec, size_t window, const uint8_t* data,
              size_t size) const {
    cyclic_hash<HashType> hasher(window, byte_hasher_);

    vec.clear();
    vec.reserve(size - window);

    size_t i = 0;

    while (i < window) {
      hasher.update(data[i++]);
    }

    vec.push_back(hasher());

    while (i < size) {
      hasher.update(data[i - window], data[i]);
      vec.push_back(hasher());
      ++i;
    }
  }

  byte_hash<HashType>& byte_hasher_;
  const std::vector<size_t> window_;
  log_proxy<LoggerPolicy> log_;
};
} // namespace dwarfs

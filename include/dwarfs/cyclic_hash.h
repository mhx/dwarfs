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

#include <array>
#include <random>
#include <stdexcept>

namespace dwarfs {

template <typename T>
class byte_hash {
 public:
  byte_hash() {
    std::default_random_engine generator;
    std::uniform_int_distribution<T> distribution(0, static_cast<T>(-1));

    for (size_t i = 0; i < hash_.size(); ++i) {
      hash_[i] = distribution(generator);
    }
  }

  T operator()(uint8_t c) const { return hash_[c]; }

 private:
  std::array<T, std::numeric_limits<uint8_t>::max() + 1> hash_;
};

template <typename T>
class cyclic_hash {
 public:
  cyclic_hash(size_t window_size, const byte_hash<T>& ch)
      : hash_(0)
      , byte_hash_(ch) {
    if (window_size % hash_bits) {
      throw std::runtime_error("unsupported window size");
    }
  }

  void reset() { hash_ = 0; }

  void update(uint8_t outbyte, uint8_t inbyte) {
    hash_ = rol(hash_) ^ byte_hash_(outbyte) ^ byte_hash_(inbyte);
  }

  void update(uint8_t inbyte) { hash_ = rol(hash_) ^ byte_hash_(inbyte); }

  T operator()() const { return hash_; }

 private:
  static const size_t hash_bits = 8 * sizeof(T);

  inline T rol(T x) const { return (x << 1) | (x >> (hash_bits - 1)); }

  T hash_;
  const byte_hash<T>& byte_hash_;
};
} // namespace dwarfs

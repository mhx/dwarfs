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

#include <chrono>
#include <deque>

namespace dwarfs {

template <typename T>
class speedometer {
 public:
  speedometer(std::chrono::milliseconds window_length)
      : window_length_{window_length} {}

  void put(T s) {
    auto now = std::chrono::steady_clock::now();
    auto old = now - window_length_;

    while (!samples_.empty() && samples_.front().first < old) {
      samples_.pop_front();
    }

    samples_.emplace_back(now, s);
  }

  T num_per_second() const {
    if (samples_.size() < 2) {
      return T();
    }
    auto const& first = samples_.front();
    auto const& last = samples_.back();
    auto dt = last.first - first.first;
    auto dv = last.second - first.second;
    return (1000 * dv) /
           std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
  }

  void clear() { samples_.clear(); }

 private:
  std::deque<std::pair<std::chrono::steady_clock::time_point, T>> samples_;
  std::chrono::milliseconds window_length_;
};

} // namespace dwarfs

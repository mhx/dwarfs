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

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace dwarfs::writer::internal {

struct time_conversion_factors {
  std::optional<uint32_t> sec;
  std::optional<uint32_t> nsec;
};

class time_resolution_converter {
 public:
  explicit time_resolution_converter(
      std::optional<std::chrono::nanoseconds> resolution);
  time_resolution_converter(std::optional<std::chrono::nanoseconds> resolution,
                            time_conversion_factors const& old_conv);

  bool requires_conversion() const;

  time_conversion_factors new_conversion_factors() const { return new_conv_; }

  template <std::integral T>
  T convert_offset(T val) const {
    return val / conv_sec_;
  }

  template <std::integral T>
  T convert_subsec(T val) const {
    return conv_subsec_ == 0 ? T{0} : val / conv_subsec_;
  }

  template <std::integral T>
  T align_offset(T val) const {
    return (val / conv_sec_) * conv_sec_;
  }

 private:
  struct init_data;
  explicit time_resolution_converter(init_data const& data);

  uint32_t const conv_sec_{0};
  uint32_t const conv_subsec_{0};
  time_conversion_factors const new_conv_;
};

} // namespace dwarfs::writer::internal

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * ricepp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ricepp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ricepp.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <concepts>
#include <span>
#include <vector>

namespace ricepp {

template <std::unsigned_integral PixelT>
class codec_interface {
 public:
  using pixel_type = PixelT;

  virtual ~codec_interface() = default;

  [[nodiscard]] virtual std::vector<uint8_t>
  encode(std::span<pixel_type const> input) const = 0;

  virtual size_t worst_case_encoded_bytes(size_t pixel_count) const = 0;

  virtual size_t
  worst_case_encoded_bytes(std::span<pixel_type const> input) const = 0;

  virtual std::span<uint8_t>
  encode(std::span<uint8_t> output,
         std::span<pixel_type const> input) const = 0;

  virtual void decode(std::span<pixel_type> output,
                      std::span<uint8_t const> input) const = 0;
};

} // namespace ricepp

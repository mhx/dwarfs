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
#include <memory>

#include <ricepp/codec_interface.h>

namespace ricepp {

struct codec_config;

namespace detail {

enum class cpu_variant {
  fallback,
  has_bmi2,
  has_bmi2_avx512,
};

template <std::unsigned_integral PixelT, cpu_variant CPU>
std::unique_ptr<codec_interface<PixelT>>
create_codec_cpuspecific_(codec_config const& config);

} // namespace detail
} // namespace ricepp

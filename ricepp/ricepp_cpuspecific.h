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
#include <stdexcept>
#include <string_view>

#include <ricepp/decoder_interface.h>
#include <ricepp/encoder_interface.h>

#include "cpu_variant.h"

namespace ricepp {

struct codec_config;

namespace detail {

template <template <std::unsigned_integral> typename CodecInterface,
          template <std::unsigned_integral, cpu_variant> typename CreateCodec,
          std::unsigned_integral PixelValueType>
std::unique_ptr<CodecInterface<PixelValueType>>
create_codec_cpuspecific(codec_config const& config) {
  switch (get_cpu_variant()) {
#ifdef RICEPP_CPU_BMI2_AVX512
  case detail::cpu_variant::has_bmi2_avx512:
    show_cpu_variant_once("BMI2+AVX512");
    return CreateCodec<PixelValueType,
                       detail::cpu_variant::has_bmi2_avx512>::create(config);
#endif

#ifdef RICEPP_CPU_BMI2
  case detail::cpu_variant::has_bmi2:
    show_cpu_variant_once("BMI2");
    return CreateCodec<PixelValueType, detail::cpu_variant::has_bmi2>::create(
        config);
#endif

  default:
    show_cpu_variant_once("fallback");
    return CreateCodec<PixelValueType, detail::cpu_variant::fallback>::create(
        config);
  }

  throw std::runtime_error("internal error: unknown CPU variant");
}

template <std::unsigned_integral PixelT, cpu_variant CPU>
struct encoder_cpuspecific_ {
  static std::unique_ptr<encoder_interface<PixelT>>
  create(codec_config const& config);
};

template <std::unsigned_integral PixelT, cpu_variant CPU>
struct decoder_cpuspecific_ {
  static std::unique_ptr<decoder_interface<PixelT>>
  create(codec_config const& config);
};

} // namespace detail
} // namespace ricepp

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
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

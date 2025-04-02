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

#include <cstdlib>
#include <iostream>

#include "cpu_variant.h"

namespace ricepp::detail {

namespace {

detail::cpu_variant get_cpu_variant_init() {
#ifndef _WIN32
#if defined(__has_builtin)
#if __has_builtin(__builtin_cpu_supports) &&                                   \
    (defined(RICEPP_CPU_BMI2) || defined(RICEPP_CPU_BMI2_AVX512))
  __builtin_cpu_init();

  bool const has_avx512vl = __builtin_cpu_supports("avx512vl");
  bool const has_avx512vbmi = __builtin_cpu_supports("avx512vbmi");
  bool const has_bmi2 = __builtin_cpu_supports("bmi2");

  if (has_avx512vl && has_avx512vbmi && has_bmi2) {
    return detail::cpu_variant::has_bmi2_avx512;
  }

  if (has_bmi2) {
    return detail::cpu_variant::has_bmi2;
  }
#endif
#endif
#endif

  return detail::cpu_variant::fallback;
}

} // namespace

detail::cpu_variant get_cpu_variant() {
  static detail::cpu_variant const variant = get_cpu_variant_init();
  return variant;
}

void show_cpu_variant(std::string_view variant) {
  if (std::getenv("RICEPP_SHOW_CPU_VARIANT")) {
    std::cerr << "ricepp: using " << variant << " CPU variant\n";
  }
}

void show_cpu_variant_once(std::string_view variant) {
  static auto const _ = [&variant]() {
    show_cpu_variant(variant);
    return true;
  }();
}

} // namespace ricepp::detail

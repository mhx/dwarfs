/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
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

#include <cstddef>
#include <cstdint>

namespace dwarfs::binary_literals {

constexpr auto operator""_KiB(unsigned long long value) {
  return static_cast<uint64_t>(value) << 10;
}

constexpr auto operator""_MiB(unsigned long long value) {
  return static_cast<uint64_t>(value) << 20;
}

constexpr auto operator""_GiB(unsigned long long value) {
  return static_cast<uint64_t>(value) << 30;
}

constexpr auto operator""_TiB(unsigned long long value) {
  return static_cast<uint64_t>(value) << 40;
}

constexpr auto operator""_KiBs(unsigned long long value) {
  return static_cast<size_t>(value) << 10;
}

constexpr auto operator""_MiBs(unsigned long long value) {
  return static_cast<size_t>(value) << 20;
}

constexpr auto operator""_GiBs(unsigned long long value) {
  return static_cast<size_t>(value) << 30;
}

constexpr auto operator""_TiBs(unsigned long long value) {
  return static_cast<size_t>(value) << 40;
}

} // namespace dwarfs::binary_literals

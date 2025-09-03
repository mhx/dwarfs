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

#include <dwarfs/checksum.h>

#include <dwarfs/internal/fs_section_checker.h>

namespace dwarfs::internal {

namespace {

template <typename T>
bool check_impl(T const& section, file_segment const& seg) {
  if (auto const cs_val = section.xxh3_64_value()) {
    if (auto const cs_span = section.checksum_span(seg)) {
      return checksum::verify(checksum::xxh3_64, cs_span->data(),
                              cs_span->size(), &*cs_val, sizeof(*cs_val));
    }
  }

  return true;
}

} // namespace

bool fs_section_checker::check(fs_section const& section) const {
  return check_impl(section, seg_);
}

bool fs_section_checker::check(fs_section::impl const& section) const {
  return check_impl(section, seg_);
}

bool fs_section_checker::verify(fs_section const& section) const {
  if (auto const cs_val = section.sha2_512_256_value()) {
    if (auto const cs_span = section.integrity_span(seg_)) {
      return checksum::verify(checksum::sha2_512_256, cs_span->data(),
                              cs_span->size(), cs_val->data(), cs_val->size());
    }
  }

  return true;
}

} // namespace dwarfs::internal

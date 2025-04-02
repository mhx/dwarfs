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

#include <fmt/format.h>

#include <folly/Conv.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/util.h>

namespace dwarfs::reader {

file_off_t parse_image_offset(std::string const& str) {
  if (str == "auto") {
    return filesystem_options::IMAGE_OFFSET_AUTO;
  }

  auto off = folly::tryTo<file_off_t>(str);

  if (!off) {
    auto ce = folly::makeConversionError(off.error(), str);
    DWARFS_THROW(runtime_error,
                 fmt::format("failed to parse image offset: {} ({})", str,
                             exception_str(ce)));
  }

  if (off.value() < 0) {
    DWARFS_THROW(runtime_error, "image offset must be positive");
  }

  return off.value();
}

} // namespace dwarfs::reader

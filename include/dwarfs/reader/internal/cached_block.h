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

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace dwarfs {

class byte_buffer_factory;
class file_segment;
class logger;

namespace internal {

class fs_section;

}

namespace reader::internal {

class cached_block {
 public:
  static std::unique_ptr<cached_block>
  create(logger& lgr, dwarfs::internal::fs_section const& b,
         file_segment const& seg, byte_buffer_factory const& bbf,
         bool disable_integrity_check);

  // TODO: have a create method for an uncompressed block,
  //       or (preferably) just handle this case internally

  virtual ~cached_block() = default;

  virtual size_t range_end() const = 0;
  virtual uint8_t const* data() const = 0;
  virtual void decompress_until(size_t end) = 0;
  virtual size_t uncompressed_size() const = 0;
  virtual void touch() = 0;
  virtual bool
  last_used_before(std::chrono::steady_clock::time_point tp) const = 0;
  virtual bool any_pages_swapped_out(std::vector<uint8_t>& tmp) const = 0;
};

} // namespace reader::internal

} // namespace dwarfs

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

#include <iosfwd>
#include <memory>
#include <span>

namespace dwarfs {

enum class pcm_sample_endianness { Big, Little };
enum class pcm_sample_signedness { Signed, Unsigned };
enum class pcm_sample_padding { Lsb, Msb };

std::ostream& operator<<(std::ostream& os, pcm_sample_endianness e);
std::ostream& operator<<(std::ostream& os, pcm_sample_signedness s);
std::ostream& operator<<(std::ostream& os, pcm_sample_padding p);

template <typename UnpackedType>
class pcm_sample_transformer {
 public:
  pcm_sample_transformer(pcm_sample_endianness end, pcm_sample_signedness sig,
                         pcm_sample_padding pad, int bytes, int bits);

  void unpack(std::span<UnpackedType> dst, std::span<uint8_t const> src) const {
    impl_->unpack(dst, src);
  }

  void pack(std::span<uint8_t> dst, std::span<UnpackedType const> src) const {
    impl_->pack(dst, src);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    unpack(std::span<UnpackedType> dst, std::span<uint8_t const> src) const = 0;
    virtual void
    pack(std::span<uint8_t> dst, std::span<UnpackedType const> src) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

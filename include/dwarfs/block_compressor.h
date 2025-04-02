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

#include <memory>
#include <stdexcept>
#include <string>

#include <dwarfs/byte_buffer.h>
#include <dwarfs/compression.h>
#include <dwarfs/compression_constraints.h>

namespace dwarfs {

class bad_compression_ratio_error : public std::runtime_error {
 public:
  bad_compression_ratio_error()
      : std::runtime_error{"bad compression ratio"} {}
};

class block_compressor {
 public:
  block_compressor() = default;

  explicit block_compressor(std::string const& spec);

  block_compressor(block_compressor const& bc)
      : impl_(bc.impl_->clone()) {}

  block_compressor(block_compressor&& bc) = default;
  block_compressor& operator=(block_compressor&& rhs) = default;

  shared_byte_buffer compress(shared_byte_buffer const& data) const {
    return impl_->compress(data, nullptr);
  }

  shared_byte_buffer
  compress(shared_byte_buffer const& data, std::string const& metadata) const {
    return impl_->compress(data, &metadata);
  }

  compression_type type() const { return impl_->type(); }

  std::string describe() const { return impl_->describe(); }

  std::string metadata_requirements() const {
    return impl_->metadata_requirements();
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const {
    return impl_->get_compression_constraints(metadata);
  }

  explicit operator bool() const { return static_cast<bool>(impl_); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::unique_ptr<impl> clone() const = 0;

    virtual shared_byte_buffer compress(shared_byte_buffer const& data,
                                        std::string const* metadata) const = 0;

    virtual compression_type type() const = 0;
    virtual std::string describe() const = 0;

    virtual std::string metadata_requirements() const = 0;

    virtual compression_constraints
    get_compression_constraints(std::string const& metadata) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

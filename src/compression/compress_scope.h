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

#include <dwarfs/block_compressor.h>
#include <dwarfs/memory_manager.h>

namespace dwarfs {

class compress_scope {
 public:
  enum class buffer_mode {
    RESERVE,
    RESIZE,
  };

  compress_scope(block_compressor::impl const* compressor,
                 memory_manager* memmgr, size_t data_size,
                 size_t compress_bound, buffer_mode mode = buffer_mode::RESIZE)
      : compressed_{malloc_byte_buffer::create()} {
    if (memmgr) {
      if (auto mem_usage = compressor->estimate_memory_usage(data_size);
          mem_usage > 0) {
        compressor_credit_ = memmgr->request(mem_usage, -1, "comp");
      }
      output_credit_ = memmgr->request(compress_bound, -1, "cblk");
    }
    if (mode == buffer_mode::RESIZE) {
      compressed_.resize(compress_bound);
    } else {
      compressed_.reserve(compress_bound);
    }
  }

  mutable_byte_buffer& buffer() { return compressed_; }

  uint8_t* data() { return compressed_.data(); }

  size_t size() const { return compressed_.size(); }

  void release() { compressor_credit_.release(); }

  void shrink(size_t size) {
    compressed_.resize(size);
    shrink_to_fit();
  }

  void shrink_to_fit() {
    compressed_.shrink_to_fit();

    if (output_credit_) {
      output_credit_.resize(compressed_.size());
    }
  }

  shared_byte_buffer share() {
    if (output_credit_) {
      compressed_.hold(std::move(output_credit_));
    }

    return compressed_.share();
  }

 private:
  mutable_byte_buffer compressed_;
  memory_manager::credit_handle compressor_credit_;
  memory_manager::credit_handle output_credit_;
};

} // namespace dwarfs

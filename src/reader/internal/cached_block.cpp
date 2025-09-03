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

#include <algorithm>
#include <atomic>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <dwarfs/block_decompressor.h>
#include <dwarfs/error.h>
#include <dwarfs/file_segment.h>
#include <dwarfs/logger.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/reader/internal/cached_block.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;

namespace {

template <typename LoggerPolicy>
class cached_block_ final : public cached_block {
 public:
  static inline std::atomic<size_t> instance_count_{0};

  cached_block_(logger& lgr, fs_section const& b, file_segment const& seg,
                byte_buffer_factory const& buffer_factory, bool release,
                bool disable_integrity_check)
      : decompressor_{std::make_unique<block_decompressor>(b.compression(),
                                                           b.data(seg))}
      , data_{decompressor_->start_decompression(buffer_factory)}
      , seg_{seg}
      , section_(b)
      , LOG_PROXY_INIT(lgr)
      , release_(release)
      , uncompressed_size_{decompressor_->uncompressed_size()} {
    if (!disable_integrity_check && !section_.check_fast(seg_)) {
      DWARFS_THROW(runtime_error, "block data integrity check failed");
    }
    std::atomic_fetch_add(&instance_count_, 1U);
    LOG_TRACE << "create cached block " << section_.section_number().value()
              << " [" << instance_count_
              << "], release=" << (release ? "true" : "false");
  }

  ~cached_block_() override {
    std::atomic_fetch_sub(&instance_count_, 1U);
    LOG_TRACE << "delete cached block " << section_.section_number().value()
              << " [" << instance_count_ << "]";
    if (decompressor_) {
      try_release();
    }
  }

  // once the block is fully decompressed, we can reset the decompressor_

  // This can be called from any thread
  size_t range_end() const override {
    return range_end_.load(std::memory_order_acquire);
  }

  // TODO: The code relies on the fact that the data_ buffer is never
  // reallocated once block decompression has started. I would like to
  // somehow enforce that this cannot happen.
  uint8_t const* data() const override { return data_.data(); }

  void decompress_until(size_t end) override {
    auto pos = data_.size();

    while (pos < end) {
      if (!decompressor_) {
        DWARFS_THROW(runtime_error, "no decompressor for block");
      }

      if (decompressor_->decompress_frame()) {
        // We're done, free the memory
        decompressor_.reset();

        // And release the memory from the mapping
        try_release();
      }

      pos = data_.size();
      range_end_.store(pos, std::memory_order_release);
    }
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  void touch() override { last_access_ = std::chrono::steady_clock::now(); }

  bool
  last_used_before(std::chrono::steady_clock::time_point tp) const override {
    return last_access_ < tp;
  }

  bool any_pages_swapped_out(std::vector<uint8_t>& tmp
                             [[maybe_unused]]) const override {
#if !(defined(_WIN32) || defined(__APPLE__))
    auto make_vec_arg = [](uint8_t* vec) {
#ifdef __FreeBSD__
      return reinterpret_cast<char*>(vec);
#else
      return vec;
#endif
    };

    // TODO: should be possible to do this on Windows and macOS as well
    auto page_size = ::sysconf(_SC_PAGESIZE);
    tmp.resize((data_.size() + page_size - 1) / page_size);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    if (::mincore(const_cast<uint8_t*>(data_.data()), data_.size(),
                  make_vec_arg(tmp.data())) == 0) {
      // i&1 == 1 means resident in memory
      return std::any_of(tmp.begin(), tmp.end(),
                         [](auto i) { return (i & 1) == 0; });
    }
#endif
    return false;
  }

 private:
  void try_release() {
    if (release_) {
      LOG_TRACE << "releasing mapped memory for block "
                << section_.section_number().value();
      std::error_code ec;
      seg_.advise(io_advice::dontneed, ec);
      if (ec) {
        LOG_INFO << "madvise() failed: " << ec.message();
      }
    }
  }

  std::atomic<size_t> range_end_{0};
  std::unique_ptr<block_decompressor> decompressor_;
  shared_byte_buffer data_;
  file_segment seg_;
  fs_section section_;
  LOG_PROXY_DECL(LoggerPolicy);
  bool const release_;
  size_t const uncompressed_size_;
  std::chrono::steady_clock::time_point last_access_;
};

} // namespace

std::unique_ptr<cached_block>
cached_block::create(logger& lgr, fs_section const& b, file_segment const& seg,
                     byte_buffer_factory const& bbf, bool release,
                     bool disable_integrity_check) {
  return make_unique_logging_object<cached_block, cached_block_,
                                    logger_policies>(lgr, b, seg, bbf, release,
                                                     disable_integrity_check);
}

} // namespace dwarfs::reader::internal

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <atomic>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "dwarfs/block_compressor.h"
#include "dwarfs/cached_block.h"
#include "dwarfs/error.h"
#include "dwarfs/fs_section.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"

namespace dwarfs {

template <typename LoggerPolicy>
class cached_block_ final : public cached_block {
 public:
  cached_block_(logger& lgr, fs_section const& b, std::shared_ptr<mmif> mm,
                bool release, bool disable_integrity_check)
      : decompressor_(std::make_unique<block_decompressor>(
            b.compression(), mm->as<uint8_t>(b.start()), b.length(), data_))
      , mm_(std::move(mm))
      , section_(b)
      , LOG_PROXY_INIT(lgr)
      , release_(release)
      , uncompressed_size_{decompressor_->uncompressed_size()} {
    if (!disable_integrity_check && !section_.check_fast(*mm_)) {
      DWARFS_THROW(runtime_error, "block data integrity check failed");
    }
  }

  ~cached_block_() override {
    if (decompressor_) {
      try_release();
    }
  }

  // once the block is fully decompressed, we can reset the decompressor_

  // This can be called from any thread
  size_t range_end() const override { return range_end_.load(); }

  const uint8_t* data() const override { return data_.data(); }

  void decompress_until(size_t end) override {
    while (data_.size() < end) {
      if (!decompressor_) {
        DWARFS_THROW(runtime_error, "no decompressor for block");
      }

      if (decompressor_->decompress_frame()) {
        // We're done, free the memory
        decompressor_.reset();

        // And release the memory from the mapping
        try_release();
      }

      range_end_ = data_.size();
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
#ifndef _WIN32
    auto page_size = ::sysconf(_SC_PAGESIZE);
    tmp.resize((data_.size() + page_size - 1) / page_size);
    if (::mincore(const_cast<uint8_t*>(data_.data()), data_.size(),
                  tmp.data()) == 0) {
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
      if (auto ec = mm_->release(section_.start(), section_.length())) {
        LOG_INFO << "madvise() failed: " << ec.message();
      }
    }
  }

  std::atomic<size_t> range_end_{0};
  std::vector<uint8_t> data_;
  std::unique_ptr<block_decompressor> decompressor_;
  std::shared_ptr<mmif> mm_;
  fs_section section_;
  LOG_PROXY_DECL(LoggerPolicy);
  bool const release_;
  size_t const uncompressed_size_;
  std::chrono::steady_clock::time_point last_access_;
};

std::unique_ptr<cached_block>
cached_block::create(logger& lgr, fs_section const& b, std::shared_ptr<mmif> mm,
                     bool release, bool disable_integrity_check) {
  return make_unique_logging_object<cached_block, cached_block_,
                                    logger_policies>(
      lgr, b, std::move(mm), release, disable_integrity_check);
}

} // namespace dwarfs

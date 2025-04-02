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

#include <future>
#include <memory>

#include <dwarfs/block_compressor.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/reader/block_range.h>

namespace dwarfs {

class logger;
class mmif;
class os_access;
class performance_monitor;

namespace internal {

class fs_section;

}

namespace reader {

struct block_cache_options;
struct cache_tidy_config;

namespace internal {

class block_cache {
 public:
  block_cache(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
              block_cache_options const& options,
              std::shared_ptr<performance_monitor const> const& perfmon);

  size_t block_count() const { return impl_->block_count(); }

  void insert(dwarfs::internal::fs_section const& section) {
    impl_->insert(section);
  }

  void set_block_size(size_t size) { impl_->set_block_size(size); }

  void set_num_workers(size_t num) { impl_->set_num_workers(num); }

  void set_tidy_config(cache_tidy_config const& cfg) {
    impl_->set_tidy_config(cfg);
  }

  std::future<block_range>
  get(size_t block_no, size_t offset, size_t size) const {
    return impl_->get(block_no, offset, size);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual size_t block_count() const = 0;
    virtual void insert(dwarfs::internal::fs_section const& section) = 0;
    virtual void set_block_size(size_t size) = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual std::future<block_range>
    get(size_t block_no, size_t offset, size_t length) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace internal
} // namespace reader
} // namespace dwarfs

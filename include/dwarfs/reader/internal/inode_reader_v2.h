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
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <dwarfs/reader/block_range.h>
#include <dwarfs/types.h>

#include <dwarfs/reader/internal/metadata_types.h>

namespace dwarfs {

class logger;
class os_access;
class performance_monitor;

namespace reader {

struct cache_tidy_config;
struct inode_reader_options;
struct iovec_read_buf;

namespace internal {

class block_cache;

class inode_reader_v2 {
 public:
  inode_reader_v2() = default;

  inode_reader_v2(logger& lgr, os_access const& os, block_cache&& bc,
                  inode_reader_options const& opts,
                  std::shared_ptr<performance_monitor const> const& perfmon);

  inode_reader_v2& operator=(inode_reader_v2&&) = default;

  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          chunk_range chunks, std::error_code& ec) const {
    return impl_->read_string(inode, size, offset, chunks, ec);
  }

  size_t read(char* buf, uint32_t inode, size_t size, file_off_t offset,
              chunk_range chunks, std::error_code& ec) const {
    return impl_->read(buf, inode, size, offset, chunks, ec);
  }

  size_t
  readv(iovec_read_buf& buf, uint32_t inode, size_t size, file_off_t offset,
        size_t maxiov, chunk_range chunks, std::error_code& ec) const {
    return impl_->readv(buf, inode, size, offset, maxiov, chunks, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        chunk_range chunks, std::error_code& ec) const {
    return impl_->readv(inode, size, offset, maxiov, chunks, ec);
  }

  void
  dump(std::ostream& os, std::string const& indent, chunk_range chunks) const {
    impl_->dump(os, indent, chunks);
  }

  void set_num_workers(size_t num) { impl_->set_num_workers(num); }

  void set_cache_tidy_config(cache_tidy_config const& cfg) {
    impl_->set_cache_tidy_config(cfg);
  }

  size_t num_blocks() const { return impl_->num_blocks(); }

  void cache_blocks(std::span<size_t const> blocks) const {
    impl_->cache_blocks(blocks);
  }

  std::future<block_range>
  read_raw_block_data(size_t block_no, size_t offset, size_t size) const {
    return impl_->read_raw_block_data(block_no, offset, size);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::string
    read_string(uint32_t inode, size_t size, file_off_t offset,
                chunk_range chunks, std::error_code& ec) const = 0;
    virtual size_t
    read(char* buf, uint32_t inode, size_t size, file_off_t offset,
         chunk_range chunks, std::error_code& ec) const = 0;
    virtual size_t
    readv(iovec_read_buf& buf, uint32_t inode, size_t size, file_off_t offset,
          size_t maxiov, chunk_range chunks, std::error_code& ec) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
          chunk_range chunks, std::error_code& ec) const = 0;
    virtual void dump(std::ostream& os, std::string const& indent,
                      chunk_range chunks) const = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_cache_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual size_t num_blocks() const = 0;
    virtual void cache_blocks(std::span<size_t const> blocks) const = 0;
    virtual std::future<block_range>
    read_raw_block_data(size_t block_no, size_t offset, size_t size) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace internal
} // namespace reader
} // namespace dwarfs

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

#pragma once

#include <memory>

#include <sys/uio.h>

#include <folly/small_vector.h>

#include "block_cache.h"
#include "fstypes.h"
#include "logger.h"

namespace dwarfs {

struct iovec_read_buf {
  // This covers more than 95% of reads
  static constexpr size_t inline_storage = 16;

  folly::small_vector<struct ::iovec, inline_storage> buf;
  folly::small_vector<block_range, inline_storage> ranges;
};

class inode_reader {
 public:
  inode_reader() = default;

  inode_reader(logger& lgr, block_cache&& bc, unsigned block_size_bits);

  inode_reader& operator=(inode_reader&&) = default;

  ssize_t read(char* buf, size_t size, off_t offset, const chunk_type* chunk,
               size_t chunk_count) const {
    return impl_->read(buf, size, offset, chunk, chunk_count);
  }

  ssize_t readv(iovec_read_buf& buf, size_t size, off_t offset,
                const chunk_type* chunk, size_t chunk_count) const {
    return impl_->readv(buf, size, offset, chunk, chunk_count);
  }

  void dump(std::ostream& os, const std::string& indent,
            const chunk_type* chunk, size_t chunk_count) const {
    impl_->dump(os, indent, chunk, chunk_count);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual ssize_t read(char* buf, size_t size, off_t offset,
                         const chunk_type* chunk, size_t chunk_count) const = 0;
    virtual ssize_t
    readv(iovec_read_buf& buf, size_t size, off_t offset,
          const chunk_type* chunk, size_t chunk_count) const = 0;
    virtual void dump(std::ostream& os, const std::string& indent,
                      const chunk_type* chunk, size_t chunk_count) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs

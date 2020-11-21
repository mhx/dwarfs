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

#include <cstring>
#include <mutex>

#include <folly/Utility.h>
#include <folly/stats/Histogram.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/config.h"
#include "dwarfs/inode_reader.h"

namespace dwarfs {

template <typename LoggerPolicy, unsigned BlockSizeBits>
class inode_reader_ : public inode_reader::impl {
 public:
  using access = chunk_access<BlockSizeBits>;

  inode_reader_(logger& lgr, block_cache&& bc)
      : cache_(std::move(bc))
      , log_(lgr)
      , iovec_sizes_(1, 0, 256) {}

  ~inode_reader_() {
    std::lock_guard<std::mutex> lock(iovec_sizes_mutex_);
    log_.info() << "iovec size p90: "
                << iovec_sizes_.getPercentileEstimate(0.9);
    log_.info() << "iovec size p95: "
                << iovec_sizes_.getPercentileEstimate(0.95);
    log_.info() << "iovec size p99: "
                << iovec_sizes_.getPercentileEstimate(0.99);
  }

  ssize_t read(char* buf, size_t size, off_t offset, const chunk_type* chunk,
               size_t chunk_count) const override;
  ssize_t readv(iovec_read_buf& buf, size_t size, off_t offset,
                const chunk_type* chunk, size_t chunk_count) const override;
  void dump(std::ostream& os, const std::string& indent,
            const chunk_type* chunk, size_t chunk_count) const override;

 private:
  template <typename StoreFunc>
  ssize_t read(size_t size, off_t offset, const chunk_type* chunk,
               size_t chunk_count, const StoreFunc& store) const;

  block_cache cache_;
  log_proxy<LoggerPolicy> log_;
  mutable folly::Histogram<size_t> iovec_sizes_;
  mutable std::mutex iovec_sizes_mutex_;
};

template <typename LoggerPolicy, unsigned BlockSizeBits>
void inode_reader_<LoggerPolicy, BlockSizeBits>::dump(
    std::ostream& os, const std::string& indent, const chunk_type* chunk,
    size_t chunk_count) const {
  for (size_t i = 0; i < chunk_count; ++i) {
    os << indent << "[" << i << "] block=" << access::block(chunk[i])
       << ", offset=" << access::offset(chunk[i])
       << ", size=" << access::size(chunk[i]) << "\n";
  }
}

template <typename LoggerPolicy, unsigned BlockSizeBits>
template <typename StoreFunc>
ssize_t
inode_reader_<LoggerPolicy, BlockSizeBits>::read(size_t size, off_t offset,
                                                 const chunk_type* chunk,
                                                 size_t chunk_count,
                                                 const StoreFunc& store) const {
  if (offset < 0) {
    return -EINVAL;
  }

  if (size == 0 || chunk_count == 0) {
    return 0;
  }

  const chunk_type* first = chunk;
  const chunk_type* last = first + chunk_count;
  size_t num_read = 0;

  // search for the first chunk that contains data from this request
  while (first < last) {
    size_t chunksize = access::size(*first);

    if (static_cast<size_t>(offset) < chunksize) {
      num_read = chunksize - offset;
      break;
    }

    offset -= chunksize;
    ++first;
  }

  if (first == last) {
    // offset beyond EOF; TODO: check if this should rather be -EINVAL
    return 0;
  }

  // request ranges from block cache
  std::vector<std::future<block_range>> ranges;

  for (chunk = first, num_read = 0; chunk < last and num_read < size; ++chunk) {
    size_t chunksize = access::size(*chunk) - offset;
    size_t chunkoff = access::offset(*chunk) + offset;

    if (num_read + chunksize > size) {
      chunksize = size - num_read;
    }

    ranges.emplace_back(cache_.get(access::block(*chunk), chunkoff, chunksize));

    num_read += chunksize;
    offset = 0;
  }

  // now fill the buffer
  num_read = 0;
  for (auto& r : ranges) {
    auto br = r.get();
    store(num_read, br);
    num_read += br.size();
  }

  return num_read;
}

template <typename LoggerPolicy, unsigned BlockSizeBits>
ssize_t
inode_reader_<LoggerPolicy, BlockSizeBits>::read(char* buf, size_t size,
                                                 off_t offset,
                                                 const chunk_type* chunk,
                                                 size_t chunk_count) const {
  return read(size, offset, chunk, chunk_count,
              [&](size_t num_read, const block_range& br) {
                ::memcpy(buf + num_read, br.data(), br.size());
              });
}

template <typename LoggerPolicy, unsigned BlockSizeBits>
ssize_t
inode_reader_<LoggerPolicy, BlockSizeBits>::readv(iovec_read_buf& buf,
                                                  size_t size, off_t offset,
                                                  const chunk_type* chunk,
                                                  size_t chunk_count) const {
  auto rv = read(size, offset, chunk, chunk_count,
                 [&](size_t, const block_range& br) {
                   buf.buf.resize(buf.buf.size() + 1);
                   buf.buf.back().iov_base = const_cast<uint8_t*>(br.data());
                   buf.buf.back().iov_len = br.size();
                   buf.ranges.emplace_back(br);
                 });
  {
    std::lock_guard<std::mutex> lock(iovec_sizes_mutex_);
    iovec_sizes_.addValue(buf.buf.size());
  }
  return rv;
}

namespace {

template <unsigned BlockSizeBits = MAX_BLOCK_BITS_SIZE>
struct inode_reader_factory {
  template <typename T>
  using inode_reader_type = inode_reader_<T, BlockSizeBits>;

  static std::unique_ptr<inode_reader::impl>
  create(logger& lgr, block_cache&& bc, unsigned block_size_bits) {
    if (block_size_bits == BlockSizeBits) {
      return make_unique_logging_object<inode_reader::impl, inode_reader_type,
                                        logger_policies>(lgr, std::move(bc));
    }

    return inode_reader_factory<BlockSizeBits - 1>::create(lgr, std::move(bc),
                                                           block_size_bits);
  }
};

template <>
struct inode_reader_factory<MIN_BLOCK_BITS_SIZE - 1> {
  static std::unique_ptr<inode_reader::impl>
  create(logger&, block_cache&&, unsigned) {
    throw std::runtime_error("unsupported block_size_bits");
  }
};
} // namespace

inode_reader::inode_reader(logger& lgr, block_cache&& bc,
                           unsigned block_size_bits)
    : impl_(inode_reader_factory<>::create(lgr, std::move(bc),
                                           block_size_bits)) {}
} // namespace dwarfs

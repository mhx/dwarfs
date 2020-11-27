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

#include <folly/stats/Histogram.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/config.h"
#include "dwarfs/inode_reader_v2.h"

namespace dwarfs {

namespace {

template <typename LoggerPolicy>
class inode_reader_ : public inode_reader_v2::impl {
 public:
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

#if 0
  ssize_t read(char* buf, size_t size, off_t offset, const chunk_type* chunk,
               size_t chunk_count) const override;
  ssize_t readv(iovec_read_buf& buf, size_t size, off_t offset,
                const chunk_type* chunk, size_t chunk_count) const override;
  void dump(std::ostream& os, const std::string& indent,
            const chunk_type* chunk, size_t chunk_count) const override;
#endif

 private:
#if 0
  template <typename StoreFunc>
  ssize_t read(size_t size, off_t offset, const chunk_type* chunk,
               size_t chunk_count, const StoreFunc& store) const;
#endif

  block_cache cache_;
  log_proxy<LoggerPolicy> log_;
  mutable folly::Histogram<size_t> iovec_sizes_;
  mutable std::mutex iovec_sizes_mutex_;
};

#if 0
template <typename LoggerPolicy>
void inode_reader_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, const chunk_type* chunk,
    size_t chunk_count) const {
  for (size_t i = 0; i < chunk_count; ++i) {
    os << indent << "[" << i << "] block=" << access::block(chunk[i])
       << ", offset=" << access::offset(chunk[i])
       << ", size=" << access::size(chunk[i]) << "\n";
  }
}

template <typename LoggerPolicy>
template <typename StoreFunc>
ssize_t
inode_reader_<LoggerPolicy>::read(size_t size, off_t offset,
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

template <typename LoggerPolicy>
ssize_t
inode_reader_<LoggerPolicy>::read(char* buf, size_t size,
                                                 off_t offset,
                                                 const chunk_type* chunk,
                                                 size_t chunk_count) const {
  return read(size, offset, chunk, chunk_count,
              [&](size_t num_read, const block_range& br) {
                ::memcpy(buf + num_read, br.data(), br.size());
              });
}

template <typename LoggerPolicy>
ssize_t
inode_reader_<LoggerPolicy>::readv(iovec_read_buf& buf,
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
#endif

} // namespace

inode_reader_v2::inode_reader_v2(logger& lgr, block_cache&& bc)
    : impl_(make_unique_logging_object<inode_reader_v2::impl, inode_reader_,
                                       logger_policies>(lgr, std::move(bc))) {}

} // namespace dwarfs

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

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include <folly/String.h>
#include <folly/container/Enumerate.h>
#include <folly/stats/Histogram.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/inode_reader_v2.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace {

template <typename LoggerPolicy>
class inode_reader_ final : public inode_reader_v2::impl {
 public:
  inode_reader_(logger& lgr, block_cache&& bc)
      : cache_(std::move(bc))
      , LOG_PROXY_INIT(lgr)
      , iovec_sizes_(1, 0, 256) {}

  ~inode_reader_() override {
    std::lock_guard lock(iovec_sizes_mutex_);
    if (iovec_sizes_.computeTotalCount() > 0) {
      LOG_INFO << "iovec size p90: " << iovec_sizes_.getPercentileEstimate(0.9);
      LOG_INFO << "iovec size p95: "
               << iovec_sizes_.getPercentileEstimate(0.95);
      LOG_INFO << "iovec size p99: "
               << iovec_sizes_.getPercentileEstimate(0.99);
    }
  }

  ssize_t
  read(char* buf, size_t size, off_t offset, chunk_range chunks) const override;
  ssize_t readv(iovec_read_buf& buf, size_t size, off_t offset,
                chunk_range chunks) const override;
  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(size_t size, off_t offset, chunk_range chunks) const override;
  void dump(std::ostream& os, const std::string& indent,
            chunk_range chunks) const override;
  void set_num_workers(size_t num) override { cache_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    cache_.set_tidy_config(cfg);
  }

 private:
  folly::Expected<std::vector<std::future<block_range>>, int>
  read(size_t size, off_t offset, chunk_range chunks) const;

  template <typename StoreFunc>
  ssize_t read(size_t size, off_t offset, chunk_range chunks,
               const StoreFunc& store) const;

  block_cache cache_;
  LOG_PROXY_DECL(LoggerPolicy);
  mutable folly::Histogram<size_t> iovec_sizes_;
  mutable std::mutex iovec_sizes_mutex_;
};

template <typename LoggerPolicy>
void inode_reader_<LoggerPolicy>::dump(std::ostream& os,
                                       const std::string& indent,
                                       chunk_range chunks) const {
  for (auto chunk : folly::enumerate(chunks)) {
    os << indent << "  [" << chunk.index << "] -> (block=" << chunk->block()
       << ", offset=" << chunk->offset() << ", size=" << chunk->size() << ")\n";
  }
}

template <typename LoggerPolicy>
folly::Expected<std::vector<std::future<block_range>>, int>
inode_reader_<LoggerPolicy>::readv(size_t size, off_t offset,
                                   chunk_range chunks) const {
  if (offset < 0) {
    return folly::makeUnexpected(-EINVAL);
  }

  // request ranges from block cache
  std::vector<std::future<block_range>> ranges;

  if (size == 0 || chunks.empty()) {
    return ranges;
  }

  auto it = chunks.begin();
  auto end = chunks.end();

  // search for the first chunk that contains data from this request
  while (it < end) {
    size_t chunksize = it->size();

    if (static_cast<size_t>(offset) < chunksize) {
      break;
    }

    offset -= chunksize;
    ++it;
  }

  if (it == end) {
    // offset beyond EOF; TODO: check if this should rather be -EINVAL
    return ranges;
  }

  for (size_t num_read = 0; it != end && num_read < size; ++it) {
    size_t chunksize = it->size() - offset;
    size_t chunkoff = it->offset() + offset;

    if (chunksize == 0) {
      LOG_ERROR << "invalid zero-sized chunk";
      return folly::makeUnexpected(-EIO);
    }

    if (num_read + chunksize > size) {
      chunksize = size - num_read;
    }

    ranges.emplace_back(cache_.get(it->block(), chunkoff, chunksize));

    num_read += chunksize;
    offset = 0;
  }

  return ranges;
}

template <typename LoggerPolicy>
template <typename StoreFunc>
ssize_t
inode_reader_<LoggerPolicy>::read(size_t size, off_t offset, chunk_range chunks,
                                  const StoreFunc& store) const {
  auto ranges = readv(size, offset, chunks);

  if (!ranges) {
    return ranges.error();
  }

  try {
    // now fill the buffer
    size_t num_read = 0;
    for (auto& r : ranges.value()) {
      auto br = r.get();
      store(num_read, br);
      num_read += br.size();
    }
    return num_read;
  } catch (runtime_error const& e) {
    LOG_ERROR << e.what();
  } catch (...) {
    LOG_ERROR << folly::exceptionStr(std::current_exception());
  }

  return -EIO;
}

template <typename LoggerPolicy>
ssize_t inode_reader_<LoggerPolicy>::read(char* buf, size_t size, off_t offset,
                                          chunk_range chunks) const {
  return read(size, offset, chunks,
              [&](size_t num_read, const block_range& br) {
                ::memcpy(buf + num_read, br.data(), br.size());
              });
}

template <typename LoggerPolicy>
ssize_t
inode_reader_<LoggerPolicy>::readv(iovec_read_buf& buf, size_t size,
                                   off_t offset, chunk_range chunks) const {
  auto rv = read(size, offset, chunks, [&](size_t, const block_range& br) {
    buf.buf.resize(buf.buf.size() + 1);
    buf.buf.back().iov_base = const_cast<uint8_t*>(br.data());
    buf.buf.back().iov_len = br.size();
    buf.ranges.emplace_back(br);
  });
  {
    std::lock_guard lock(iovec_sizes_mutex_);
    iovec_sizes_.addValue(buf.buf.size());
  }
  return rv;
}

} // namespace

inode_reader_v2::inode_reader_v2(logger& lgr, block_cache&& bc)
    : impl_(make_unique_logging_object<inode_reader_v2::impl, inode_reader_,
                                       logger_policies>(lgr, std::move(bc))) {}

} // namespace dwarfs

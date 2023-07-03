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
#include <folly/container/EvictingCacheMap.h>
#include <folly/stats/Histogram.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/inode_reader_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/performance_monitor.h"

namespace dwarfs {

namespace {

/**
 * Offset cache configuration
 *
 * The offset cache is a tiny cache that keeps track of the offset
 * and index of the last chunk that has been read from a file.
 *
 * Due to the way file metadata is organized, accessing a random
 * location inside a file requires iteration over all chunks until
 * the correct offset is found. When sequentially reading a file in
 * multiple requests, this becomes an O(n**2) operation.
 *
 * For files with a small enough number of chunks, performing the
 * linear scan isn't really a problem. For very fragmented files,
 * it can definitely be an issue.
 *
 * So this cache tries to improve the read latency for files with
 * more than `offset_cache_min_chunks` chunks. For a typical file
 * system, that's only about 1% of the files. While iterating the
 * chunks, we keep track of the chunk index and the offset of the
 * current chunk. Once we've finished reading, we store both the
 * index and the offset for the inode in the cache. When the next
 * (likely sequential) read request for the inode arrives, the
 * offset from that request will very likely be larger than the
 * offset found in the cache and so we can just move the iterator
 * ahead without performing a linear scan.
 */
constexpr size_t const offset_cache_min_chunks = 64;
constexpr size_t const offset_cache_size = 64;

struct offset_cache_entry {
  offset_cache_entry(file_off_t off, size_t ix)
      : file_offset{off}
      , chunk_index{ix} {}

  file_off_t file_offset;
  size_t chunk_index;
};

template <typename LoggerPolicy>
class inode_reader_ final : public inode_reader_v2::impl {
 public:
  inode_reader_(logger& lgr, block_cache&& bc,
                std::shared_ptr<performance_monitor const> perfmon
                [[maybe_unused]])
      : cache_(std::move(bc))
      , LOG_PROXY_INIT(lgr)
      // clang-format off
      PERFMON_CLS_PROXY_INIT(perfmon, "inode_reader_v2")
      PERFMON_CLS_TIMER_INIT(read)
      PERFMON_CLS_TIMER_INIT(readv_iovec)
      PERFMON_CLS_TIMER_INIT(readv_future) // clang-format on
      , offset_cache_{offset_cache_size}
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
    if (oc_put_.load() > 0) {
      LOG_INFO << "offset cache put: " << oc_put_.load();
      LOG_INFO << "offset cache get: " << oc_get_.load();
      LOG_INFO << "offset cache hit: " << oc_hit_.load();
    }
  }

  ssize_t read(char* buf, uint32_t inode, size_t size, file_off_t offset,
               chunk_range chunks) const override;
  ssize_t readv(iovec_read_buf& buf, uint32_t inode, size_t size,
                file_off_t offset, chunk_range chunks) const override;
  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(uint32_t inode, size_t size, file_off_t offset,
        chunk_range chunks) const override;
  void dump(std::ostream& os, const std::string& indent,
            chunk_range chunks) const override;
  void set_num_workers(size_t num) override { cache_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    cache_.set_tidy_config(cfg);
  }
  size_t num_blocks() const override { return cache_.block_count(); }

 private:
  folly::Expected<std::vector<std::future<block_range>>, int>
  read_internal(uint32_t inode, size_t size, file_off_t offset,
                chunk_range chunks) const;

  template <typename StoreFunc>
  ssize_t read_internal(uint32_t inode, size_t size, file_off_t offset,
                        chunk_range chunks, const StoreFunc& store) const;

  block_cache cache_;
  LOG_PROXY_DECL(LoggerPolicy);
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(read)
  PERFMON_CLS_TIMER_DECL(readv_iovec)
  PERFMON_CLS_TIMER_DECL(readv_future)
  mutable folly::EvictingCacheMap<uint32_t, offset_cache_entry> offset_cache_;
  mutable std::mutex offset_cache_mutex_;
  mutable folly::Histogram<size_t> iovec_sizes_;
  mutable std::mutex iovec_sizes_mutex_;
  mutable std::atomic<size_t> oc_put_{0};
  mutable std::atomic<size_t> oc_get_{0};
  mutable std::atomic<size_t> oc_hit_{0};
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
inode_reader_<LoggerPolicy>::read_internal(uint32_t inode, size_t const size,
                                           file_off_t offset,
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
  file_off_t it_offset = 0;

  // Check if we can find this inode in the offset cache
  if (offset > 0 && chunks.size() >= offset_cache_min_chunks) {
    ++oc_get_;

    std::optional<offset_cache_entry> oce;

    {
      std::lock_guard lock(offset_cache_mutex_);

      if (auto oci = offset_cache_.find(inode); oci != offset_cache_.end()) {
        oce = oci->second;
      }
    }

    if (oce && oce->file_offset <= offset) {
      std::advance(it, oce->chunk_index);
      offset -= oce->file_offset;
      it_offset = oce->file_offset;
      ++oc_hit_;
    }
  }

  // search for the first chunk that contains data from this request
  while (it < end) {
    size_t chunksize = it->size();

    if (static_cast<size_t>(offset) < chunksize) {
      break;
    }

    offset -= chunksize;
    it_offset += chunksize;
    ++it;
  }

  if (it == end) {
    // offset beyond EOF; TODO: check if this should rather be -EINVAL
    return ranges;
  }

  size_t num_read = 0;

  while (it != end) {
    size_t const chunksize = it->size();
    size_t const copyoff = it->offset() + offset;
    size_t copysize = chunksize - offset;

    if (copysize == 0) {
      LOG_ERROR << "invalid zero-sized chunk";
      return folly::makeUnexpected(-EIO);
    }

    if (num_read + copysize > size) {
      copysize = size - num_read;
    }

    ranges.emplace_back(cache_.get(it->block(), copyoff, copysize));

    num_read += copysize;

    if (num_read == size) {
      // Store in offset cache if we have enough chunks
      if (chunks.size() >= offset_cache_min_chunks) {
        offset_cache_entry oce(it_offset, std::distance(chunks.begin(), it));
        ++oc_put_;

        {
          std::lock_guard lock(offset_cache_mutex_);
          offset_cache_.set(inode, oce);
        }
      }

      break;
    }

    offset = 0;
    it_offset += chunksize;
    ++it;
  }

  return ranges;
}

template <typename LoggerPolicy>
template <typename StoreFunc>
ssize_t
inode_reader_<LoggerPolicy>::read_internal(uint32_t inode, size_t size,
                                           file_off_t offset,
                                           chunk_range chunks,
                                           const StoreFunc& store) const {
  auto ranges = read_internal(inode, size, offset, chunks);

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
folly::Expected<std::vector<std::future<block_range>>, int>
inode_reader_<LoggerPolicy>::readv(uint32_t inode, size_t const size,
                                   file_off_t offset,
                                   chunk_range chunks) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)

  return read_internal(inode, size, offset, chunks);
}

template <typename LoggerPolicy>
ssize_t
inode_reader_<LoggerPolicy>::read(char* buf, uint32_t inode, size_t size,
                                  file_off_t offset, chunk_range chunks) const {
  PERFMON_CLS_SCOPED_SECTION(read)

  return read_internal(inode, size, offset, chunks,
                       [&](size_t num_read, const block_range& br) {
                         ::memcpy(buf + num_read, br.data(), br.size());
                       });
}

template <typename LoggerPolicy>
ssize_t inode_reader_<LoggerPolicy>::readv(iovec_read_buf& buf, uint32_t inode,
                                           size_t size, file_off_t offset,
                                           chunk_range chunks) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)

  auto rv = read_internal(
      inode, size, offset, chunks, [&](size_t, const block_range& br) {
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

inode_reader_v2::inode_reader_v2(
    logger& lgr, block_cache&& bc,
    std::shared_ptr<performance_monitor const> perfmon)
    : impl_(make_unique_logging_object<inode_reader_v2::impl, inode_reader_,
                                       logger_policies>(lgr, std::move(bc),
                                                        std::move(perfmon))) {}

} // namespace dwarfs

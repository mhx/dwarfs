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
#include "dwarfs/iovec_read_buf.h"
#include "dwarfs/logger.h"
#include "dwarfs/offset_cache.h"
#include "dwarfs/performance_monitor.h"

namespace dwarfs {

namespace {

/**
 * Offset cache configuration
 *
 * The offset cache is a small cache that improves both random
 * and sequential read speed in large, fragmented files.
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
 * The offset cache saves absolute file offsets every
 * `offset_cache_chunk_index_interval` chunks, so it'll only be
 * used for files with at least that many chunks in the first
 * place. The saved offsets can be used to find a nearby chunk
 * using binary search instead of a linear scan. From that chunk,
 * the requested offset can be found using a linear scan.
 *
 * For the most common use case, sequential reads, the cache entry
 * includes the last chunk index along with its absolute offset,
 * so both the binary search and the linear scan can be completely
 * avoided when a subsequent read request starts at the end of the
 * previous read request.
 *
 * The `offset_cache_updater_max_inline_offsets` constant defines
 * how many (offset, index) pairs can be stored "inline" (i.e.
 * without requiring any memory allocations) by the cache updater
 * while performing the read request. 4 is plenty.
 *
 * Last but not least, `offset_cache_size` defines the number of
 * inodes that can live in the cache simultaneously. The number
 * of cached offsets for each inode is not limited.
 */
constexpr size_t const offset_cache_chunk_index_interval = 256;
constexpr size_t const offset_cache_updater_max_inline_offsets = 4;
constexpr size_t const offset_cache_size = 64;

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
  using offset_cache_type =
      basic_offset_cache<uint32_t, file_off_t, size_t,
                         offset_cache_chunk_index_interval,
                         offset_cache_updater_max_inline_offsets>;

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
  mutable offset_cache_type offset_cache_;
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

  size_t it_index = 0;
  file_off_t it_offset = 0;

  offset_cache_type::value_type oc_ent;
  offset_cache_type::updater oc_upd;

  // Check if we can find this inode in the offset cache
  if (offset > 0 && chunks.size() >= offset_cache_type::chunk_index_interval) {
    oc_ent = offset_cache_.find(inode, chunks.size());

    std::tie(it_index, it_offset) = oc_ent->find(offset, oc_upd);

    std::advance(it, it_index);
    offset -= it_offset;
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

    oc_upd.add_offset(++it_index, it_offset);
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
      if (oc_ent) {
        oc_ent->update(oc_upd, it_index, it_offset, chunksize);
        offset_cache_.set(inode, std::move(oc_ent));
      }

      break;
    }

    offset = 0;
    it_offset += chunksize;
    ++it;

    oc_upd.add_offset(++it_index, it_offset);
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

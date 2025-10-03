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

#include <cassert>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include <folly/stats/Histogram.h>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/fstypes.h>
#include <dwarfs/logger.h>
#include <dwarfs/memory_mapping.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/inode_reader_options.h>
#include <dwarfs/reader/iovec_read_buf.h>
#include <dwarfs/util.h>

#include <dwarfs/reader/internal/block_cache.h>
#include <dwarfs/reader/internal/inode_reader_v2.h>
#include <dwarfs/reader/internal/lru_cache.h>
#include <dwarfs/reader/internal/offset_cache.h>

namespace dwarfs::reader::internal {

namespace {

constexpr size_t const kReadAllIOV{std::numeric_limits<size_t>::max()};

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
constexpr size_t const readahead_cache_size = 64;

template <class T>
std::future<T> make_ready_future(T value) {
  std::promise<T> p;
  auto f = p.get_future();
  p.set_value(std::move(value));
  return f;
}

} // namespace

template <typename LoggerPolicy>
class inode_reader_ final : public inode_reader_v2::impl {
 public:
  inode_reader_(logger& lgr, os_access const& os, block_cache&& bc,
                inode_reader_options const& opts,
                std::shared_ptr<performance_monitor const> const& perfmon
                [[maybe_unused]])
      : cache_(std::move(bc))
      , opts_{opts}
      , os_{os}
      , LOG_PROXY_INIT(lgr)
      // clang-format off
      PERFMON_CLS_PROXY_INIT(perfmon, "inode_reader_v2")
      PERFMON_CLS_TIMER_INIT(read, "offset", "size")
      PERFMON_CLS_TIMER_INIT(read_string, "offset", "size")
      PERFMON_CLS_TIMER_INIT(readv_iovec, "offset", "size")
      PERFMON_CLS_TIMER_INIT(readv_future, "offset", "size") // clang-format on
      , offset_cache_{offset_cache_size}
      , readahead_cache_{readahead_cache_size}
      , iovec_sizes_(1, 0, 256) {}

  ~inode_reader_() override {
    std::lock_guard lock(iovec_sizes_mutex_);
    if (iovec_sizes_.computeTotalCount() > 0) {
      LOG_VERBOSE << "iovec size p90: "
                  << iovec_sizes_.getPercentileEstimate(0.9);
      LOG_VERBOSE << "iovec size p95: "
                  << iovec_sizes_.getPercentileEstimate(0.95);
      LOG_VERBOSE << "iovec size p99: "
                  << iovec_sizes_.getPercentileEstimate(0.99);
    }
  }

  std::string
  read_string(uint32_t inode, size_t size, file_off_t offset,
              chunk_range chunks, std::error_code& ec) const override;
  size_t read(char* buf, uint32_t inode, size_t size, file_off_t offset,
              chunk_range chunks, std::error_code& ec) const override;
  size_t
  readv(iovec_read_buf& buf, uint32_t inode, size_t size, file_off_t offset,
        size_t maxiov, chunk_range chunks, std::error_code& ec) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        chunk_range chunks, std::error_code& ec) const override;
  void dump(std::ostream& os, std::string const& indent,
            chunk_range chunks) const override;
  void set_num_workers(size_t num) override { cache_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    cache_.set_tidy_config(cfg);
  }
  size_t num_blocks() const override { return cache_.block_count(); }

  void cache_blocks(std::span<size_t const> blocks) const override {
    for (auto b : blocks) {
      cache_.get(b, 0, 1);
    }
  }

  std::future<block_range> read_raw_block_data(size_t block_no, size_t offset,
                                               size_t size) const override;

 private:
  using offset_cache_type =
      basic_offset_cache<uint32_t, file_off_t, size_t,
                         offset_cache_chunk_index_interval,
                         offset_cache_updater_max_inline_offsets>;

  using readahead_cache_type = lru_cache<uint32_t, file_off_t>;

  std::vector<std::future<block_range>>
  read_internal(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
                chunk_range chunks, std::error_code& ec) const;

  template <typename StoreFunc>
  size_t read_internal(uint32_t inode, size_t size, file_off_t read_offset,
                       size_t maxiov, chunk_range chunks, std::error_code& ec,
                       StoreFunc const& store) const;

  void do_readahead(uint32_t inode, chunk_range::iterator it,
                    chunk_range::iterator const& end, file_off_t read_offset,
                    size_t size, file_off_t it_offset) const;

  readonly_memory_mapping const& get_hole_data() const {
    std::call_once(hole_data_init_flag_, [this]() {
      hole_data_ = os_.map_empty_readonly(opts_.hole_data_size);
      if (!hole_data_) {
        LOG_ERROR << "failed to allocate zero block: "
                  << exception_str(std::current_exception());
      }
    });
    return hole_data_;
  }

  block_cache cache_;
  inode_reader_options const opts_;
  os_access const& os_;
  LOG_PROXY_DECL(LoggerPolicy);
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(read)
  PERFMON_CLS_TIMER_DECL(read_string)
  PERFMON_CLS_TIMER_DECL(readv_iovec)
  PERFMON_CLS_TIMER_DECL(readv_future)
  mutable offset_cache_type offset_cache_;
  mutable std::mutex readahead_cache_mutex_;
  mutable readahead_cache_type readahead_cache_;
  mutable std::mutex iovec_sizes_mutex_;
  mutable folly::Histogram<size_t> iovec_sizes_;
  mutable std::once_flag hole_data_init_flag_;
  mutable readonly_memory_mapping hole_data_;
};

template <typename LoggerPolicy>
void inode_reader_<LoggerPolicy>::dump(std::ostream& os,
                                       std::string const& indent,
                                       chunk_range chunks) const {
  for (auto const& [index, chunk] : ranges::views::enumerate(chunks)) {
    if (chunk.is_data()) {
      os << indent << "  [" << index << "] -> DATA (block=" << chunk.block()
         << ", offset=" << chunk.offset() << ", size=" << chunk.size() << ")\n";
    } else {
      os << indent << "  [" << index << "] -> HOLE (size=" << chunk.size()
         << ")\n";
    }
  }
}

template <typename LoggerPolicy>
void inode_reader_<LoggerPolicy>::do_readahead(uint32_t inode,
                                               chunk_range::iterator it,
                                               chunk_range::iterator const& end,
                                               file_off_t const read_offset,
                                               size_t const size,
                                               file_off_t it_offset) const {
  LOG_TRACE << "readahead (" << inode << "): " << read_offset << "/" << size
            << "/" << it_offset;

  file_off_t readahead_pos{0};
  file_off_t const current_offset = read_offset + size;
  file_off_t const readahead_until = current_offset + opts_.readahead;

  {
    std::lock_guard lock(readahead_cache_mutex_);

    if (read_offset > 0) {
      if (auto i = readahead_cache_.find(inode); i != readahead_cache_.end()) {
        readahead_pos = i->second;
      }

      if (readahead_until <= readahead_pos) {
        return;
      }
    }

    readahead_cache_.set(inode, readahead_until);
  }

  while (it != end) {
    if (it_offset + it->size() >= readahead_pos) {
      cache_.get(it->block(), it->offset(), it->size());
    }

    it_offset += it->size();

    if (it_offset >= readahead_until) {
      break;
    }

    ++it;
  }
}

template <typename LoggerPolicy>
std::future<block_range>
inode_reader_<LoggerPolicy>::read_raw_block_data(size_t block_no, size_t offset,
                                                 size_t size) const {
  return cache_.get(block_no, offset, size);
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
inode_reader_<LoggerPolicy>::read_internal(uint32_t inode, size_t const size,
                                           file_off_t const read_offset,
                                           size_t const maxiov,
                                           chunk_range chunks,
                                           std::error_code& ec) const {
  std::vector<std::future<block_range>> ranges;

  auto offset = read_offset;

  if (offset < 0) {
    // This is exactly how lseek(2) behaves when seeking before the start of
    // the file.
    ec = std::make_error_code(std::errc::invalid_argument);
    return ranges;
  }

  // request ranges from block cache

  if (size == 0 || chunks.empty()) {
    ec.clear();
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

    if (std::cmp_less(offset, chunksize)) {
      break;
    }

    offset -= chunksize;
    it_offset += chunksize;
    ++it;

    oc_upd.add_offset(++it_index, it_offset);
  }

  if (it == end) {
    // Offset behind end of file. This is exactly how lseek(2) and read(2)
    // behave when seeking behind the end of the file and reading past EOF.
    ec.clear();
    return ranges;
  }

  size_t num_read = 0;

  while (it != end) {
    auto const chunksize = it->size();
    auto copysize = chunksize - offset;

    DWARFS_CHECK(copysize > 0, "unexpected zero-sized chunk");

    if (std::cmp_greater(num_read + copysize, size)) {
      copysize = size - num_read;
    }

    if (it->is_data()) {
      file_off_t const copyoff = it->offset() + offset;

      assert(std::cmp_less_equal(copyoff, std::numeric_limits<size_t>::max()));
      assert(std::cmp_less_equal(copysize, std::numeric_limits<size_t>::max()));

      ranges.emplace_back(cache_.get(it->block(), copyoff, copysize));

      num_read += copysize;
    } else {
      auto const& hole_span = get_hole_data().template const_span<uint8_t>();

      while (copysize > 0) {
        auto const hole_range_size =
            std::min<size_t>(copysize, hole_span.size());

        ranges.emplace_back(make_ready_future(
            block_range(hole_span.subspan(0, hole_range_size))));

        copysize -= hole_range_size;
        num_read += hole_range_size;
      }
    }

    if (num_read == size || ranges.size() >= maxiov) {
      if (oc_ent) {
        oc_ent->update(oc_upd, it_index, it_offset, chunksize);
        offset_cache_.set(inode, std::move(oc_ent));
      }

      if (opts_.readahead > 0) {
        do_readahead(inode, it, end, read_offset, size, it_offset);
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
size_t inode_reader_<LoggerPolicy>::read_internal(
    uint32_t inode, size_t size, file_off_t offset, size_t const maxiov,
    chunk_range chunks, std::error_code& ec, StoreFunc const& store) const {
  auto ranges = read_internal(inode, size, offset, maxiov, chunks, ec);

  if (ec) {
    return 0;
  }

  try {
    // now fill the buffer
    size_t num_read = 0;
    for (auto& r : ranges) {
      auto br = r.get();
      store(num_read, br);
      num_read += br.size();
    }
    return num_read;
  } catch (...) {
    LOG_ERROR << exception_str(std::current_exception());
  }

  ec = std::make_error_code(std::errc::io_error);

  return 0;
}

template <typename LoggerPolicy>
std::string
inode_reader_<LoggerPolicy>::read_string(uint32_t inode, size_t size,
                                         file_off_t offset, chunk_range chunks,
                                         std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read_string)
  PERFMON_SET_CONTEXT(static_cast<uint64_t>(offset), size);

  auto ranges = read_internal(inode, size, offset, kReadAllIOV, chunks, ec);

  std::string res;

  if (!ec) {
    try {
      std::vector<block_range> brs(ranges.size());
      size_t total_size{0};
      for (auto& r : ranges) {
        auto br = r.get();
        total_size += br.size();
        brs.emplace_back(std::move(br));
      }
      res.reserve(total_size);
      for (auto const& br : brs) {
        res.append(reinterpret_cast<char const*>(br.data()), br.size());
      }
    } catch (...) {
      LOG_ERROR << exception_str(std::current_exception());
      ec = std::make_error_code(std::errc::io_error);
    }
  }

  return res;
}

template <typename LoggerPolicy>
size_t inode_reader_<LoggerPolicy>::read(char* buf, uint32_t inode, size_t size,
                                         file_off_t offset, chunk_range chunks,
                                         std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read)
  PERFMON_SET_CONTEXT(static_cast<uint64_t>(offset), size);

  return read_internal(inode, size, offset, kReadAllIOV, chunks, ec,
                       [&](size_t num_read, block_range const& br) {
                         ::memcpy(buf + num_read, br.data(), br.size());
                       });
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
inode_reader_<LoggerPolicy>::readv(uint32_t inode, size_t const size,
                                   file_off_t offset, size_t maxiov,
                                   chunk_range chunks,
                                   std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)
  PERFMON_SET_CONTEXT(static_cast<uint64_t>(offset), size);

  return read_internal(inode, size, offset, maxiov, chunks, ec);
}

template <typename LoggerPolicy>
size_t inode_reader_<LoggerPolicy>::readv(iovec_read_buf& buf, uint32_t inode,
                                          size_t size, file_off_t offset,
                                          size_t maxiov, chunk_range chunks,
                                          std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)
  PERFMON_SET_CONTEXT(static_cast<uint64_t>(offset), size);

  auto rv = read_internal(inode, size, offset, maxiov, chunks, ec,
                          [&](size_t, block_range const& br) {
                            auto& iov = buf.buf.emplace_back();
                            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                            iov.iov_base = const_cast<uint8_t*>(br.data());
                            iov.iov_len = br.size();
                            buf.ranges.emplace_back(br);
                          });

  {
    std::lock_guard lock(iovec_sizes_mutex_);
    iovec_sizes_.addValue(buf.buf.size());
  }

  return rv;
}

inode_reader_v2::inode_reader_v2(
    logger& lgr, os_access const& os, block_cache&& bc,
    inode_reader_options const& opts,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : impl_(make_unique_logging_object<inode_reader_v2::impl, inode_reader_,
                                       logger_policies>(lgr, os, std::move(bc),
                                                        opts, perfmon)) {}

} // namespace dwarfs::reader::internal

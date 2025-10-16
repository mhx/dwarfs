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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>
#include <version>

#include <fmt/format.h>

#include <dwarfs/block_decompressor.h>
#include <dwarfs/error.h>
#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/mapped_byte_buffer.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/internal/fs_section_checker.h>
#include <dwarfs/internal/worker_group.h>
#include <dwarfs/reader/internal/block_cache.h>
#include <dwarfs/reader/internal/filesystem_parser.h>
#include <dwarfs/reader/internal/inode_reader_v2.h>
#include <dwarfs/reader/internal/metadata_v2.h>

namespace dwarfs::reader {

namespace internal {

using namespace dwarfs::internal;

namespace {

constexpr size_t const kDefaultMaxIOV{std::numeric_limits<size_t>::max()};
constexpr size_t const KReadFullFile{std::numeric_limits<size_t>::max()};

void check_section_logger(logger& lgr, fs_section const& section) {
  LOG_PROXY(debug_logger_policy, lgr);

  LOG_DEBUG << "section " << section.description() << " @ " << section.start()
            << " [" << section.length() << " bytes]";

  if (!section.is_known_type()) {
    LOG_WARN << "unknown section type " << folly::to_underlying(section.type())
             << " in section @ " << section.start();
  }

  if (!section.is_known_compression()) {
    LOG_WARN << "unknown compression type "
             << folly::to_underlying(section.compression()) << " in section @ "
             << section.start();
  }
}

template <typename Fn>
auto call_ec_throw(Fn&& fn) {
  std::error_code ec;
  auto result = std::forward<Fn>(fn)(ec);
  if (ec) {
    throw std::system_error(ec);
  }
  return result;
}

using section_map = std::unordered_map<section_type, std::vector<fs_section>>;

class section_wrapper {
 public:
  class section_data {
   public:
    struct secseg {
      secseg(fs_section const& s, file_segment const& seg)
          : section{s}
          , segment{seg} {}

      fs_section section;
      file_segment segment;
    };

    section_data(fs_section const& sec, file_segment const& seg)
        : data_{secseg{sec, seg}} {}

    explicit section_data(shared_byte_buffer const& buf)
        : data_{buf} {}

    std::span<uint8_t const> span() const {
      return data_ |
             match{
                 [](secseg const& ss) { return ss.section.data(ss.segment); },
                 [](shared_byte_buffer const& buf) { return buf.span(); },
             };
    }

    void lock(std::error_code& ec) {
      data_ | match{
                  [&](secseg const& ss) { ss.segment.lock(ec); },
                  [](shared_byte_buffer const&) {},
              };
    }

   private:
    std::variant<secseg, shared_byte_buffer> data_;
  };

  section_wrapper(file_view const& fv, fs_section const& sec)
      : fv_{fv}
      , sec_{sec} {}

  std::optional<block_decompressor> try_get_block_decompressor() {
    try {
      return get_block_decompressor();
    } catch (std::exception const&) { // NOLINT(bugprone-empty-catch)
    }

    return std::nullopt;
  }

  size_t get_uncompressed_size() {
    if (sec_.compression() == compression_type::NONE) {
      return sec_.length();
    }

    return get_block_decompressor().uncompressed_size();
  }

  section_data get_section_data() {
    auto const& seg = segment();

    DWARFS_CHECK(
        sec_.check_fast(seg),
        fmt::format("attempt to access damaged {} section", sec_.name()));

    auto compression = sec_.compression();

    if (compression == compression_type::NONE) {
      return section_data{sec_, seg};
    }

    return section_data{
        block_decompressor::decompress(compression, sec_.data(seg))};
  }

 private:
  file_segment const& segment() {
    if (!segment_) {
      segment_ = sec_.segment(fv_);
    }

    return segment_.value();
  }

  block_decompressor get_block_decompressor() {
    auto const& seg = segment();

    if (!sec_.check_fast(seg)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("attempt to access damaged {} section", sec_.name()));
    }

    return {sec_.compression(), sec_.data(seg)};
  }

  file_view fv_;
  fs_section sec_;
  std::optional<file_segment> segment_{};
};

std::tuple<section_wrapper::section_data, metadata_v2>
make_metadata(logger& lgr, file_view const& mm, section_map const& sections,
              metadata_options const& options, int inode_offset,
              mlock_mode lock_mode, bool force_consistency_check,
              std::shared_ptr<performance_monitor const> const& perfmon) {
  LOG_PROXY(debug_logger_policy, lgr);
  auto schema_it = sections.find(section_type::METADATA_V2_SCHEMA);
  auto meta_it = sections.find(section_type::METADATA_V2);

  if (schema_it == sections.end()) {
    DWARFS_THROW(runtime_error, "no metadata schema found");
  }

  if (schema_it->second.size() > 1) {
    DWARFS_THROW(runtime_error, "multiple metadata schemas found");
  }

  if (meta_it == sections.end()) {
    DWARFS_THROW(runtime_error, "no metadata found");
  }

  if (meta_it->second.size() > 1) {
    DWARFS_THROW(runtime_error, "multiple metadata found");
  }

  auto meta_section = section_wrapper(mm, meta_it->second.front());
  auto schema_section = section_wrapper(mm, schema_it->second.front());

  auto meta_buffer = meta_section.get_section_data();
  auto schema_buffer = schema_section.get_section_data();

  if (lock_mode != mlock_mode::NONE) {
    std::error_code ec;

    meta_buffer.lock(ec);

    if (ec) {
      if (lock_mode == mlock_mode::MUST) {
        DWARFS_THROW(system_error, "mlock");
      }
      LOG_WARN << "mlock() failed: " << ec.message();
    }
  }

  return {meta_buffer,
          metadata_v2{lgr, schema_buffer.span(), meta_buffer.span(), options,
                      inode_offset, force_consistency_check, perfmon}};
}

} // namespace

template <typename LoggerPolicy>
class filesystem_ final {
 public:
  filesystem_(logger& lgr, os_access const& os, file_view const& mm,
              filesystem_options const& options,
              std::shared_ptr<performance_monitor const> const& perfmon);

  int check(filesystem_check_level level, size_t num_threads) const;
  void
  dump(std::ostream& os, fsinfo_options const& opts, history const& hist) const;
  std::string dump(fsinfo_options const& opts, history const& hist) const;
  nlohmann::json
  info_as_json(fsinfo_options const& opts, history const& hist) const;
  nlohmann::json metadata_as_json() const;
  std::string serialize_metadata_as_json(bool simple) const;
  filesystem_version version() const;
  bool has_valid_section_index() const;
  void walk(std::function<void(dir_entry_view)> const& func) const;
  void walk_data_order(std::function<void(dir_entry_view)> const& func) const;
  dir_entry_view root() const;
  std::optional<dir_entry_view> find(std::string_view path) const;
  std::optional<inode_view> find(int inode) const;
  std::optional<dir_entry_view> find(int inode, std::string_view name) const;
  file_stat getattr(inode_view entry, std::error_code& ec) const;
  file_stat getattr(inode_view entry, getattr_options const& opts,
                    std::error_code& ec) const;
  file_stat getattr(inode_view entry) const;
  file_stat getattr(inode_view entry, getattr_options const& opts) const;
  bool access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid) const;
  void access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const;
  std::optional<directory_view> opendir(inode_view entry) const;
  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const;
  size_t dirsize(directory_view dir) const;
  std::string
  readlink(inode_view entry, readlink_mode mode, std::error_code& ec) const;
  std::string readlink(inode_view entry, readlink_mode mode) const;
  void statvfs(vfs_stat* stbuf) const;
  int open(inode_view entry) const;
  int open(inode_view entry, std::error_code& ec) const;
  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence) const;
  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence,
                  std::error_code& ec) const;
  std::string read_string(uint32_t inode) const;
  std::string read_string(uint32_t inode, std::error_code& ec) const;
  std::string read_string(uint32_t inode, size_t size, file_off_t offset) const;
  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          std::error_code& ec) const;
  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset) const;
  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf, std::error_code& ec) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov, std::error_code& ec) const;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov) const;
  std::vector<std::future<block_range>> readv(uint32_t inode) const;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, std::error_code& ec) const;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset) const;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        std::error_code& ec) const;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov) const;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        std::error_code& ec) const;
  std::optional<file_extents_iterable> header() const;
  void set_num_workers(size_t num) { ir_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) {
    ir_.set_cache_tidy_config(cfg);
  }
  size_t num_blocks() const { return ir_.num_blocks(); }
  bool has_symlinks() const { return meta_.has_symlinks(); }
  bool has_sparse_files() const { return meta_.has_sparse_files(); }
  history get_history() const;
  nlohmann::json get_inode_info(inode_view entry) const {
    return meta_.get_inode_info(std::move(entry),
                                std::numeric_limits<size_t>::max());
  }
  nlohmann::json get_inode_info(inode_view entry, size_t max_chunks) const {
    return meta_.get_inode_info(std::move(entry), max_chunks);
  }
  std::vector<std::string> get_all_block_categories() const {
    return meta_.get_all_block_categories();
  }
  std::vector<file_stat::uid_type> get_all_uids() const {
    return meta_.get_all_uids();
  }
  std::vector<file_stat::gid_type> get_all_gids() const {
    return meta_.get_all_gids();
  }
  std::shared_ptr<filesystem_parser> get_parser() const {
    return std::make_shared<filesystem_parser>(
        LOG_GET_LOGGER, mm_, image_offset_, options_.image_size);
  }
  std::optional<std::string> get_block_category(size_t block_no) const {
    return meta_.get_block_category(block_no);
  }
  std::optional<nlohmann::json>
  get_block_category_metadata(size_t block_no) const {
    return meta_.get_block_category_metadata(block_no);
  }

  void cache_blocks_by_category(std::string_view category) const {
    auto const max_blocks = get_max_cache_blocks();
    auto block_numbers = meta_.get_block_numbers_by_category(category);
    if (block_numbers.size() > max_blocks) {
      LOG_WARN << "too many blocks in category " << category
               << ", caching only the first " << max_blocks << " out of "
               << block_numbers.size() << " blocks";
      block_numbers.resize(max_blocks);
    }
    ir_.cache_blocks(block_numbers);
  }

  void cache_all_blocks() const {
    auto const max_blocks = get_max_cache_blocks();
    auto num_blocks = ir_.num_blocks();
    if (num_blocks > max_blocks) {
      LOG_WARN << "too many blocks in filesystem, caching only the first "
               << max_blocks << " out of " << num_blocks << " blocks";
      num_blocks = max_blocks;
    }
    std::vector<size_t> block_numbers(num_blocks);
    std::iota(block_numbers.begin(), block_numbers.end(), 0);
    ir_.cache_blocks(block_numbers);
  }

  std::unique_ptr<thrift::metadata::metadata> thawed_metadata() const {
    return metadata_v2_utils(meta_).thaw();
  }

  std::unique_ptr<thrift::metadata::metadata> unpacked_metadata() const {
    return metadata_v2_utils(meta_).unpack();
  }

  std::unique_ptr<thrift::metadata::fs_options> thawed_fs_options() const {
    return metadata_v2_utils(meta_).thaw_fs_options();
  }

  std::future<block_range>
  read_raw_block_data(size_t block_no, size_t offset, size_t size) const {
    return ir_.read_raw_block_data(block_no, offset, size);
  }

 private:
  filesystem_parser make_fs_parser() const {
    return filesystem_parser(LOG_GET_LOGGER, mm_, image_offset_,
                             options_.image_size);
  }

  size_t get_max_cache_blocks() const {
    return options_.block_cache.max_bytes / meta_.block_size();
  }

  filesystem_info const* get_info(fsinfo_options const& opts) const;
  void check_section(fs_section const& section) const;
  std::string read_string_ec(uint32_t inode, size_t size, file_off_t offset,
                             std::error_code& ec) const;
  size_t read_ec(uint32_t inode, char* buf, size_t size, file_off_t offset,
                 std::error_code& ec) const;
  size_t readv_ec(uint32_t inode, iovec_read_buf& buf, size_t size,
                  file_off_t offset, size_t maxiov, std::error_code& ec) const;
  std::vector<std::future<block_range>>
  readv_ec(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
           std::error_code& ec) const;

  LOG_PROXY_DECL(LoggerPolicy);
  os_access const& os_;
  file_view mm_;
  metadata_v2 meta_;
  inode_reader_v2 ir_;
  mutable std::mutex mx_;
  std::optional<section_wrapper::section_data> meta_buffer_;
  std::optional<file_extents_iterable> header_;
  mutable block_access_level fsinfo_block_access_level_{
      block_access_level::no_access};
  mutable std::unique_ptr<filesystem_info const> fsinfo_;
  std::vector<fs_section> history_sections_;
  file_off_t const image_offset_;
  filesystem_options const options_;
  filesystem_version version_;
  bool has_valid_section_index_{false};
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(find_path)
  PERFMON_CLS_TIMER_DECL(find_inode)
  PERFMON_CLS_TIMER_DECL(find_inode_name)
  PERFMON_CLS_TIMER_DECL(getattr)
  PERFMON_CLS_TIMER_DECL(getattr_ec)
  PERFMON_CLS_TIMER_DECL(getattr_opts)
  PERFMON_CLS_TIMER_DECL(getattr_opts_ec)
  PERFMON_CLS_TIMER_DECL(access)
  PERFMON_CLS_TIMER_DECL(access_ec)
  PERFMON_CLS_TIMER_DECL(opendir)
  PERFMON_CLS_TIMER_DECL(readdir)
  PERFMON_CLS_TIMER_DECL(dirsize)
  PERFMON_CLS_TIMER_DECL(readlink)
  PERFMON_CLS_TIMER_DECL(readlink_ec)
  PERFMON_CLS_TIMER_DECL(statvfs)
  PERFMON_CLS_TIMER_DECL(open)
  PERFMON_CLS_TIMER_DECL(open_ec)
  PERFMON_CLS_TIMER_DECL(seek)
  PERFMON_CLS_TIMER_DECL(seek_ec)
  PERFMON_CLS_TIMER_DECL(read_string)
  PERFMON_CLS_TIMER_DECL(read_string_ec)
  PERFMON_CLS_TIMER_DECL(read)
  PERFMON_CLS_TIMER_DECL(read_ec)
  PERFMON_CLS_TIMER_DECL(readv_iovec)
  PERFMON_CLS_TIMER_DECL(readv_iovec_ec)
  PERFMON_CLS_TIMER_DECL(readv_future)
  PERFMON_CLS_TIMER_DECL(readv_future_ec)
};

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::check_section(fs_section const& section) const {
  check_section_logger(LOG_GET_LOGGER, section);
}

template <typename LoggerPolicy>
filesystem_info const*
filesystem_<LoggerPolicy>::get_info(fsinfo_options const& opts) const {
  std::lock_guard lock(mx_);

  if (!fsinfo_ || opts.block_access > fsinfo_block_access_level_) {
    auto parser = make_fs_parser();
    filesystem_info info;

    parser.rewind();

    while (auto s = parser.next_section()) {
      check_section(*s);

      auto section = section_wrapper(mm_, *s);

      if (s->type() == section_type::BLOCK) {
        ++info.block_count;
        info.compressed_block_size += s->length();
        info.compressed_block_sizes.push_back(s->length());
        if (opts.block_access >= block_access_level::unrestricted) {
          try {
            auto uncompressed_size = section.get_uncompressed_size();
            info.uncompressed_block_size += uncompressed_size;
            info.uncompressed_block_sizes.emplace_back(uncompressed_size);
          } catch (std::exception const&) {
            info.uncompressed_block_size += s->length();
            info.uncompressed_block_size_is_estimate = true;
            info.uncompressed_block_sizes.emplace_back(std::nullopt);
          }
        } else {
          info.uncompressed_block_size += s->length();
          info.uncompressed_block_size_is_estimate = true;
          info.uncompressed_block_sizes.emplace_back(std::nullopt);
        }
      } else if (s->type() == section_type::METADATA_V2) {
        info.compressed_metadata_size += s->length();
        try {
          info.uncompressed_metadata_size += section.get_uncompressed_size();
        } catch (std::exception const&) {
          info.uncompressed_metadata_size += s->length();
          info.uncompressed_metadata_size_is_estimate = true;
        }
      }
    }

    fsinfo_ = std::make_unique<filesystem_info>(info);
    fsinfo_block_access_level_ = opts.block_access;
  }

  return fsinfo_.get();
}

template <typename LoggerPolicy>
filesystem_<LoggerPolicy>::filesystem_(
    logger& lgr, os_access const& os, file_view const& mm,
    filesystem_options const& options,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : LOG_PROXY_INIT(lgr)
    , os_{os}
    , mm_{mm}
    , image_offset_{filesystem_parser(lgr, mm_, options.image_offset)
                        .image_offset()}
    , options_{options} // clang-format off
    PERFMON_CLS_PROXY_INIT(perfmon, "filesystem_v2")
    PERFMON_CLS_TIMER_INIT(find_path)
    PERFMON_CLS_TIMER_INIT(find_inode)
    PERFMON_CLS_TIMER_INIT(find_inode_name)
    PERFMON_CLS_TIMER_INIT(getattr)
    PERFMON_CLS_TIMER_INIT(getattr_ec)
    PERFMON_CLS_TIMER_INIT(getattr_opts)
    PERFMON_CLS_TIMER_INIT(getattr_opts_ec)
    PERFMON_CLS_TIMER_INIT(access)
    PERFMON_CLS_TIMER_INIT(access_ec)
    PERFMON_CLS_TIMER_INIT(opendir)
    PERFMON_CLS_TIMER_INIT(readdir)
    PERFMON_CLS_TIMER_INIT(dirsize)
    PERFMON_CLS_TIMER_INIT(readlink)
    PERFMON_CLS_TIMER_INIT(readlink_ec)
    PERFMON_CLS_TIMER_INIT(statvfs)
    PERFMON_CLS_TIMER_INIT(open)
    PERFMON_CLS_TIMER_INIT(open_ec)
    PERFMON_CLS_TIMER_INIT(seek)
    PERFMON_CLS_TIMER_INIT(seek_ec)
    PERFMON_CLS_TIMER_INIT(read_string)
    PERFMON_CLS_TIMER_INIT(read_string_ec)
    PERFMON_CLS_TIMER_INIT(read)
    PERFMON_CLS_TIMER_INIT(read_ec)
    PERFMON_CLS_TIMER_INIT(readv_iovec)
    PERFMON_CLS_TIMER_INIT(readv_iovec_ec)
    PERFMON_CLS_TIMER_INIT(readv_future)
    PERFMON_CLS_TIMER_INIT(readv_future_ec) // clang-format on
{
  block_cache cache(lgr, os_, mm_, options.block_cache, perfmon);
  auto parser = make_fs_parser();

  if (parser.has_index()) {
    LOG_DEBUG << "found valid section index";
    has_valid_section_index_ = true;
  }

  header_ = parser.header();
  version_ = parser.fs_version();

  section_map sections;

  while (auto s = parser.next_section()) {
    if (s->type() == section_type::BLOCK) {
      // Don't use check_section() here because it'll trigger the lazy
      // section to load, defeating the purpose of the section index.
      // See github issue #183.
      LOG_DEBUG << "section " << s->name() << " @ " << s->start() << " ["
                << s->length() << " bytes]";

      cache.insert(*s);
    } else {
      check_section(*s);

      if (!s->check_fast_mm(mm_)) {
        switch (s->type()) {
        case section_type::METADATA_V2:
        case section_type::METADATA_V2_SCHEMA:
          DWARFS_THROW(runtime_error,
                       "checksum error in section: " + s->name());
          break;

        default:
          LOG_WARN << "checksum error in section: " << s->name();
          break;
        }
      }

      sections[s->type()].push_back(*s);
    }
  }

  std::tie(meta_buffer_, meta_) =
      make_metadata(lgr, mm_, sections, options.metadata, options.inode_offset,
                    options.lock_mode, !parser.has_checksums(), perfmon);

  LOG_DEBUG << "read " << cache.block_count() << " blocks and " << meta_.size()
            << " bytes of metadata";

  cache.set_block_size(meta_.block_size());

  ir_ = inode_reader_v2(lgr, os_, std::move(cache), options.inode_reader,
                        perfmon);

  if (auto it = sections.find(section_type::HISTORY); it != sections.end()) {
    history_sections_ = std::move(it->second);
  }
}

template <typename LoggerPolicy>
history filesystem_<LoggerPolicy>::get_history() const {
  history hist({.with_timestamps = true});

  for (auto& section : history_sections_) {
    if (section.check_fast_mm(mm_)) {
      auto buffer = section_wrapper(mm_, section).get_section_data();
      hist.parse_append(buffer.span());
    }
  }

  return hist;
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::check(filesystem_check_level level,
                                     size_t num_threads) const {
  auto parser = make_fs_parser();

  worker_group wg(LOG_GET_LOGGER, os_, "fscheck", num_threads);
  std::vector<std::future<fs_section>> sections;

  while (auto sp = parser.next_section()) {
    check_section(*sp);

    std::packaged_task<fs_section()> task{[this, level, s = std::move(*sp)] {
      auto seg = s.segment(mm_);
      if (level == filesystem_check_level::INTEGRITY ||
          level == filesystem_check_level::FULL) {
        fs_section_checker checker(seg);
        if (!checker.verify(s)) {
          DWARFS_THROW(runtime_error,
                       "integrity check error in section: " + s.name());
        }
      } else {
        if (!s.check_fast(seg)) {
          DWARFS_THROW(runtime_error, "checksum error in section: " + s.name());
        }
      }

      return s;
    }};

    sections.emplace_back(task.get_future());
    wg.add_job(std::move(task));
  }

  std::unordered_set<section_type> seen;
  int errors = 0;

  for (auto& sf : sections) {
    try {
      auto s = sf.get();

      if (s.type() != section_type::BLOCK &&
          s.type() != section_type::HISTORY) {
        if (!seen.emplace(s.type()).second) {
          DWARFS_THROW(runtime_error, "duplicate section: " + s.name());
        }
      }
    } catch (std::exception const& e) {
      LOG_ERROR << exception_str(e);
      ++errors;
    }
  }

  if (level == filesystem_check_level::FULL) {
    try {
      meta_.check_consistency();
    } catch (std::exception const& e) {
      LOG_ERROR << exception_str(e);
      ++errors;
    }
  }

  return errors;
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::dump(std::ostream& os,
                                     fsinfo_options const& opts,
                                     history const& hist) const {
  auto parser = make_fs_parser();

  if (opts.features.has(fsinfo_feature::version)) {
    os << "DwarFS version " << parser.version();
    if (auto off = parser.image_offset(); off > 0) {
      os << " at offset " << off;
    }
    os << "\n";
  }

  size_t block_no{0};

  if (opts.features.has(fsinfo_feature::section_details)) {
    while (auto sp = parser.next_section()) {
      auto const& s = *sp;

      auto section = section_wrapper(mm_, s);
      auto bd = section.try_get_block_decompressor();
      std::string block_size;

      if (bd) {
        auto uncompressed_size = bd->uncompressed_size();
        float compression_ratio = float(s.length()) / uncompressed_size;
        block_size = fmt::format("blocksize={}, ratio={:.2f}%",
                                 uncompressed_size, 100.0 * compression_ratio);
      } else {
        block_size = fmt::format("blocksize={} (estimate)", s.length());
      }

      std::string category;

      if (s.type() == section_type::BLOCK) {
        if (auto catstr = meta_.get_block_category(block_no)) {
          category = fmt::format(", category={}", catstr.value());
        }
        ++block_no;
      }

      std::string extra;

      if (auto m = meta_.get_block_category_metadata(block_no)) {
        extra = fmt::format(", metadata={}", m->dump());
      } else if (bd) {
        if (auto m = bd->metadata()) {
          extra = fmt::format(", metadata={}", *m);
        }
      }

      if (s.type() == section_type::SECTION_INDEX) {
        if (parser.has_index()) {
          extra = " [VALID]";
        } else {
          extra = " [INVALID]";
        }
      }

      os << "SECTION " << s.description() << ", " << block_size << category
         << extra << "\n";
    }
  }

  if (opts.features.has(fsinfo_feature::history)) {
    hist.dump(os);
  }

  metadata_v2_utils(meta_).dump(
      os, opts, get_info(opts), [&](std::string const& indent, uint32_t inode) {
        std::error_code ec;
        auto chunks = meta_.get_chunks(inode, ec);
        if (!ec) {
          os << indent << chunks.size() << " chunks in inode " << inode << "\n";
          ir_.dump(os, indent + "  ", chunks);
        } else {
          LOG_ERROR << "error reading chunks for inode " << inode << ": "
                    << ec.message();
        }
      });
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::dump(fsinfo_options const& opts,
                                            history const& hist) const {
  std::ostringstream oss;
  dump(oss, opts, hist);
  return oss.str();
}

template <typename LoggerPolicy>
nlohmann::json
filesystem_<LoggerPolicy>::info_as_json(fsinfo_options const& opts,
                                        history const& hist) const {
  auto parser = make_fs_parser();

  auto info = nlohmann::json::object();

  if (opts.features.has(fsinfo_feature::version)) {
    info["version"] = nlohmann::json::object({
        {"major", parser.major_version()},
        {"minor", parser.minor_version()},
        {"header", parser.header_version()},
    });
    info["image_offset"] = parser.image_offset();
  }

  if (opts.features.has(fsinfo_feature::history)) {
    info["history"] = hist.as_json();
  }

  if (opts.features.has(fsinfo_feature::section_details)) {
    size_t block_no{0};

    while (auto sp = parser.next_section()) {
      auto const& s = *sp;

      auto section = section_wrapper(mm_, s);

      bool checksum_ok = s.check_fast_mm(mm_);

      nlohmann::json section_info{
          {"type", s.name()},
          {"compression", s.compression_name()},
          {"compressed_size", s.length()},
          {"checksum_ok", checksum_ok},
      };

      auto bd = section.try_get_block_decompressor();

      if (bd) {
        auto uncompressed_size = bd->uncompressed_size();
        section_info["size"] = uncompressed_size;
        section_info["ratio"] = float(s.length()) / uncompressed_size;

        if (auto m = meta_.get_block_category_metadata(block_no)) {
          section_info["metadata"] = *m;
        } else if (auto m = bd->metadata()) {
          section_info["metadata"] = nlohmann::json::parse(*m);
        }
      }

      if (s.type() == section_type::BLOCK) {
        if (auto catstr = meta_.get_block_category(block_no)) {
          section_info["category"] = catstr.value();
        }
        ++block_no;
      }

      if (s.type() == section_type::SECTION_INDEX) {
        section_info["valid"] = parser.has_index();
      }

      info["sections"].push_back(std::move(section_info));
      info["valid_section_index"] = parser.has_index();
    }
  }

  info.update(metadata_v2_utils(meta_).info_as_json(opts, get_info(opts)));

  return info;
}

template <typename LoggerPolicy>
nlohmann::json filesystem_<LoggerPolicy>::metadata_as_json() const {
  return metadata_v2_utils(meta_).as_json();
}

template <typename LoggerPolicy>
std::string
filesystem_<LoggerPolicy>::serialize_metadata_as_json(bool simple) const {
  return metadata_v2_utils(meta_).serialize_as_json(simple);
}

template <typename LoggerPolicy>
filesystem_version filesystem_<LoggerPolicy>::version() const {
  return version_;
}

template <typename LoggerPolicy>
bool filesystem_<LoggerPolicy>::has_valid_section_index() const {
  return has_valid_section_index_;
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk(
    std::function<void(dir_entry_view)> const& func) const {
  meta_.walk(func);
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk_data_order(
    std::function<void(dir_entry_view)> const& func) const {
  meta_.walk_data_order(func);
}

template <typename LoggerPolicy>
dir_entry_view filesystem_<LoggerPolicy>::root() const {
  return meta_.root();
}

template <typename LoggerPolicy>
std::optional<dir_entry_view>
filesystem_<LoggerPolicy>::find(std::string_view path) const {
  PERFMON_CLS_SCOPED_SECTION(find_path)
  return meta_.find(path);
}

template <typename LoggerPolicy>
std::optional<inode_view> filesystem_<LoggerPolicy>::find(int inode) const {
  PERFMON_CLS_SCOPED_SECTION(find_inode)
  return meta_.find(inode);
}

template <typename LoggerPolicy>
std::optional<dir_entry_view>
filesystem_<LoggerPolicy>::find(int inode, std::string_view name) const {
  PERFMON_CLS_SCOPED_SECTION(find_inode_name)
  return meta_.find(inode, name);
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                             std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_ec)
  return meta_.getattr(std::move(entry), ec);
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(getattr)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.getattr(std::move(entry), ec); });
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                             getattr_options const& opts,
                                             std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_opts_ec)
  return meta_.getattr(std::move(entry), opts, ec);
}

template <typename LoggerPolicy>
file_stat
filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                   getattr_options const& opts) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_opts)
  return call_ec_throw([&](std::error_code& ec) {
    return meta_.getattr(std::move(entry), opts, ec);
  });
}

template <typename LoggerPolicy>
bool filesystem_<LoggerPolicy>::access(inode_view entry, int mode,
                                       file_stat::uid_type uid,
                                       file_stat::gid_type gid) const {
  PERFMON_CLS_SCOPED_SECTION(access)
  std::error_code ec;
  meta_.access(std::move(entry), mode, uid, gid, ec);
  return !ec;
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::access(inode_view entry, int mode,
                                       file_stat::uid_type uid,
                                       file_stat::gid_type gid,
                                       std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(access_ec)
  meta_.access(std::move(entry), mode, uid, gid, ec);
}

template <typename LoggerPolicy>
std::optional<directory_view>
filesystem_<LoggerPolicy>::opendir(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(opendir)
  return meta_.opendir(std::move(entry));
}

template <typename LoggerPolicy>
std::optional<dir_entry_view>
filesystem_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readdir)
  return meta_.readdir(dir, offset);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::dirsize(directory_view dir) const {
  PERFMON_CLS_SCOPED_SECTION(dirsize)
  return meta_.dirsize(dir);
}

template <typename LoggerPolicy>
std::string
filesystem_<LoggerPolicy>::readlink(inode_view entry, readlink_mode mode,
                                    std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readlink_ec)
  return meta_.readlink(std::move(entry), mode, ec);
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::readlink(inode_view entry,
                                                readlink_mode mode) const {
  PERFMON_CLS_SCOPED_SECTION(readlink)
  return call_ec_throw([&](std::error_code& ec) {
    return meta_.readlink(std::move(entry), mode, ec);
  });
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::statvfs(vfs_stat* stbuf) const {
  PERFMON_CLS_SCOPED_SECTION(statvfs)
  // TODO: not sure if that's the right abstraction...
  meta_.statvfs(stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(inode_view entry,
                                    std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(open_ec)
  return meta_.open(std::move(entry), ec);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(open)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.open(std::move(entry), ec); });
}

template <typename LoggerPolicy>
file_off_t
filesystem_<LoggerPolicy>::seek(uint32_t inode, file_off_t offset,
                                seek_whence whence, std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(seek_ec)
  return meta_.seek(inode, offset, whence, ec);
}

template <typename LoggerPolicy>
file_off_t filesystem_<LoggerPolicy>::seek(uint32_t inode, file_off_t offset,
                                           seek_whence whence) const {
  PERFMON_CLS_SCOPED_SECTION(seek)
  return call_ec_throw([&](std::error_code& ec) {
    return meta_.seek(inode, offset, whence, ec);
  });
}

template <typename LoggerPolicy>
std::string
filesystem_<LoggerPolicy>::read_string_ec(uint32_t inode, size_t size,
                                          file_off_t offset,
                                          std::error_code& ec) const {
  auto chunks = meta_.get_chunks(inode, ec);
  if (!ec) {
    return ir_.read_string(inode, size, offset, chunks, ec);
  }
  return {};
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::read_string(uint32_t inode,
                                                   std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read_string_ec)
  return read_string_ec(inode, KReadFullFile, 0, ec);
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::read_string(uint32_t inode) const {
  PERFMON_CLS_SCOPED_SECTION(read_string)
  return call_ec_throw([&](std::error_code& ec) {
    return read_string_ec(inode, KReadFullFile, 0, ec);
  });
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::read_string(uint32_t inode, size_t size,
                                                   file_off_t offset,
                                                   std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read_string_ec)
  return read_string_ec(inode, size, offset, ec);
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::read_string(uint32_t inode, size_t size,
                                                   file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(read_string)
  return call_ec_throw([&](std::error_code& ec) {
    return read_string_ec(inode, size, offset, ec);
  });
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::read_ec(uint32_t inode, char* buf,
                                          size_t size, file_off_t offset,
                                          std::error_code& ec) const {
  auto chunks = meta_.get_chunks(inode, ec);
  if (!ec) {
    return ir_.read(buf, inode, size, offset, chunks, ec);
  }
  return 0;
}

template <typename LoggerPolicy>
size_t
filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                file_off_t offset, std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read_ec)
  return read_ec(inode, buf, size, offset, ec);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                       file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(read)
  return call_ec_throw([&](std::error_code& ec) {
    return read_ec(inode, buf, size, offset, ec);
  });
}

template <typename LoggerPolicy>
size_t
filesystem_<LoggerPolicy>::readv_ec(uint32_t inode, iovec_read_buf& buf,
                                    size_t size, file_off_t offset,
                                    size_t maxiov, std::error_code& ec) const {
  auto chunks = meta_.get_chunks(inode, ec);
  if (!ec) {
    return ir_.readv(buf, inode, size, offset, maxiov, chunks, ec);
  }
  return 0;
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec_ec)
  return readv_ec(inode, buf, KReadFullFile, 0, kDefaultMaxIOV, ec);
}

template <typename LoggerPolicy>
size_t
filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, buf, KReadFullFile, 0, kDefaultMaxIOV, ec);
  });
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        size_t size, file_off_t offset,
                                        std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec_ec)
  return readv_ec(inode, buf, size, offset, kDefaultMaxIOV, ec);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        size_t size, file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, buf, size, offset, kDefaultMaxIOV, ec);
  });
}

template <typename LoggerPolicy>
size_t
filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                 size_t size, file_off_t offset, size_t maxiov,
                                 std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec_ec)
  return readv_ec(inode, buf, size, offset, maxiov, ec);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        size_t size, file_off_t offset,
                                        size_t maxiov) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, buf, size, offset, maxiov, ec);
  });
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv_ec(uint32_t inode, size_t size,
                                    file_off_t offset, size_t maxiov,
                                    std::error_code& ec) const {
  auto chunks = meta_.get_chunks(inode, ec);
  if (!ec) {
    return ir_.readv(inode, size, offset, maxiov, chunks, ec);
  }
  return {};
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future_ec)
  return readv_ec(inode, KReadFullFile, 0, kDefaultMaxIOV, ec);
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, KReadFullFile, 0, kDefaultMaxIOV, ec);
  });
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size, file_off_t offset,
                                 std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future_ec)
  return readv_ec(inode, size, offset, kDefaultMaxIOV, ec);
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size,
                                 file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, size, offset, kDefaultMaxIOV, ec);
  });
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size, file_off_t offset,
                                 size_t maxiov, std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future_ec)
  return readv_ec(inode, size, offset, maxiov, ec);
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size, file_off_t offset,
                                 size_t maxiov) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, size, offset, maxiov, ec);
  });
}

template <typename LoggerPolicy>
std::optional<file_extents_iterable> filesystem_<LoggerPolicy>::header() const {
  return header_;
}

template <typename LoggerPolicy, typename Base>
class filesystem_common_ : public Base {
 public:
  filesystem_common_(logger& lgr, os_access const& os, file_view const& mm,
                     filesystem_options const& options,
                     std::shared_ptr<performance_monitor const> const& perfmon)
      : fs_{lgr, os, std::move(mm), options, perfmon} {}

  filesystem_version version() const override { return fs_.version(); }
  bool has_valid_section_index() const override {
    return fs_.has_valid_section_index();
  }
  void walk(std::function<void(dir_entry_view)> const& func) const override {
    fs_.walk(func);
  }
  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override {
    fs_.walk_data_order(func);
  }
  dir_entry_view root() const override { return fs_.root(); }
  std::optional<dir_entry_view> find(std::string_view path) const override {
    return fs_.find(path);
  }
  std::optional<inode_view> find(int inode) const override {
    return fs_.find(inode);
  }
  std::optional<dir_entry_view>
  find(int inode, std::string_view name) const override {
    return fs_.find(inode, name);
  }
  file_stat getattr(inode_view entry, std::error_code& ec) const override {
    return fs_.getattr(entry, ec);
  }
  file_stat getattr(inode_view entry, getattr_options const& opts,
                    std::error_code& ec) const override {
    return fs_.getattr(entry, opts, ec);
  }
  file_stat getattr(inode_view entry) const override {
    return fs_.getattr(entry);
  }
  file_stat
  getattr(inode_view entry, getattr_options const& opts) const override {
    return fs_.getattr(entry, opts);
  }
  bool access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid) const override {
    return fs_.access(entry, mode, uid, gid);
  }
  void access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const override {
    return fs_.access(entry, mode, uid, gid, ec);
  }
  std::optional<directory_view> opendir(inode_view entry) const override {
    return fs_.opendir(entry);
  }
  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const override {
    return fs_.readdir(dir, offset);
  }
  size_t dirsize(directory_view dir) const override { return fs_.dirsize(dir); }
  std::string readlink(inode_view entry, readlink_mode mode,
                       std::error_code& ec) const override {
    return fs_.readlink(entry, mode, ec);
  }
  std::string readlink(inode_view entry, readlink_mode mode) const override {
    return fs_.readlink(entry, mode);
  }
  void statvfs(vfs_stat* stbuf) const override { fs_.statvfs(stbuf); }
  int open(inode_view entry) const override { return fs_.open(entry); }
  int open(inode_view entry, std::error_code& ec) const override {
    return fs_.open(entry, ec);
  }
  file_off_t
  seek(uint32_t inode, file_off_t offset, seek_whence whence) const override {
    return fs_.seek(inode, offset, whence);
  }
  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence,
                  std::error_code& ec) const override {
    return fs_.seek(inode, offset, whence, ec);
  }
  std::string read_string(uint32_t inode) const override {
    return fs_.read_string(inode);
  }
  std::string read_string(uint32_t inode, std::error_code& ec) const override {
    return fs_.read_string(inode, ec);
  }
  std::string
  read_string(uint32_t inode, size_t size, file_off_t offset) const override {
    return fs_.read_string(inode, size, offset);
  }
  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          std::error_code& ec) const override {
    return fs_.read_string(inode, size, offset, ec);
  }
  size_t read(uint32_t inode, char* buf, size_t size,
              file_off_t offset) const override {
    return fs_.read(inode, buf, size, offset);
  }
  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const override {
    return fs_.read(inode, buf, size, offset, ec);
  }
  size_t readv(uint32_t inode, iovec_read_buf& buf) const override {
    return fs_.readv(inode, buf);
  }
  size_t readv(uint32_t inode, iovec_read_buf& buf,
               std::error_code& ec) const override {
    return fs_.readv(inode, buf, ec);
  }
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const override {
    return fs_.readv(inode, buf, size, offset, ec);
  }
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset) const override {
    return fs_.readv(inode, buf, size, offset);
  }
  size_t
  readv(uint32_t inode, iovec_read_buf& buf, size_t size, file_off_t offset,
        size_t maxiov, std::error_code& ec) const override {
    return fs_.readv(inode, buf, size, offset, maxiov, ec);
  }
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov) const override {
    return fs_.readv(inode, buf, size, offset, maxiov);
  }
  std::vector<std::future<block_range>> readv(uint32_t inode) const override {
    return fs_.readv(inode);
  }
  std::vector<std::future<block_range>>
  readv(uint32_t inode, std::error_code& ec) const override {
    return fs_.readv(inode, ec);
  }
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset) const override {
    return fs_.readv(inode, size, offset);
  }
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        std::error_code& ec) const override {
    return fs_.readv(inode, size, offset, ec);
  }
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        size_t maxiov) const override {
    return fs_.readv(inode, size, offset, maxiov);
  }
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        std::error_code& ec) const override {
    return fs_.readv(inode, size, offset, maxiov, ec);
  }
  void set_num_workers(size_t num) override { fs_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    fs_.set_cache_tidy_config(cfg);
  }
  size_t num_blocks() const override { return fs_.num_blocks(); }
  bool has_symlinks() const override { return fs_.has_symlinks(); }
  bool has_sparse_files() const override { return fs_.has_sparse_files(); }
  nlohmann::json get_inode_info(inode_view entry) const override {
    return fs_.get_inode_info(entry);
  }
  nlohmann::json
  get_inode_info(inode_view entry, size_t max_chunks) const override {
    return fs_.get_inode_info(entry, max_chunks);
  }
  std::vector<std::string> get_all_block_categories() const override {
    return fs_.get_all_block_categories();
  }
  std::vector<file_stat::uid_type> get_all_uids() const override {
    return fs_.get_all_uids();
  }
  std::vector<file_stat::gid_type> get_all_gids() const override {
    return fs_.get_all_gids();
  }
  std::shared_ptr<filesystem_parser> get_parser() const override {
    return fs_.get_parser();
  }
  std::optional<std::string>
  get_block_category(size_t block_no) const override {
    return fs_.get_block_category(block_no);
  }
  std::optional<nlohmann::json>
  get_block_category_metadata(size_t block_no) const override {
    return fs_.get_block_category_metadata(block_no);
  }

  void cache_blocks_by_category(std::string_view category) const override {
    fs_.cache_blocks_by_category(category);
  }

  void cache_all_blocks() const override { fs_.cache_all_blocks(); }

 protected:
  filesystem_<LoggerPolicy> const& fs() const { return fs_; }

 private:
  filesystem_<LoggerPolicy> fs_;
};

template <typename LoggerPolicy>
using filesystem_lite_ =
    filesystem_common_<LoggerPolicy, filesystem_v2_lite::impl_lite>;

template <typename LoggerPolicy>
class filesystem_full_
    : public filesystem_common_<LoggerPolicy, filesystem_v2::impl> {
 public:
  using filesystem_common_<LoggerPolicy, filesystem_v2::impl>::fs;

  filesystem_full_(logger& lgr, os_access const& os, file_view const& mm,
                   filesystem_options const& options,
                   std::shared_ptr<performance_monitor const> const& perfmon)
      : filesystem_common_<LoggerPolicy, filesystem_v2::impl>(
            lgr, os, std::move(mm), options, perfmon)
      , history_{fs().get_history()} {}

  int check(filesystem_check_level level, size_t num_threads) const override {
    return fs().check(level, num_threads);
  }
  void dump(std::ostream& os, fsinfo_options const& opts) const override {
    fs().dump(os, opts, history_);
  }
  std::string dump(fsinfo_options const& opts) const override {
    return fs().dump(opts, history_);
  }
  nlohmann::json info_as_json(fsinfo_options const& opts) const override {
    return fs().info_as_json(opts, history_);
  }
  nlohmann::json metadata_as_json() const override {
    return fs().metadata_as_json();
  }
  std::string serialize_metadata_as_json(bool simple) const override {
    return fs().serialize_metadata_as_json(simple);
  }
  std::optional<file_extents_iterable> header() const override {
    return fs().header();
  }
  history const& get_history() const override { return history_; }
  std::unique_ptr<thrift::metadata::metadata> thawed_metadata() const override {
    return fs().thawed_metadata();
  }
  std::unique_ptr<thrift::metadata::metadata>
  unpacked_metadata() const override {
    return fs().unpacked_metadata();
  }
  std::unique_ptr<thrift::metadata::fs_options>
  thawed_fs_options() const override {
    return fs().thawed_fs_options();
  }
  std::future<block_range> read_raw_block_data(size_t block_no, size_t offset,
                                               size_t size) const override {
    return fs().read_raw_block_data(block_no, offset, size);
  }

 private:
  history history_;
};

} // namespace internal

filesystem_v2_lite::filesystem_v2_lite(logger& lgr, os_access const& os,
                                       std::filesystem::path const& path)
    : filesystem_v2_lite(lgr, os, os.open_file(os.canonical(path))) {}

filesystem_v2_lite::filesystem_v2_lite(
    logger& lgr, os_access const& os, std::filesystem::path const& path,
    filesystem_options const& options,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : filesystem_v2_lite(lgr, os, os.open_file(os.canonical(path)), options,
                         perfmon) {}

filesystem_v2_lite::filesystem_v2_lite(logger& lgr, os_access const& os,
                                       file_view const& mm)
    : filesystem_v2_lite(lgr, os, mm, filesystem_options()) {}

filesystem_v2_lite::filesystem_v2_lite(
    logger& lgr, os_access const& os, file_view const& mm,
    filesystem_options const& options,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : filesystem_v2_lite(
          make_unique_logging_object<filesystem_v2_lite::impl_lite,
                                     internal::filesystem_lite_,
                                     logger_policies>(lgr, os, mm, options,
                                                      perfmon)) {}

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             std::filesystem::path const& path)
    : filesystem_v2(lgr, os, os.open_file(os.canonical(path))) {}

filesystem_v2::filesystem_v2(
    logger& lgr, os_access const& os, std::filesystem::path const& path,
    filesystem_options const& options,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : filesystem_v2(lgr, os, os.open_file(os.canonical(path)), options,
                    perfmon) {}

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             file_view const& mm)
    : filesystem_v2(lgr, os, mm, filesystem_options()) {}

filesystem_v2::filesystem_v2(
    logger& lgr, os_access const& os, file_view const& mm,
    filesystem_options const& options,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : filesystem_v2_lite(
          make_unique_logging_object<
              filesystem_v2::impl, internal::filesystem_full_, logger_policies>(
              lgr, os, mm, options, perfmon)) {}

int filesystem_v2::identify(logger& lgr, os_access const& os,
                            file_view const& mm, std::ostream& output,
                            int detail_level, size_t num_readers,
                            bool check_integrity, file_off_t image_offset) {
  filesystem_options fsopts;
  fsopts.image_offset = image_offset;
  filesystem_v2 fs(lgr, os, mm, fsopts);

  auto errors = fs.check(check_integrity ? filesystem_check_level::FULL
                                         : filesystem_check_level::CHECKSUM,
                         num_readers);

  fs.dump(output, {.features = fsinfo_features::for_level(detail_level)});

  return errors;
}

std::optional<file_extents_iterable>
filesystem_v2::header(logger& lgr, file_view const& mm) {
  return header(lgr, mm, filesystem_options::IMAGE_OFFSET_AUTO);
}

std::optional<file_extents_iterable>
filesystem_v2::header(logger& lgr, file_view const& mm,
                      file_off_t image_offset) {
  return internal::filesystem_parser(lgr, mm, image_offset).header();
}

int filesystem_v2::check(filesystem_check_level level,
                         size_t num_threads) const {
  return full_().check(level, num_threads);
}

void filesystem_v2::dump(std::ostream& os, fsinfo_options const& opts) const {
  full_().dump(os, opts);
}

std::string filesystem_v2::dump(fsinfo_options const& opts) const {
  return full_().dump(opts);
}

nlohmann::json filesystem_v2::info_as_json(fsinfo_options const& opts) const {
  return full_().info_as_json(opts);
}

nlohmann::json filesystem_v2::metadata_as_json() const {
  return full_().metadata_as_json();
}

std::string filesystem_v2::serialize_metadata_as_json(bool simple) const {
  return full_().serialize_metadata_as_json(simple);
}

std::optional<file_extents_iterable> filesystem_v2::header() const {
  return full_().header();
}

history const& filesystem_v2::get_history() const {
  return full_().get_history();
}

std::unique_ptr<thrift::metadata::metadata>
filesystem_v2::thawed_metadata() const {
  return full_().thawed_metadata();
}

std::unique_ptr<thrift::metadata::metadata>
filesystem_v2::unpacked_metadata() const {
  return full_().unpacked_metadata();
}

std::unique_ptr<thrift::metadata::fs_options>
filesystem_v2::thawed_fs_options() const {
  return full_().thawed_fs_options();
}

std::future<block_range>
filesystem_v2::read_raw_block_data(size_t block_no, size_t offset,
                                   size_t size) const {
  return full_().read_raw_block_data(block_no, offset, size);
}

auto filesystem_v2::full_() const -> impl const& { return this->as_<impl>(); }

} // namespace dwarfs::reader

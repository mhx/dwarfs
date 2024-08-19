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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <version>

#include <fmt/format.h>

#include <dwarfs/block_compressor.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/mmif.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/internal/worker_group.h>
#include <dwarfs/reader/internal/block_cache.h>
#include <dwarfs/reader/internal/filesystem_parser.h>
#include <dwarfs/reader/internal/inode_reader_v2.h>
#include <dwarfs/reader/internal/metadata_v2.h>
#include <dwarfs/writer/internal/block_data.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>

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

size_t
get_uncompressed_section_size(std::shared_ptr<mmif> mm, fs_section const& sec) {
  if (sec.compression() == compression_type::NONE) {
    return sec.length();
  }

  if (!sec.check_fast(*mm)) {
    DWARFS_THROW(
        runtime_error,
        fmt::format("attempt to access damaged {} section", sec.name()));
  }

  std::vector<uint8_t> tmp;
  auto span = sec.data(*mm);
  block_decompressor bd(sec.compression(), span.data(), span.size(), tmp);
  return bd.uncompressed_size();
}

std::optional<size_t>
try_get_uncompressed_section_size(std::shared_ptr<mmif> mm,
                                  fs_section const& sec) {
  if (sec.check_fast(*mm)) {
    try {
      return get_uncompressed_section_size(mm, sec);
    } catch (std::exception const&) {
    }
  }

  return std::nullopt;
}

std::span<uint8_t const>
get_section_data(std::shared_ptr<mmif> mm, fs_section const& section,
                 std::vector<uint8_t>& buffer, bool force_buffer) {
  DWARFS_CHECK(
      section.check_fast(*mm),
      fmt::format("attempt to access damaged {} section", section.name()));

  auto span = section.data(*mm);
  auto compression = section.compression();

  if (!force_buffer && compression == compression_type::NONE) {
    return span;
  }

  buffer =
      block_decompressor::decompress(compression, span.data(), span.size());

  return buffer;
}

metadata_v2
make_metadata(logger& lgr, std::shared_ptr<mmif> mm,
              section_map const& sections, std::vector<uint8_t>& schema_buffer,
              std::vector<uint8_t>& meta_buffer,
              const metadata_options& options, int inode_offset = 0,
              bool force_buffers = false,
              mlock_mode lock_mode = mlock_mode::NONE,
              bool force_consistency_check = false,
              std::shared_ptr<performance_monitor const> perfmon = nullptr) {
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

  auto& meta_section = meta_it->second.front();

  auto meta_section_range =
      get_section_data(mm, meta_section, meta_buffer, force_buffers);

  if (lock_mode != mlock_mode::NONE) {
    if (auto ec = mm->lock(meta_section.start(), meta_section_range.size())) {
      if (lock_mode == mlock_mode::MUST) {
        DWARFS_THROW(system_error, "mlock");
      } else {
        LOG_WARN << "mlock() failed: " << ec.message();
      }
    }
  }

  // don't keep the compressed metadata in cache
  if (meta_section.compression() != compression_type::NONE) {
    if (auto ec = mm->release(meta_section.start(), meta_section.length())) {
      LOG_INFO << "madvise() failed: " << ec.message();
    }
  }

  return metadata_v2(lgr,
                     get_section_data(mm, schema_it->second.front(),
                                      schema_buffer, force_buffers),
                     meta_section_range, options, inode_offset,
                     force_consistency_check, perfmon);
}

} // namespace

template <typename LoggerPolicy>
class filesystem_ final : public filesystem_v2::impl {
 public:
  filesystem_(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
              const filesystem_options& options,
              std::shared_ptr<performance_monitor const> perfmon);

  int check(filesystem_check_level level, size_t num_threads) const override;
  void dump(std::ostream& os, fsinfo_options const& opts) const override;
  std::string dump(fsinfo_options const& opts) const override;
  nlohmann::json info_as_json(fsinfo_options const& opts) const override;
  nlohmann::json metadata_as_json() const override;
  std::string serialize_metadata_as_json(bool simple) const override;
  void walk(std::function<void(dir_entry_view)> const& func) const override;
  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override;
  std::optional<inode_view> find(const char* path) const override;
  std::optional<inode_view> find(int inode) const override;
  std::optional<inode_view> find(int inode, const char* name) const override;
  file_stat getattr(inode_view entry, std::error_code& ec) const override;
  file_stat getattr(inode_view entry, getattr_options const& opts,
                    std::error_code& ec) const override;
  file_stat getattr(inode_view entry) const override;
  file_stat
  getattr(inode_view entry, getattr_options const& opts) const override;
  bool access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid) const override;
  void access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const override;
  std::optional<directory_view> opendir(inode_view entry) const override;
  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const override;
  size_t dirsize(directory_view dir) const override;
  std::string readlink(inode_view entry, readlink_mode mode,
                       std::error_code& ec) const override;
  std::string readlink(inode_view entry, readlink_mode mode) const override;
  void statvfs(vfs_stat* stbuf) const override;
  int open(inode_view entry) const override;
  int open(inode_view entry, std::error_code& ec) const override;
  std::string read_string(uint32_t inode) const override;
  std::string read_string(uint32_t inode, std::error_code& ec) const override;
  std::string
  read_string(uint32_t inode, size_t size, file_off_t offset) const override;
  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          std::error_code& ec) const override;
  size_t read(uint32_t inode, char* buf, size_t size,
              file_off_t offset) const override;
  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf,
               std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset) const override;
  size_t
  readv(uint32_t inode, iovec_read_buf& buf, size_t size, file_off_t offset,
        size_t maxiov, std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov) const override;
  std::vector<std::future<block_range>> readv(uint32_t inode) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, std::error_code& ec) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        std::error_code& ec) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        size_t maxiov) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        std::error_code& ec) const override;
  std::optional<std::span<uint8_t const>> header() const override;
  void set_num_workers(size_t num) override { ir_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    ir_.set_cache_tidy_config(cfg);
  }
  size_t num_blocks() const override { return ir_.num_blocks(); }
  bool has_symlinks() const override { return meta_.has_symlinks(); }
  history const& get_history() const override { return history_; }
  nlohmann::json get_inode_info(inode_view entry) const override {
    return meta_.get_inode_info(entry);
  }
  std::vector<std::string> get_all_block_categories() const override {
    return meta_.get_all_block_categories();
  }
  std::vector<file_stat::uid_type> get_all_uids() const override {
    return meta_.get_all_uids();
  }
  std::vector<file_stat::gid_type> get_all_gids() const override {
    return meta_.get_all_gids();
  }
  std::shared_ptr<filesystem_parser> get_parser() const override {
    return std::make_unique<filesystem_parser>(mm_, image_offset_);
  }
  std::optional<std::string>
  get_block_category(size_t block_no) const override {
    return meta_.get_block_category(block_no);
  }

 private:
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
  std::shared_ptr<mmif> mm_;
  metadata_v2 meta_;
  inode_reader_v2 ir_;
  mutable std::mutex mx_;
  std::vector<uint8_t> meta_buffer_;
  std::optional<std::span<uint8_t const>> header_;
  mutable block_access_level fsinfo_block_access_level_{
      block_access_level::no_access};
  mutable std::unique_ptr<filesystem_info const> fsinfo_;
  history history_;
  file_off_t const image_offset_;
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
    filesystem_parser parser(mm_, image_offset_);
    filesystem_info info;

    parser.rewind();

    while (auto s = parser.next_section()) {
      check_section(*s);

      if (s->type() == section_type::BLOCK) {
        ++info.block_count;
        info.compressed_block_size += s->length();
        info.compressed_block_sizes.push_back(s->length());
        if (opts.block_access >= block_access_level::unrestricted) {
          try {
            auto uncompressed_size = get_uncompressed_section_size(mm_, *s);
            info.uncompressed_block_size += uncompressed_size;
            info.uncompressed_block_sizes.push_back(uncompressed_size);
          } catch (std::exception const&) {
            info.uncompressed_block_size += s->length();
            info.uncompressed_block_size_is_estimate = true;
            info.uncompressed_block_sizes.push_back(std::nullopt);
          }
        } else {
          info.uncompressed_block_size += s->length();
          info.uncompressed_block_size_is_estimate = true;
          info.uncompressed_block_sizes.push_back(std::nullopt);
        }
      } else if (s->type() == section_type::METADATA_V2) {
        info.compressed_metadata_size += s->length();
        try {
          info.uncompressed_metadata_size +=
              get_uncompressed_section_size(mm_, *s);
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
    logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
    const filesystem_options& options,
    std::shared_ptr<performance_monitor const> perfmon)
    : LOG_PROXY_INIT(lgr)
    , os_{os}
    , mm_{std::move(mm)}
    , history_({.with_timestamps = true})
    , image_offset_{filesystem_parser::find_image_offset(
          *mm_, options.image_offset)} // clang-format off
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
  filesystem_parser parser(mm_, image_offset_);

  if (parser.has_index()) {
    LOG_DEBUG << "found valid section index";
  }

  header_ = parser.header();

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

      if (!s->check_fast(*mm_)) {
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

  std::vector<uint8_t> schema_buffer;

  meta_ = make_metadata(lgr, mm_, sections, schema_buffer, meta_buffer_,
                        options.metadata, options.inode_offset, false,
                        options.lock_mode, !parser.has_checksums(), perfmon);

  LOG_DEBUG << "read " << cache.block_count() << " blocks and " << meta_.size()
            << " bytes of metadata";

  cache.set_block_size(meta_.block_size());

  ir_ = inode_reader_v2(lgr, std::move(cache), options.inode_reader, perfmon);

  if (auto it = sections.find(section_type::HISTORY); it != sections.end()) {
    for (auto& section : it->second) {
      if (section.check_fast(*mm_)) {
        std::vector<uint8_t> buffer;
        history_.parse_append(get_section_data(mm_, section, buffer, false));
      }
    }
  }
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::check(filesystem_check_level level,
                                     size_t num_threads) const {
  filesystem_parser parser(mm_, image_offset_);

  worker_group wg(LOG_GET_LOGGER, os_, "fscheck", num_threads);
  std::vector<std::future<fs_section>> sections;

  while (auto sp = parser.next_section()) {
    check_section(*sp);

    std::packaged_task<fs_section()> task{[this, level, s = std::move(*sp)] {
      if (level == filesystem_check_level::INTEGRITY ||
          level == filesystem_check_level::FULL) {
        if (!s.verify(*mm_)) {
          DWARFS_THROW(runtime_error,
                       "integrity check error in section: " + s.name());
        }
      } else {
        if (!s.check_fast(*mm_)) {
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
                                     fsinfo_options const& opts) const {
  filesystem_parser parser(mm_, image_offset_);

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

      std::string block_size;

      if (auto uncompressed_size = try_get_uncompressed_section_size(mm_, s)) {
        float compression_ratio = float(s.length()) / uncompressed_size.value();
        block_size =
            fmt::format("blocksize={}, ratio={:.2f}%",
                        uncompressed_size.value(), 100.0 * compression_ratio);
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

      os << "SECTION " << s.description() << ", " << block_size << category
         << "\n";
    }
  }

  if (opts.features.has(fsinfo_feature::history)) {
    history_.dump(os);
  }

  meta_.dump(
      os, opts, get_info(opts), [&](const std::string& indent, uint32_t inode) {
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
std::string filesystem_<LoggerPolicy>::dump(fsinfo_options const& opts) const {
  std::ostringstream oss;
  dump(oss, opts);
  return oss.str();
}

template <typename LoggerPolicy>
nlohmann::json
filesystem_<LoggerPolicy>::info_as_json(fsinfo_options const& opts) const {
  filesystem_parser parser(mm_, image_offset_);

  nlohmann::json info{
      {"version",
       {
           {"major", parser.major_version()},
           {"minor", parser.minor_version()},
           {"header", parser.header_version()},
       }},
      {"image_offset", parser.image_offset()},
  };

  if (opts.features.has(fsinfo_feature::history)) {
    info["history"] = history_.as_json();
  }

  if (opts.features.has(fsinfo_feature::section_details)) {
    size_t block_no{0};

    while (auto sp = parser.next_section()) {
      auto const& s = *sp;

      bool checksum_ok = s.check_fast(*mm_);

      nlohmann::json section_info{
          {"type", s.name()},
          {"compressed_size", s.length()},
          {"checksum_ok", checksum_ok},
      };

      if (auto uncompressed_size = try_get_uncompressed_section_size(mm_, s)) {
        section_info["size"] = uncompressed_size.value();
        section_info["ratio"] = float(s.length()) / uncompressed_size.value();
      }

      if (s.type() == section_type::BLOCK) {
        if (auto catstr = meta_.get_block_category(block_no)) {
          section_info["category"] = catstr.value();
        }
        ++block_no;
      }

      info["sections"].push_back(std::move(section_info));
    }
  }

  info.update(meta_.info_as_json(opts, get_info(opts)));

  return info;
}

template <typename LoggerPolicy>
nlohmann::json filesystem_<LoggerPolicy>::metadata_as_json() const {
  return meta_.as_json();
}

template <typename LoggerPolicy>
std::string
filesystem_<LoggerPolicy>::serialize_metadata_as_json(bool simple) const {
  return meta_.serialize_as_json(simple);
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
std::optional<inode_view>
filesystem_<LoggerPolicy>::find(const char* path) const {
  PERFMON_CLS_SCOPED_SECTION(find_path)
  return meta_.find(path);
}

template <typename LoggerPolicy>
std::optional<inode_view> filesystem_<LoggerPolicy>::find(int inode) const {
  PERFMON_CLS_SCOPED_SECTION(find_inode)
  return meta_.find(inode);
}

template <typename LoggerPolicy>
std::optional<inode_view>
filesystem_<LoggerPolicy>::find(int inode, const char* name) const {
  PERFMON_CLS_SCOPED_SECTION(find_inode_name)
  return meta_.find(inode, name);
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                             std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_ec)
  return meta_.getattr(entry, ec);
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(getattr)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.getattr(entry, ec); });
}

template <typename LoggerPolicy>
file_stat filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                             getattr_options const& opts,
                                             std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_opts_ec)
  return meta_.getattr(entry, opts, ec);
}

template <typename LoggerPolicy>
file_stat
filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                   getattr_options const& opts) const {
  PERFMON_CLS_SCOPED_SECTION(getattr_opts)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.getattr(entry, opts, ec); });
}

template <typename LoggerPolicy>
bool filesystem_<LoggerPolicy>::access(inode_view entry, int mode,
                                       file_stat::uid_type uid,
                                       file_stat::gid_type gid) const {
  PERFMON_CLS_SCOPED_SECTION(access)
  std::error_code ec;
  meta_.access(entry, mode, uid, gid, ec);
  return !ec;
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::access(inode_view entry, int mode,
                                       file_stat::uid_type uid,
                                       file_stat::gid_type gid,
                                       std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(access_ec)
  meta_.access(entry, mode, uid, gid, ec);
}

template <typename LoggerPolicy>
std::optional<directory_view>
filesystem_<LoggerPolicy>::opendir(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(opendir)
  return meta_.opendir(entry);
}

template <typename LoggerPolicy>
std::optional<std::pair<inode_view, std::string>>
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
  return meta_.readlink(entry, mode, ec);
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::readlink(inode_view entry,
                                                readlink_mode mode) const {
  PERFMON_CLS_SCOPED_SECTION(readlink)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.readlink(entry, mode, ec); });
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
  return meta_.open(entry, ec);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(open)
  return call_ec_throw(
      [&](std::error_code& ec) { return meta_.open(entry, ec); });
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
std::optional<std::span<uint8_t const>>
filesystem_<LoggerPolicy>::header() const {
  return header_;
}

} // namespace internal

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             std::filesystem::path const& path)
    : filesystem_v2(lgr, os, os.map_file(os.canonical(path))) {}

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             std::filesystem::path const& path,
                             filesystem_options const& options,
                             std::shared_ptr<performance_monitor const> perfmon)
    : filesystem_v2(lgr, os, os.map_file(os.canonical(path)), options,
                    std::move(perfmon)) {}

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             std::shared_ptr<mmif> mm)
    : filesystem_v2(lgr, os, std::move(mm), filesystem_options()) {}

filesystem_v2::filesystem_v2(logger& lgr, os_access const& os,
                             std::shared_ptr<mmif> mm,
                             const filesystem_options& options,
                             std::shared_ptr<performance_monitor const> perfmon)
    : impl_(make_unique_logging_object<filesystem_v2::impl,
                                       internal::filesystem_, logger_policies>(
          lgr, os, std::move(mm), options, std::move(perfmon))) {}

int filesystem_v2::identify(logger& lgr, os_access const& os,
                            std::shared_ptr<mmif> mm, std::ostream& output,
                            int detail_level, size_t num_readers,
                            bool check_integrity, file_off_t image_offset) {
  filesystem_options fsopts;
  fsopts.metadata.enable_nlink = true;
  fsopts.image_offset = image_offset;
  filesystem_v2 fs(lgr, os, mm, fsopts);

  auto errors = fs.check(check_integrity ? filesystem_check_level::FULL
                                         : filesystem_check_level::CHECKSUM,
                         num_readers);

  fs.dump(output, {.features = fsinfo_features::for_level(detail_level)});

  return errors;
}

std::optional<std::span<uint8_t const>>
filesystem_v2::header(std::shared_ptr<mmif> mm) {
  return header(std::move(mm), filesystem_options::IMAGE_OFFSET_AUTO);
}

std::optional<std::span<uint8_t const>>
filesystem_v2::header(std::shared_ptr<mmif> mm, file_off_t image_offset) {
  return internal::filesystem_parser(mm, image_offset).header();
}

} // namespace dwarfs::reader

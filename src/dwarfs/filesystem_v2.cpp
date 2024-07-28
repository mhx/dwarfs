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
#include <dwarfs/categorizer.h>
#include <dwarfs/category_resolver.h>
#include <dwarfs/error.h>
#include <dwarfs/filesystem_v2.h>
#include <dwarfs/filesystem_writer.h>
#include <dwarfs/fs_section.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/mmif.h>
#include <dwarfs/options.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/progress.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/block_cache.h>
#include <dwarfs/internal/block_data.h>
#include <dwarfs/internal/inode_reader_v2.h>
#include <dwarfs/internal/metadata_v2.h>
#include <dwarfs/internal/worker_group.h>

namespace dwarfs {

namespace internal {

namespace {

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

class filesystem_parser {
 private:
  static uint64_t constexpr section_offset_mask{(UINT64_C(1) << 48) - 1};

 public:
  static file_off_t find_image_offset(mmif& mm, file_off_t image_offset) {
    if (image_offset != filesystem_options::IMAGE_OFFSET_AUTO) {
      return image_offset;
    }

    static constexpr std::array<char, 7> magic{
        {'D', 'W', 'A', 'R', 'F', 'S', MAJOR_VERSION}};

    file_off_t start = 0;
    for (;;) {
      if (start + magic.size() >= mm.size()) {
        break;
      }

      auto ss = mm.span<char>(start);
#if __cpp_lib_boyer_moore_searcher >= 201603
      auto searcher = std::boyer_moore_searcher(magic.begin(), magic.end());
#else
      auto searcher = std::default_searcher(magic.begin(), magic.end());
#endif
      auto it = std::search(ss.begin(), ss.end(), searcher);

      if (it == ss.end()) {
        break;
      }

      file_off_t pos = start + std::distance(ss.begin(), it);

      if (pos + sizeof(file_header) >= mm.size()) {
        break;
      }

      auto fh = mm.as<file_header>(pos);

      if (fh->minor < 2) {
        // best we can do for older file systems
        return pos;
      }

      // do a little more validation before we return
      if (pos + sizeof(section_header_v2) >= mm.size()) {
        break;
      }

      auto sh = mm.as<section_header_v2>(pos);

      if (sh->number == 0) {
        auto endpos = pos + sh->length + 2 * sizeof(section_header_v2);

        if (endpos < sh->length) {
          // overflow
          break;
        }

        if (endpos >= mm.size()) {
          break;
        }

        auto ps = mm.as<void>(pos + sh->length + sizeof(section_header_v2));

        if (::memcmp(ps, magic.data(), magic.size()) == 0 and
            reinterpret_cast<section_header_v2 const*>(ps)->number == 1) {
          return pos;
        }
      }

      start = pos + magic.size();
    }

    DWARFS_THROW(runtime_error, "no filesystem found");
  }

  explicit filesystem_parser(std::shared_ptr<mmif> mm,
                             file_off_t image_offset = 0)
      : mm_{std::move(mm)}
      , image_offset_{find_image_offset(*mm_, image_offset)} {
    if (mm_->size() < image_offset_ + sizeof(file_header)) {
      DWARFS_THROW(runtime_error, "file too small");
    }

    auto fh = mm_->as<file_header>(image_offset_);

    if (::memcmp(&fh->magic[0], "DWARFS", 6) != 0) {
      DWARFS_THROW(runtime_error, "magic not found");
    }

    if (fh->major != MAJOR_VERSION) {
      DWARFS_THROW(runtime_error, "different major version");
    }

    if (fh->minor > MINOR_VERSION) {
      DWARFS_THROW(runtime_error, "newer minor version");
    }

    version_ = fh->minor >= 2 ? 2 : 1;
    major_ = fh->major;
    minor_ = fh->minor;

    if (minor_ >= 4) {
      find_index();
    }

    rewind();
  }

  std::optional<fs_section> next_section() {
    if (index_.empty()) {
      if (offset_ < static_cast<file_off_t>(mm_->size())) {
        auto section = fs_section(*mm_, offset_, version_);
        offset_ = section.end();
        return section;
      }
    } else {
      if (offset_ < static_cast<file_off_t>(index_.size())) {
        uint64_t id = index_[offset_++];
        uint64_t offset = id & section_offset_mask;
        uint64_t next_offset = offset_ < static_cast<file_off_t>(index_.size())
                                   ? index_[offset_] & section_offset_mask
                                   : mm_->size() - image_offset_;
        return fs_section(mm_, static_cast<section_type>(id >> 48),
                          image_offset_ + offset, next_offset - offset,
                          version_);
      }
    }

    return std::nullopt;
  }

  std::optional<std::span<uint8_t const>> header() const {
    if (image_offset_ == 0) {
      return std::nullopt;
    }
    return mm_->span(0, image_offset_);
  }

  void rewind() {
    if (index_.empty()) {
      offset_ = image_offset_;
      if (version_ == 1) {
        offset_ += sizeof(file_header);
      }
    } else {
      offset_ = 0;
    }
  }

  std::string version() const {
    return fmt::format("{0}.{1} [{2}]", major_, minor_, version_);
  }

  int major_version() const { return major_; }
  int minor_version() const { return minor_; }
  int header_version() const { return version_; }

  file_off_t image_offset() const { return image_offset_; }

  bool has_checksums() const { return version_ >= 2; }

  bool has_index() const { return !index_.empty(); }

 private:
  void find_index() {
    uint64_t index_pos;

    ::memcpy(&index_pos, mm_->as<void>(mm_->size() - sizeof(uint64_t)),
             sizeof(uint64_t));

    if ((index_pos >> 48) ==
        static_cast<uint16_t>(section_type::SECTION_INDEX)) {
      index_pos &= section_offset_mask;
      index_pos += image_offset_;

      if (index_pos < mm_->size()) {
        auto section = fs_section(*mm_, index_pos, version_);

        if (section.check_fast(*mm_)) {
          index_.resize(section.length() / sizeof(uint64_t));
          ::memcpy(index_.data(), section.data(*mm_).data(), section.length());
        }
      }
    }
  }

  std::shared_ptr<mmif> mm_;
  file_off_t const image_offset_;
  file_off_t offset_{0};
  int version_{0};
  uint8_t major_{0};
  uint8_t minor_{0};
  std::vector<uint64_t> index_;
};

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
  void dump(std::ostream& os, int detail_level) const override;
  std::string dump(int detail_level) const override;
  nlohmann::json info_as_json(int detail_level) const override;
  nlohmann::json metadata_as_json() const override;
  std::string serialize_metadata_as_json(bool simple) const override;
  void walk(std::function<void(dir_entry_view)> const& func) const override;
  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override;
  std::optional<inode_view> find(const char* path) const override;
  std::optional<inode_view> find(int inode) const override;
  std::optional<inode_view> find(int inode, const char* name) const override;
  file_stat getattr(inode_view entry, std::error_code& ec) const override;
  file_stat getattr(inode_view entry) const override;
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
  int statvfs(vfs_stat* stbuf) const override;
  int open(inode_view entry) const override;
  size_t read(uint32_t inode, char* buf, size_t size,
              file_off_t offset) const override;
  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const override;
  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset) const override;
  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
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
  void rewrite(progress& prog, filesystem_writer& writer,
               category_resolver const& cat_resolver,
               rewrite_options const& opts) const override;

 private:
  filesystem_info const& get_info() const;
  void check_section(fs_section const& section) const;
  std::vector<std::future<block_range>>
  readv_ec(uint32_t inode, size_t size, file_off_t offset,
           std::error_code& ec) const;
  size_t readv_ec(uint32_t inode, iovec_read_buf& buf, size_t size,
                  file_off_t offset, std::error_code& ec) const;

  LOG_PROXY_DECL(LoggerPolicy);
  os_access const& os_;
  std::shared_ptr<mmif> mm_;
  metadata_v2 meta_;
  inode_reader_v2 ir_;
  mutable std::mutex mx_;
  std::vector<uint8_t> meta_buffer_;
  std::optional<std::span<uint8_t const>> header_;
  mutable std::unique_ptr<filesystem_info const> fsinfo_;
  history history_;
  file_off_t const image_offset_;
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(find_path)
  PERFMON_CLS_TIMER_DECL(find_inode)
  PERFMON_CLS_TIMER_DECL(find_inode_name)
  PERFMON_CLS_TIMER_DECL(getattr)
  PERFMON_CLS_TIMER_DECL(getattr_ec)
  PERFMON_CLS_TIMER_DECL(access)
  PERFMON_CLS_TIMER_DECL(access_ec)
  PERFMON_CLS_TIMER_DECL(opendir)
  PERFMON_CLS_TIMER_DECL(readdir)
  PERFMON_CLS_TIMER_DECL(dirsize)
  PERFMON_CLS_TIMER_DECL(readlink)
  PERFMON_CLS_TIMER_DECL(readlink_ec)
  PERFMON_CLS_TIMER_DECL(statvfs)
  PERFMON_CLS_TIMER_DECL(open)
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
filesystem_info const& filesystem_<LoggerPolicy>::get_info() const {
  std::lock_guard lock(mx_);

  if (!fsinfo_) {
    filesystem_parser parser(mm_, image_offset_);
    filesystem_info info;

    parser.rewind();

    while (auto s = parser.next_section()) {
      check_section(*s);

      if (s->type() == section_type::BLOCK) {
        ++info.block_count;
        info.compressed_block_size += s->length();
        info.compressed_block_sizes.push_back(s->length());
        try {
          auto uncompressed_size = get_uncompressed_section_size(mm_, *s);
          info.uncompressed_block_size += uncompressed_size;
          info.uncompressed_block_sizes.push_back(uncompressed_size);
        } catch (std::exception const&) {
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
  }

  return *fsinfo_;
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
    PERFMON_CLS_TIMER_INIT(access)
    PERFMON_CLS_TIMER_INIT(access_ec)
    PERFMON_CLS_TIMER_INIT(opendir)
    PERFMON_CLS_TIMER_INIT(readdir)
    PERFMON_CLS_TIMER_INIT(dirsize)
    PERFMON_CLS_TIMER_INIT(readlink)
    PERFMON_CLS_TIMER_INIT(readlink_ec)
    PERFMON_CLS_TIMER_INIT(statvfs)
    PERFMON_CLS_TIMER_INIT(open)
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
void filesystem_<LoggerPolicy>::rewrite(progress& prog,
                                        filesystem_writer& writer,
                                        category_resolver const& cat_resolver,
                                        rewrite_options const& opts) const {
  filesystem_parser parser(mm_, image_offset_);

  if (opts.recompress_block) {
    size_t block_no{0};
    parser.rewind();

    while (auto s = parser.next_section()) {
      if (s->type() == section_type::BLOCK) {
        if (auto catstr = meta_.get_block_category(block_no)) {
          if (auto cat = cat_resolver.category_value(catstr.value())) {
            writer.check_block_compression(s->compression(), s->data(*mm_),
                                           cat);
          }
        }
        ++block_no;
      }
    }
  }

  prog.original_size = mm_->size();
  prog.filesystem_size = mm_->size();
  prog.block_count = num_blocks();

  if (header_) {
    writer.copy_header(*header_);
  }

  size_t block_no{0};

  auto log_rewrite =
      [&](bool compressing, const auto& s,
          std::optional<fragment_category::value_type> const& cat) {
        auto prefix = compressing ? "recompressing" : "copying";
        std::string catinfo;
        std::string compinfo;
        if (cat) {
          catinfo = fmt::format(", {}", cat_resolver.category_name(*cat));
        }
        if (compressing) {
          compinfo = fmt::format(
              " using '{}'", writer.get_compressor(s->type(), cat).describe());
        }
        LOG_VERBOSE << prefix << " " << size_with_unit(s->length()) << " "
                    << get_section_name(s->type()) << " ("
                    << get_compression_name(s->compression()) << catinfo << ")"
                    << compinfo;
      };

  auto log_recompress =
      [&](const auto& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) { log_rewrite(true, s, cat); };

  auto copy_compressed =
      [&](const auto& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) {
        log_rewrite(false, s, cat);
        writer.write_compressed_section(*s, s->data(*mm_));
      };

  auto from_none_to_none =
      [&](auto const& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) {
        if (s->compression() == compression_type::NONE) {
          auto& bc = writer.get_compressor(s->type(), cat);
          if (bc.type() == compression_type::NONE) {
            return true;
          }
        }
        return false;
      };

  parser.rewind();

  while (auto s = parser.next_section()) {
    switch (s->type()) {
    case section_type::BLOCK: {
      std::optional<fragment_category::value_type> cat;
      bool recompress_block{true};

      if (opts.recompress_block) {
        auto catstr = meta_.get_block_category(block_no);

        if (catstr) {
          cat = cat_resolver.category_value(catstr.value());

          if (!cat) {
            LOG_ERROR << "unknown category '" << catstr.value()
                      << "' for block " << block_no;
          }

          if (!opts.recompress_categories.empty()) {
            bool is_in_set{opts.recompress_categories.count(catstr.value()) >
                           0};

            recompress_block =
                opts.recompress_categories_exclude ? !is_in_set : is_in_set;
          }
        }
      }

      if (recompress_block && from_none_to_none(s, cat)) {
        recompress_block = false;
      }

      if (recompress_block) {
        log_recompress(s, cat);

        writer.write_section(section_type::BLOCK, s->compression(),
                             s->data(*mm_), cat);
      } else {
        copy_compressed(s, cat);
      }

      ++block_no;
    } break;

    case section_type::METADATA_V2_SCHEMA:
    case section_type::METADATA_V2:
      if (opts.recompress_metadata && !from_none_to_none(s)) {
        log_recompress(s);
        writer.write_section(s->type(), s->compression(), s->data(*mm_));
      } else {
        copy_compressed(s);
      }
      break;

    case section_type::HISTORY:
      if (opts.enable_history) {
        history hist{opts.history};
        hist.parse(history_.serialize());
        hist.append(opts.command_line_arguments);

        LOG_VERBOSE << "updating " << get_section_name(s->type()) << " ("
                    << get_compression_name(s->compression())
                    << "), compressing using '"
                    << writer.get_compressor(s->type()).describe() << "'";

        writer.write_history(std::make_shared<block_data>(hist.serialize()));
      } else {
        LOG_VERBOSE << "removing " << get_section_name(s->type());
      }
      break;

    case section_type::SECTION_INDEX:
      // this will be automatically added by the filesystem_writer
      break;

    default:
      // verbatim copy everything else
      copy_compressed(s);
      break;
    }
  }

  writer.flush();
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::check(filesystem_check_level level,
                                     size_t num_threads) const {
  filesystem_parser parser(mm_, image_offset_);

  worker_group wg(LOG_GET_LOGGER, os_, "fscheck", num_threads);
  std::vector<std::future<fs_section>> sections;

  while (auto sp = parser.next_section()) {
    check_section(*sp);

    std::packaged_task<fs_section()> task{[this, level, s = *sp] {
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
void filesystem_<LoggerPolicy>::dump(std::ostream& os, int detail_level) const {
  filesystem_parser parser(mm_, image_offset_);

  if (detail_level > 0) {
    os << "DwarFS version " << parser.version();
    if (auto off = parser.image_offset(); off > 0) {
      os << " at offset " << off;
    }
    os << "\n";
  }

  size_t block_no{0};

  if (detail_level > 2) {
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

  if (detail_level > 1) {
    history_.dump(os);
  }

  meta_.dump(os, detail_level, get_info(),
             [&](const std::string& indent, uint32_t inode) {
               if (auto chunks = meta_.get_chunks(inode)) {
                 os << indent << chunks->size() << " chunks in inode " << inode
                    << "\n";
                 ir_.dump(os, indent + "  ", *chunks);
               } else {
                 LOG_ERROR << "error reading chunks for inode " << inode;
               }
             });
}

template <typename LoggerPolicy>
std::string filesystem_<LoggerPolicy>::dump(int detail_level) const {
  std::ostringstream oss;
  dump(oss, detail_level);
  return oss.str();
}

template <typename LoggerPolicy>
nlohmann::json filesystem_<LoggerPolicy>::info_as_json(int detail_level) const {
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

  if (detail_level > 1) {
    info["history"] = history_.as_json();
  }

  if (detail_level > 2) {
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

  info.update(meta_.info_as_json(detail_level, get_info()));

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
int filesystem_<LoggerPolicy>::statvfs(vfs_stat* stbuf) const {
  PERFMON_CLS_SCOPED_SECTION(statvfs)
  // TODO: not sure if that's the right abstraction...
  return meta_.statvfs(stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(inode_view entry) const {
  PERFMON_CLS_SCOPED_SECTION(open)
  return meta_.open(entry);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                       file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(read)
  if (auto chunks = meta_.get_chunks(inode)) {
    return call_ec_throw([&](std::error_code& ec) {
      return ir_.read(buf, inode, size, offset, *chunks, ec);
    });
  }
  throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
}

template <typename LoggerPolicy>
size_t
filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                file_off_t offset, std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(read_ec)
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.read(buf, inode, size, offset, *chunks, ec);
  }
  ec = std::make_error_code(std::errc::bad_file_descriptor);
  return 0;
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv_ec(uint32_t inode, iovec_read_buf& buf,
                                           size_t size, file_off_t offset,
                                           std::error_code& ec) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.readv(buf, inode, size, offset, *chunks, ec);
  }
  ec = std::make_error_code(std::errc::bad_file_descriptor);
  return 0;
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        size_t size, file_off_t offset,
                                        std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec_ec)
  return readv_ec(inode, buf, size, offset, ec);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                        size_t size, file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readv_iovec)
  return call_ec_throw([&](std::error_code& ec) {
    return readv_ec(inode, buf, size, offset, ec);
  });
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv_ec(uint32_t inode, size_t size,
                                    file_off_t offset,
                                    std::error_code& ec) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.readv(inode, size, offset, *chunks, ec);
  }
  ec = std::make_error_code(std::errc::bad_file_descriptor);
  return {};
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size, file_off_t offset,
                                 std::error_code& ec) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future_ec)
  return readv_ec(inode, size, offset, ec);
}

template <typename LoggerPolicy>
std::vector<std::future<block_range>>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size,
                                 file_off_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readv_future)
  return call_ec_throw(
      [&](std::error_code& ec) { return readv_ec(inode, size, offset, ec); });
}

template <typename LoggerPolicy>
std::optional<std::span<uint8_t const>>
filesystem_<LoggerPolicy>::header() const {
  return header_;
}

} // namespace internal

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

  fs.dump(output, detail_level);

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

} // namespace dwarfs

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

#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <sys/mman.h>
#include <sys/statvfs.h>

#include <boost/system/system_error.hpp>

#include <folly/Range.h>

#include <fmt/format.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/block_compressor.h"
#include "dwarfs/block_data.h"
#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fs_section.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/inode_reader_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/mmif.h"
#include "dwarfs/options.h"
#include "dwarfs/progress.h"

namespace dwarfs {

namespace {

class filesystem_parser {
 public:
  filesystem_parser(std::shared_ptr<mmif> mm)
      : mm_(mm) {
    if (mm_->size() < sizeof(file_header)) {
      DWARFS_THROW(runtime_error, "file too small");
    }

    auto fh = mm_->as<file_header>();

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

    rewind();
  }

  std::optional<fs_section> next_section() {
    if (offset_ < mm_->size()) {
      auto section = fs_section(*mm_, offset_, version_);
      offset_ = section.end();
      return section;
    }

    return std::nullopt;
  }

  void rewind() { offset_ = version_ == 1 ? sizeof(file_header) : 0; }

  std::string version() const {
    return fmt::format("{0}.{1} [{2}]", major_, minor_, version_);
  }

 private:
  std::shared_ptr<mmif> mm_;
  size_t offset_{0};
  int version_{0};
  uint8_t major_{0};
  uint8_t minor_{0};
};

using section_map = std::unordered_map<section_type, fs_section>;

folly::ByteRange
get_section_data(std::shared_ptr<mmif> mm, fs_section const& section,
                 std::vector<uint8_t>& buffer, bool force_buffer) {
  auto compression = section.compression();
  auto start = section.start();
  auto length = section.length();

  if (!force_buffer && compression == compression_type::NONE) {
    return mm->range(start, length);
  }

  buffer = block_decompressor::decompress(compression, mm->as<uint8_t>(start),
                                          length);

  return buffer;
}

metadata_v2
make_metadata(logger& lgr, std::shared_ptr<mmif> mm,
              section_map const& sections, std::vector<uint8_t>& schema_buffer,
              std::vector<uint8_t>& meta_buffer,
              const metadata_options& options,
              const struct ::stat* stat_defaults = nullptr,
              int inode_offset = 0, bool force_buffers = false,
              mlock_mode lock_mode = mlock_mode::NONE) {
  LOG_PROXY(debug_logger_policy, lgr);
  auto schema_it = sections.find(section_type::METADATA_V2_SCHEMA);
  auto meta_it = sections.find(section_type::METADATA_V2);

  if (schema_it == sections.end()) {
    DWARFS_THROW(runtime_error, "no metadata schema found");
  }

  if (meta_it == sections.end()) {
    DWARFS_THROW(runtime_error, "no metadata found");
  }

  auto& meta_section = meta_it->second;

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

  return metadata_v2(
      lgr,
      get_section_data(mm, schema_it->second, schema_buffer, force_buffers),
      meta_section_range, options, stat_defaults, inode_offset);
}

template <typename LoggerPolicy>
class filesystem_ final : public filesystem_v2::impl {
 public:
  filesystem_(logger& lgr_, std::shared_ptr<mmif> mm,
              const filesystem_options& options,
              const struct ::stat* stat_defaults, int inode_offset);

  void dump(std::ostream& os, int detail_level) const override;
  folly::dynamic metadata_as_dynamic() const override;
  std::string serialize_metadata_as_json(bool simple) const override;
  void walk(std::function<void(entry_view)> const& func) const override;
  void walk(std::function<void(entry_view, directory_view)> const& func)
      const override;
  void
  walk_inode_order(std::function<void(entry_view)> const& func) const override;
  void
  walk_inode_order(std::function<void(entry_view, directory_view)> const& func)
      const override;
  std::optional<entry_view> find(const char* path) const override;
  std::optional<entry_view> find(int inode) const override;
  std::optional<entry_view> find(int inode, const char* name) const override;
  int getattr(entry_view entry, struct ::stat* stbuf) const override;
  int access(entry_view entry, int mode, uid_t uid, gid_t gid) const override;
  std::optional<directory_view> opendir(entry_view entry) const override;
  std::optional<std::pair<entry_view, std::string_view>>
  readdir(directory_view dir, size_t offset) const override;
  size_t dirsize(directory_view dir) const override;
  int readlink(entry_view entry, std::string* buf) const override;
  folly::Expected<std::string_view, int>
  readlink(entry_view entry) const override;
  int statvfs(struct ::statvfs* stbuf) const override;
  int open(entry_view entry) const override;
  ssize_t
  read(uint32_t inode, char* buf, size_t size, off_t offset) const override;
  ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                off_t offset) const override;
  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(uint32_t inode, size_t size, off_t offset) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  std::shared_ptr<mmif> mm_;
  metadata_v2 meta_;
  inode_reader_v2 ir_;
  std::vector<uint8_t> meta_buffer_;
};

template <typename LoggerPolicy>
filesystem_<LoggerPolicy>::filesystem_(logger& lgr, std::shared_ptr<mmif> mm,
                                       const filesystem_options& options,
                                       const struct ::stat* stat_defaults,
                                       int inode_offset)
    : LOG_PROXY_INIT(lgr)
    , mm_(std::move(mm)) {
  filesystem_parser parser(mm_);
  block_cache cache(lgr, mm_, options.block_cache);

  section_map sections;

  while (auto s = parser.next_section()) {
    LOG_DEBUG << "section " << s->description() << " @ " << s->start() << " ["
              << s->length() << " bytes]";
    if (s->type() == section_type::BLOCK) {
      cache.insert(*s);
    } else {
      if (!s->check_fast(*mm_)) {
        DWARFS_THROW(runtime_error, "checksum error in section: " + s->name());
      }

      if (!sections.emplace(s->type(), *s).second) {
        DWARFS_THROW(runtime_error, "duplicate section: " + s->name());
      }
    }
  }

  std::vector<uint8_t> schema_buffer;

  meta_ = make_metadata(lgr, mm_, sections, schema_buffer, meta_buffer_,
                        options.metadata, stat_defaults, inode_offset, false,
                        options.lock_mode);

  LOG_DEBUG << "read " << cache.block_count() << " blocks and " << meta_.size()
            << " bytes of metadata";

  cache.set_block_size(meta_.block_size());

  ir_ = inode_reader_v2(lgr, std::move(cache));
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::dump(std::ostream& os, int detail_level) const {
  meta_.dump(os, detail_level, [&](const std::string& indent, uint32_t inode) {
    if (auto chunks = meta_.get_chunks(inode)) {
      os << indent << chunks->size() << " chunks in inode " << inode << "\n";
      ir_.dump(os, indent + "  ", *chunks);
    } else {
      LOG_ERROR << "error reading chunks for inode " << inode;
    }
  });
}

template <typename LoggerPolicy>
folly::dynamic filesystem_<LoggerPolicy>::metadata_as_dynamic() const {
  return meta_.as_dynamic();
}

template <typename LoggerPolicy>
std::string
filesystem_<LoggerPolicy>::serialize_metadata_as_json(bool simple) const {
  return meta_.serialize_as_json(simple);
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  meta_.walk(func);
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk(
    std::function<void(entry_view, directory_view)> const& func) const {
  meta_.walk(func);
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk_inode_order(
    std::function<void(entry_view)> const& func) const {
  meta_.walk_inode_order(func);
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk_inode_order(
    std::function<void(entry_view, directory_view)> const& func) const {
  meta_.walk_inode_order(func);
}

template <typename LoggerPolicy>
std::optional<entry_view>
filesystem_<LoggerPolicy>::find(const char* path) const {
  return meta_.find(path);
}

template <typename LoggerPolicy>
std::optional<entry_view> filesystem_<LoggerPolicy>::find(int inode) const {
  return meta_.find(inode);
}

template <typename LoggerPolicy>
std::optional<entry_view>
filesystem_<LoggerPolicy>::find(int inode, const char* name) const {
  return meta_.find(inode, name);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::getattr(entry_view entry,
                                       struct ::stat* stbuf) const {
  return meta_.getattr(entry, stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::access(entry_view entry, int mode, uid_t uid,
                                      gid_t gid) const {
  return meta_.access(entry, mode, uid, gid);
}

template <typename LoggerPolicy>
std::optional<directory_view>
filesystem_<LoggerPolicy>::opendir(entry_view entry) const {
  return meta_.opendir(entry);
}

template <typename LoggerPolicy>
std::optional<std::pair<entry_view, std::string_view>>
filesystem_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
  return meta_.readdir(dir, offset);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::dirsize(directory_view dir) const {
  return meta_.dirsize(dir);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::readlink(entry_view entry,
                                        std::string* buf) const {
  return meta_.readlink(entry, buf);
}

template <typename LoggerPolicy>
folly::Expected<std::string_view, int>
filesystem_<LoggerPolicy>::readlink(entry_view entry) const {
  return meta_.readlink(entry);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  // TODO: not sure if that's the right abstraction...
  return meta_.statvfs(stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(entry_view entry) const {
  return meta_.open(entry);
}

template <typename LoggerPolicy>
ssize_t filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                        off_t offset) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.read(buf, size, offset, *chunks);
  }
  return -EBADF;
}

template <typename LoggerPolicy>
ssize_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                         size_t size, off_t offset) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.readv(buf, size, offset, *chunks);
  }
  return -EBADF;
}

template <typename LoggerPolicy>
folly::Expected<std::vector<std::future<block_range>>, int>
filesystem_<LoggerPolicy>::readv(uint32_t inode, size_t size,
                                 off_t offset) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.readv(size, offset, *chunks);
  }
  return folly::makeUnexpected(-EBADF);
}

} // namespace

filesystem_v2::filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm)
    : filesystem_v2(lgr, std::move(mm), filesystem_options()) {}

filesystem_v2::filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm,
                             const filesystem_options& options,
                             const struct ::stat* stat_defaults,
                             int inode_offset)
    : impl_(make_unique_logging_object<filesystem_v2::impl, filesystem_,
                                       logger_policies>(
          lgr, std::move(mm), options, stat_defaults, inode_offset)) {}

void filesystem_v2::rewrite(logger& lgr, progress& prog,
                            std::shared_ptr<mmif> mm, filesystem_writer& writer,
                            rewrite_options const& opts) {
  // TODO:
  LOG_PROXY(debug_logger_policy, lgr);
  filesystem_parser parser(mm);

  std::vector<section_type> section_types;
  section_map sections;
  size_t total_block_size = 0;

  while (auto s = parser.next_section()) {
    if (!s->check_fast(*mm)) {
      DWARFS_THROW(runtime_error, "checksum error in section: " + s->name());
    }
    if (!s->verify(*mm)) {
      DWARFS_THROW(runtime_error,
                   "integrity check error in section: " + s->name());
    }
    if (s->type() == section_type::BLOCK) {
      ++prog.block_count;
      total_block_size += s->length();
    } else {
      if (!sections.emplace(s->type(), *s).second) {
        DWARFS_THROW(runtime_error, "duplicate section: " + s->name());
      }
      section_types.push_back(s->type());
    }
  }

  std::vector<uint8_t> schema_raw;
  std::vector<uint8_t> meta_raw;

  if (opts.recompress_metadata) {
    auto meta = make_metadata(lgr, mm, sections, schema_raw, meta_raw,
                              metadata_options(), nullptr, 0, true);

    struct ::statvfs stbuf;
    meta.statvfs(&stbuf);
    prog.original_size = stbuf.f_blocks * stbuf.f_frsize;
  } else {
    prog.original_size = total_block_size;
  }

  parser.rewind();

  while (auto s = parser.next_section()) {
    // TODO: multi-thread this?
    if (s->type() == section_type::BLOCK) {
      if (opts.recompress_block) {
        auto block =
            std::make_shared<block_data>(block_decompressor::decompress(
                s->compression(), mm->as<uint8_t>(s->start()), s->length()));
        prog.filesystem_size += block->size();
        writer.write_block(std::move(block));
      } else {
        writer.write_compressed_section(s->type(), s->compression(),
                                        s->data(*mm));
      }
    }
  }

  if (opts.recompress_metadata) {
    writer.write_metadata_v2_schema(
        std::make_shared<block_data>(std::move(schema_raw)));
    writer.write_metadata_v2(std::make_shared<block_data>(std::move(meta_raw)));
  } else {
    for (auto type : section_types) {
      auto& sec = DWARFS_NOTHROW(sections.at(type));
      writer.write_compressed_section(type, sec.compression(), sec.data(*mm));
    }
  }

  writer.flush();
}

void filesystem_v2::identify(logger& lgr, std::shared_ptr<mmif> mm,
                             std::ostream& os, int detail_level) {
  // TODO:
  LOG_PROXY(debug_logger_policy, lgr);
  filesystem_parser parser(mm);

  os << "FILESYSTEM version " << parser.version() << std::endl;

  section_map sections;

  while (auto s = parser.next_section()) {
    std::vector<uint8_t> tmp;
    block_decompressor bd(s->compression(), mm->as<uint8_t>(s->start()),
                          s->length(), tmp);
    float compression_ratio = float(s->length()) / bd.uncompressed_size();

    os << "SECTION " << s->description()
       << ", blocksize=" << bd.uncompressed_size()
       << ", ratio=" << fmt::format("{:.2f}%", 100.0 * compression_ratio)
       << std::endl;

    // TODO: don't throw if we're just checking the file system

    if (!s->check_fast(*mm)) {
      DWARFS_THROW(runtime_error, "checksum error in section: " + s->name());
    }
    if (!s->verify(*mm)) {
      DWARFS_THROW(runtime_error,
                   "integrity check error in section: " + s->name());
    }
    if (s->type() != section_type::BLOCK) {
      if (!sections.emplace(s->type(), *s).second) {
        DWARFS_THROW(runtime_error, "duplicate section: " + s->name());
      }
    }
  }

  if (detail_level > 0) {
    filesystem_v2(lgr, mm).dump(os, detail_level);
  }
}

} // namespace dwarfs

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
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <sys/mman.h>
#include <sys/statvfs.h>

#include <boost/system/system_error.hpp>

#include <folly/Range.h>

#include <fmt/format.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/block_compressor.h"
#include "dwarfs/config.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
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
  struct section {
    size_t start{0};
    section_header header;
  };

  filesystem_parser(std::shared_ptr<mmif> mm)
      : mm_(mm)
      , offset_(sizeof(file_header)) {
    if (mm_->size() < sizeof(file_header)) {
      throw std::runtime_error("file too small");
    }

    const file_header* fh = mm_->as<file_header>();

    if (::memcmp(&fh->magic[0], "DWARFS", 6) != 0) {
      throw std::runtime_error("magic not found");
    }

    if (fh->major != MAJOR_VERSION) {
      throw std::runtime_error("different major version");
    }

    if (fh->minor > MINOR_VERSION) {
      throw std::runtime_error("newer minor version");
    }
  }

  template <typename Logger>
  bool next_section(section_header& sh, size_t& start, Logger& lgr) {
    if (offset_ + sizeof(section_header) <= mm_->size()) {
      ::memcpy(&sh, mm_->as<char>(offset_), sizeof(section_header));

      lgr.trace() << "section_header@" << offset_ << " (" << sh.to_string()
                  << ")";

      offset_ += sizeof(section_header);

      if (offset_ + sh.length > mm_->size()) {
        throw std::runtime_error("truncated file");
      }

      start = offset_;

      offset_ += sh.length;

      return true;
    }

    return false;
  }

  template <typename Logger>
  std::optional<section> next_section(Logger& lgr) {
    section rv;
    if (next_section(rv.header, rv.start, lgr)) {
      return rv;
    }
    return std::nullopt;
  }

  void rewind() { offset_ = sizeof(file_header); }

 private:
  std::shared_ptr<mmif> mm_;
  size_t offset_;
};

using section_map =
    std::unordered_map<section_type, filesystem_parser::section>;

folly::ByteRange
get_section_data(std::shared_ptr<mmif> mm,
                 filesystem_parser::section const& section,
                 std::vector<uint8_t>& buffer, bool force_buffer) {
  if (!force_buffer && section.header.compression == compression_type::NONE) {
    return mm->range(section.start, section.header.length);
  }

  buffer = block_decompressor::decompress(section.header.compression,
                                          mm->as<uint8_t>(section.start),
                                          section.header.length);

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
  log_proxy<debug_logger_policy> log(lgr);
  auto schema_it = sections.find(section_type::METADATA_V2_SCHEMA);
  auto meta_it = sections.find(section_type::METADATA_V2);

  if (schema_it == sections.end()) {
    throw std::runtime_error("no metadata schema found");
  }

  if (meta_it == sections.end()) {
    throw std::runtime_error("no metadata found");
  }

  auto& meta_section = meta_it->second;

  auto meta_section_range =
      get_section_data(mm, meta_section, meta_buffer, force_buffers);

  if (lock_mode != mlock_mode::NONE) {
    if (auto ec = mm->lock(meta_section.start, meta_section_range.size())) {
      if (lock_mode == mlock_mode::MUST) {
        throw boost::system::system_error(ec, "mlock");
      } else {
        log.warn() << "mlock() failed: " << ec.message();
      }
    }
  }

  // don't keep the compressed metadata in cache
  if (meta_section.header.compression != compression_type::NONE) {
    if (auto ec = mm->release(meta_section.start, meta_section.header.length)) {
      log.info() << "madvise() failed: " << ec.message();
    }
  }

  return metadata_v2(
      lgr,
      get_section_data(mm, schema_it->second, schema_buffer, force_buffers),
      meta_section_range, options, stat_defaults, inode_offset);
}

template <typename LoggerPolicy>
class filesystem_ : public filesystem_v2::impl {
 public:
  filesystem_(logger& lgr_, std::shared_ptr<mmif> mm,
              const filesystem_options& options,
              const struct ::stat* stat_defaults, int inode_offset);

  void dump(std::ostream& os, int detail_level) const override;
  folly::dynamic metadata_as_dynamic() const override;
  void walk(std::function<void(entry_view)> const& func) const override;
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

 private:
  log_proxy<LoggerPolicy> log_;
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
    : log_(lgr)
    , mm_(mm) {
  filesystem_parser parser(mm_);
  block_cache cache(lgr, mm, options.block_cache);

  section_map sections;

  while (auto s = parser.next_section(log_)) {
    if (s->header.type == section_type::BLOCK) {
      cache.insert(s->header.compression, s->start,
                   static_cast<size_t>(s->header.length));
    } else {
      if (!sections.emplace(s->header.type, *s).second) {
        throw std::runtime_error("duplicate section: " +
                                 get_section_name(s->header.type));
      }
    }
  }

  std::vector<uint8_t> schema_buffer;

  meta_ = make_metadata(lgr, mm_, sections, schema_buffer, meta_buffer_,
                        options.metadata, stat_defaults, inode_offset, false,
                        options.lock_mode);

  log_.debug() << "read " << cache.block_count() << " blocks and "
               << meta_.size() << " bytes of metadata";

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
      log_.error() << "error reading chunks for inode " << inode;
    }
  });
}

template <typename LoggerPolicy>
folly::dynamic filesystem_<LoggerPolicy>::metadata_as_dynamic() const {
  return meta_.as_dynamic();
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  meta_.walk(func);
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
  return -1;
}

template <typename LoggerPolicy>
ssize_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                         size_t size, off_t offset) const {
  if (auto chunks = meta_.get_chunks(inode)) {
    return ir_.readv(buf, size, offset, *chunks);
  }
  return -1;
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
                            std::shared_ptr<mmif> mm,
                            filesystem_writer& writer) {
  // TODO:
  log_proxy<debug_logger_policy> log(lgr);
  filesystem_parser parser(mm);

  section_map sections;

  while (auto s = parser.next_section(log)) {
    if (s->header.type == section_type::BLOCK) {
      ++prog.block_count;
    } else {
      if (!sections.emplace(s->header.type, *s).second) {
        throw std::runtime_error("duplicate section: " +
                                 get_section_name(s->header.type));
      }
    }
  }

  std::vector<uint8_t> schema_raw;
  std::vector<uint8_t> meta_raw;
  auto meta = make_metadata(lgr, mm, sections, schema_raw, meta_raw,
                            metadata_options(), nullptr, 0, true);

  struct ::statvfs stbuf;
  meta.statvfs(&stbuf);
  prog.original_size = stbuf.f_blocks * stbuf.f_frsize;

  parser.rewind();

  while (auto s = parser.next_section(log)) {
    // TODO: multi-thread this?
    if (s->header.type == section_type::BLOCK) {
      auto block = block_decompressor::decompress(
          s->header.compression, mm->as<uint8_t>(s->start), s->header.length);
      prog.filesystem_size += block.size();
      writer.write_block(std::move(block));
    }
  }

  writer.write_metadata_v2_schema(std::move(schema_raw));
  writer.write_metadata_v2(std::move(meta_raw));

  writer.flush();
}

void filesystem_v2::identify(logger& lgr, std::shared_ptr<mmif> mm,
                             std::ostream& os, int detail_level) {
  // TODO:
  log_proxy<debug_logger_policy> log(lgr);
  filesystem_parser parser(mm);

  section_map sections;

  while (auto s = parser.next_section(log)) {
    std::vector<uint8_t> tmp;
    block_decompressor bd(s->header.compression, mm->as<uint8_t>(s->start),
                          s->header.length, tmp);
    float compression_ratio = float(s->header.length) / bd.uncompressed_size();

    os << "SECTION " << s->header.to_string()
       << ", blocksize=" << bd.uncompressed_size()
       << ", ratio=" << fmt::format("{:.2f}%", 100.0 * compression_ratio)
       << std::endl;

    if (s->header.type != section_type::BLOCK) {
      if (!sections.emplace(s->header.type, *s).second) {
        throw std::runtime_error("duplicate section: " +
                                 get_section_name(s->header.type));
      }
    }
  }

  if (detail_level > 0) {
    filesystem_v2(lgr, mm).dump(os, detail_level);
  }
}

} // namespace dwarfs

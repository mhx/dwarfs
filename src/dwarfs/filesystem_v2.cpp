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

#include <folly/container/Enumerate.h>

#include <fmt/core.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/config.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/inode_reader.h"
#include "dwarfs/progress.h"

namespace dwarfs {

namespace {

class filesystem_parser {
 public:
  filesystem_parser(std::shared_ptr<mmif> mm)
      : mm_(mm)
      , offset_(sizeof(file_header)) {
    if (mm_->size() < sizeof(file_header)) {
      throw std::runtime_error("file too small");
    }

    const file_header* fh = mm_->as<file_header>();

    if (::memcmp(&fh->magic[0], "DWARFS", 6) != 0 &&
        ::memcmp(&fh->magic[0], "NANOFS", 6) != 0) { // keep for compatibility
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

  void rewind() { offset_ = sizeof(file_header); }

 private:
  std::shared_ptr<mmif> mm_;
  size_t offset_;
};

template <typename LoggerPolicy>
class filesystem_ : public filesystem_v2::impl {
 public:
  filesystem_(logger& lgr_, std::shared_ptr<mmif> mm,
              const block_cache_options& bc_options,
              const struct ::stat* stat_defaults, int inode_offset);

  void dump(std::ostream& os) const override;
  void walk(std::function<void(entry_view)> const& func) const override;
#if 0
  const dir_entry* find(const char* path) const override;
  const dir_entry* find(int inode) const override;
  const dir_entry* find(int inode, const char* name) const override;
  int getattr(const dir_entry* de, struct ::stat* stbuf) const override;
  int access(const dir_entry* de, int mode, uid_t uid,
             gid_t gid) const override;
  const directory* opendir(const dir_entry* de) const override;
  const dir_entry*
  readdir(const directory* d, size_t offset, std::string* name) const override;
  size_t dirsize(const directory* d) const override;
  int readlink(const dir_entry* de, char* buf, size_t size) const override;
  int readlink(const dir_entry* de, std::string* buf) const override;
  int statvfs(struct ::statvfs* stbuf) const override;
  int open(const dir_entry* de) const override;
  ssize_t
  read(uint32_t inode, char* buf, size_t size, off_t offset) const override;
  ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                off_t offset) const override;
#endif

 private:
  log_proxy<LoggerPolicy> log_;
  std::shared_ptr<mmif> mm_;
  metadata_v2 meta_;
  inode_reader ir_;
  std::vector<uint8_t> meta_buffer_;
};

template <typename LoggerPolicy>
filesystem_<LoggerPolicy>::filesystem_(logger& lgr, std::shared_ptr<mmif> mm,
                                       const block_cache_options& bc_options,
                                       const struct ::stat* stat_defaults,
                                       int inode_offset)
    : log_(lgr)
    , mm_(mm) {
  filesystem_parser parser(mm_);
  block_cache cache(lgr, bc_options);

  section_header sh;
  section_header sh_schema;
  section_header sh_data;
  size_t start;
  size_t start_schema = 0;
  size_t start_data = 0;

  while (parser.next_section(sh, start, log_)) {
    switch (sh.type) {
    case section_type::BLOCK:
      cache.insert(sh.compression, mm_->as<uint8_t>(start),
                   static_cast<size_t>(sh.length));
      break;

    case section_type::METADATA:
      // TODO: ignore for now, fail later
      break;

    case section_type::METADATA_V2_SCHEMA:
      sh_schema = sh;
      start_schema = start;
      break;

    case section_type::METADATA_V2:
      sh_data = sh;
      start_data = start;
      break;

    default:
      throw std::runtime_error("unknown section");
    }
  }

  if (start_schema == 0 || sh_schema.length == 0) {
    throw std::runtime_error("no metadata schema found");
  }

  if (start_data == 0) {
    throw std::runtime_error("no metadata found");
  }

  folly::ByteRange schema;
  folly::ByteRange data;
  std::vector<uint8_t> schema_buf;

  if (sh_data.compression == compression_type::NONE) {
    data = mm_->range(start_data, sh_data.length);
  } else {
    meta_buffer_ = block_decompressor::decompress(
        sh_data.compression, mm_->as<uint8_t>(start_data), sh_data.length);
    data = meta_buffer_;
  }

  if (sh_schema.compression == compression_type::NONE) {
    schema = mm_->range(start_schema, sh_schema.length);
  } else {
    schema_buf = block_decompressor::decompress(sh_schema.compression,
                                                mm_->as<uint8_t>(start_schema),
                                                sh_schema.length);
    schema = schema_buf;
  }

  log_.info() << "schema: " << schema.size() << " (" << sh_schema.length
              << "), data: " << data.size() << " (" << sh_data.length << ")";

  meta_ = metadata_v2(lgr, schema, data, stat_defaults, inode_offset);

  log_.debug() << "read " << cache.block_count() << " blocks and "
               << meta_.size() << " bytes of metadata";

  cache.set_block_size(meta_.block_size());

  // TODO:
  // ir_ = inode_reader(lgr, std::move(cache), meta_.block_size_bits());
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::dump(std::ostream& os) const {
  meta_.dump(os, [&](const std::string& indent, uint32_t inode) {
    auto chunks = meta_.get_chunks(inode);

    if (chunks) {
      os << indent << chunks->size() << " chunks in inode " << inode << "\n";
      for (auto chunk : folly::enumerate(*chunks)) {
        os << indent << "  [" << chunk.index << "] -> (" << chunk->block()
           << ", " << chunk->offset() << ", " << chunk->size() << ")\n";
      }
    }

    // ir_.dump(os, indent + "  ", chunk, num);  // TODO
  });
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  meta_.walk(func);
}

#if 0
template <typename LoggerPolicy>
const dir_entry* filesystem_<LoggerPolicy>::find(const char* path) const {
  return meta_.find(path);
}

template <typename LoggerPolicy>
const dir_entry* filesystem_<LoggerPolicy>::find(int inode) const {
  return meta_.find(inode);
}

template <typename LoggerPolicy>
const dir_entry*
filesystem_<LoggerPolicy>::find(int inode, const char* name) const {
  return meta_.find(inode, name);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::getattr(const dir_entry* de,
                                       struct ::stat* stbuf) const {
  return meta_.getattr(de, stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::access(const dir_entry* de, int mode, uid_t uid,
                                      gid_t gid) const {
  return meta_.access(de, mode, uid, gid);
}

template <typename LoggerPolicy>
const directory* filesystem_<LoggerPolicy>::opendir(const dir_entry* de) const {
  return meta_.opendir(de);
}

template <typename LoggerPolicy>
const dir_entry*
filesystem_<LoggerPolicy>::readdir(const directory* d, size_t offset,
                                   std::string* name) const {
  return meta_.readdir(d, offset, name);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::dirsize(const directory* d) const {
  return meta_.dirsize(d);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::readlink(const dir_entry* de, char* buf,
                                        size_t size) const {
  return meta_.readlink(de, buf, size);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::readlink(const dir_entry* de,
                                        std::string* buf) const {
  return meta_.readlink(de, buf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  // TODO: not sure if that's the right abstraction...
  return meta_.statvfs(stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(const dir_entry* de) const {
  return meta_.open(de);
}

template <typename LoggerPolicy>
ssize_t filesystem_<LoggerPolicy>::read(uint32_t inode, char* buf, size_t size,
                                        off_t offset) const {
  size_t num = 0;
  const chunk_type* chunk = meta_.get_chunks(inode, num);
  return ir_.read(buf, size, offset, chunk, num);
}

template <typename LoggerPolicy>
ssize_t filesystem_<LoggerPolicy>::readv(uint32_t inode, iovec_read_buf& buf,
                                         size_t size, off_t offset) const {
  size_t num = 0;
  const chunk_type* chunk = meta_.get_chunks(inode, num);
  return ir_.readv(buf, size, offset, chunk, num);
}
#endif

} // namespace

filesystem_v2::filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm,
                             const block_cache_options& bc_options,
                             const struct ::stat* stat_defaults,
                             int inode_offset)
    : impl_(make_unique_logging_object<filesystem_v2::impl, filesystem_,
                                       logger_policies>(
          lgr, mm, bc_options, stat_defaults, inode_offset)) {}

void filesystem_v2::rewrite(logger& lgr, progress& prog,
                            std::shared_ptr<mmif> mm,
                            filesystem_writer& writer) {
  // TODO:
  log_proxy<debug_logger_policy> log(lgr);
  filesystem_parser parser(mm);

  section_header sh;
  size_t start;
  std::vector<uint8_t> meta_raw;
  metadata_v2 meta;

  while (parser.next_section(sh, start, log)) {
    if (sh.type == section_type::METADATA) {
      // TODO: only decompress if needed
      meta_raw = block_decompressor::decompress(
          sh.compression, mm->as<uint8_t>(start), sh.length);
      // TODO: FIXME:
      // meta = metadata_v2(lgr, meta_raw, nullptr);
      break;
    } else {
      ++prog.block_count;
    }
  }

  struct ::statvfs stbuf;
  meta.statvfs(&stbuf);
  prog.original_size = stbuf.f_blocks * stbuf.f_frsize;

  parser.rewind();

  while (parser.next_section(sh, start, log)) {
    // TODO: multi-thread this?
    switch (sh.type) {
    case section_type::BLOCK: {
      auto block = block_decompressor::decompress(
          sh.compression, mm->as<uint8_t>(start), sh.length);
      prog.filesystem_size += block.size();
      writer.write_block(std::move(block));
      break;
    }

    case section_type::METADATA:
      writer.write_metadata(std::move(meta_raw));
      break;

    case section_type::METADATA_V2:
      // TODO...
      break;

    default:
      throw std::runtime_error("unknown section");
    }
  }

  writer.flush();
}

void filesystem_v2::identify(logger& lgr, std::shared_ptr<mmif> mm,
                             std::ostream& os) {
  // TODO:
  log_proxy<debug_logger_policy> log(lgr);
  filesystem_parser parser(mm);

  section_header sh;
  size_t start;

  while (parser.next_section(sh, start, log)) {
    std::vector<uint8_t> tmp;
    block_decompressor bd(sh.compression, mm->as<uint8_t>(start), sh.length,
                          tmp);
    float compression_ratio = float(sh.length) / bd.uncompressed_size();

    os << "SECTION " << sh.to_string()
       << ", blocksize=" << bd.uncompressed_size()
       << ", ratio=" << fmt::format("{:.2%}%", compression_ratio) << std::endl;

    // TODO: do we need this?
    // if (sh.type == section_type::METADATA_V2) {
    //   // TODO: only decompress if needed
    //   bd.decompress_frame(bd.uncompressed_size());
    //   metadata_v2 meta(lgr, tmp, nullptr);
    //   struct ::statvfs stbuf;
    //   meta.statvfs(&stbuf);
    //   os << "block size: " << stbuf.f_bsize << std::endl;
    //   os << "inode count: " << stbuf.f_files << std::endl;
    //   os << "original filesystem size: " << stbuf.f_blocks << std::endl;
    // }
  }
}

} // namespace dwarfs

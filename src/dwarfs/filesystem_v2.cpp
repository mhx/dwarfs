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
#include <mutex>
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
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

class filesystem_parser {
 private:
  static uint64_t constexpr section_offset_mask{(UINT64_C(1) << 48) - 1};

 public:
  static off_t find_image_offset(mmif& mm, off_t image_offset) {
    if (image_offset != filesystem_options::IMAGE_OFFSET_AUTO) {
      return image_offset;
    }

    static constexpr std::array<char, 7> magic{
        {'D', 'W', 'A', 'R', 'F', 'S', MAJOR_VERSION}};

    off_t start = 0;
    for (;;) {
      if (start + magic.size() >= mm.size()) {
        break;
      }

      auto ps = mm.as<void>(start);
      auto pc = ::memmem(ps, mm.size() - start, magic.data(), magic.size());

      if (!pc) {
        break;
      }

      off_t pos = start + static_cast<uint8_t const*>(pc) -
                  static_cast<uint8_t const*>(ps);

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

        ps = mm.as<void>(pos + sh->length + sizeof(section_header_v2));

        if (::memcmp(ps, magic.data(), magic.size()) == 0 and
            reinterpret_cast<section_header_v2 const*>(ps)->number == 1) {
          return pos;
        }
      }

      start = pos + magic.size();
    }

    DWARFS_THROW(runtime_error, "no filesystem found");
  }

  explicit filesystem_parser(std::shared_ptr<mmif> mm, off_t image_offset = 0)
      : mm_{mm}
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
      if (offset_ < static_cast<off_t>(mm_->size())) {
        auto section = fs_section(*mm_, offset_, version_);
        offset_ = section.end();
        return section;
      }
    } else {
      if (offset_ < static_cast<off_t>(index_.size())) {
        uint64_t id = index_[offset_++];
        uint64_t offset = id & section_offset_mask;
        uint64_t next_offset = offset_ < static_cast<off_t>(index_.size())
                                   ? index_[offset_] & section_offset_mask
                                   : mm_->size() - image_offset_;
        return fs_section(mm_, static_cast<section_type>(id >> 48),
                          image_offset_ + offset, next_offset - offset,
                          version_);
      }
    }

    return std::nullopt;
  }

  std::optional<folly::ByteRange> header() const {
    if (image_offset_ == 0) {
      return std::nullopt;
    }
    return folly::ByteRange(mm_->as<uint8_t>(), image_offset_);
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

  off_t image_offset() const { return image_offset_; }

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
  off_t const image_offset_;
  off_t offset_{0};
  int version_{0};
  uint8_t major_{0};
  uint8_t minor_{0};
  std::vector<uint64_t> index_;
};

using section_map = std::unordered_map<section_type, fs_section>;

size_t
get_uncompressed_section_size(std::shared_ptr<mmif> mm, fs_section const& sec) {
  std::vector<uint8_t> tmp;
  block_decompressor bd(sec.compression(), mm->as<uint8_t>(sec.start()),
                        sec.length(), tmp);
  return bd.uncompressed_size();
}

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
              const metadata_options& options, int inode_offset = 0,
              bool force_buffers = false,
              mlock_mode lock_mode = mlock_mode::NONE,
              bool force_consistency_check = false) {
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
      meta_section_range, options, inode_offset, force_consistency_check);
}

template <typename LoggerPolicy>
class filesystem_ final : public filesystem_v2::impl {
 public:
  filesystem_(logger& lgr, std::shared_ptr<mmif> mm,
              const filesystem_options& options, int inode_offset);

  void dump(std::ostream& os, int detail_level) const override;
  folly::dynamic metadata_as_dynamic() const override;
  std::string serialize_metadata_as_json(bool simple) const override;
  void walk(std::function<void(dir_entry_view)> const& func) const override;
  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override;
  std::optional<inode_view> find(const char* path) const override;
  std::optional<inode_view> find(int inode) const override;
  std::optional<inode_view> find(int inode, const char* name) const override;
  int getattr(inode_view entry, struct ::stat* stbuf) const override;
  int access(inode_view entry, int mode, uid_t uid, gid_t gid) const override;
  std::optional<directory_view> opendir(inode_view entry) const override;
  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const override;
  size_t dirsize(directory_view dir) const override;
  int readlink(inode_view entry, std::string* buf) const override;
  folly::Expected<std::string, int> readlink(inode_view entry) const override;
  int statvfs(struct ::statvfs* stbuf) const override;
  int open(inode_view entry) const override;
  ssize_t
  read(uint32_t inode, char* buf, size_t size, off_t offset) const override;
  ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                off_t offset) const override;
  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(uint32_t inode, size_t size, off_t offset) const override;
  std::optional<folly::ByteRange> header() const override;
  void set_num_workers(size_t num) override { ir_.set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) override {
    ir_.set_cache_tidy_config(cfg);
  }

 private:
  filesystem_info const& get_info() const;

  LOG_PROXY_DECL(LoggerPolicy);
  std::shared_ptr<mmif> mm_;
  metadata_v2 meta_;
  inode_reader_v2 ir_;
  mutable std::mutex mx_;
  mutable filesystem_parser parser_;
  std::vector<uint8_t> meta_buffer_;
  std::optional<folly::ByteRange> header_;
  mutable std::unique_ptr<filesystem_info const> fsinfo_;
};

template <typename LoggerPolicy>
filesystem_info const& filesystem_<LoggerPolicy>::get_info() const {
  std::lock_guard lock(mx_);

  if (!fsinfo_) {
    filesystem_info info;

    parser_.rewind();

    while (auto s = parser_.next_section()) {
      if (s->type() == section_type::BLOCK) {
        ++info.block_count;
        info.compressed_block_size += s->length();
        info.uncompressed_block_size += get_uncompressed_section_size(mm_, *s);
      } else if (s->type() == section_type::METADATA_V2) {
        info.compressed_metadata_size += s->length();
        info.uncompressed_metadata_size +=
            get_uncompressed_section_size(mm_, *s);
      }
    }

    fsinfo_ = std::make_unique<filesystem_info>(info);
  }

  return *fsinfo_;
}

template <typename LoggerPolicy>
filesystem_<LoggerPolicy>::filesystem_(logger& lgr, std::shared_ptr<mmif> mm,
                                       const filesystem_options& options,
                                       int inode_offset)
    : LOG_PROXY_INIT(lgr)
    , mm_(std::move(mm))
    , parser_(mm_, options.image_offset) {
  block_cache cache(lgr, mm_, options.block_cache);

  if (parser_.has_index()) {
    LOG_DEBUG << "found valid section index";
  }

  header_ = parser_.header();

  section_map sections;

  while (auto s = parser_.next_section()) {
    LOG_DEBUG << "section " << s->name() << " @ " << s->start() << " ["
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
                        options.metadata, inode_offset, false,
                        options.lock_mode, !parser_.has_checksums());

  LOG_DEBUG << "read " << cache.block_count() << " blocks and " << meta_.size()
            << " bytes of metadata";

  cache.set_block_size(meta_.block_size());

  ir_ = inode_reader_v2(lgr, std::move(cache));
}

template <typename LoggerPolicy>
void filesystem_<LoggerPolicy>::dump(std::ostream& os, int detail_level) const {
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
  return meta_.find(path);
}

template <typename LoggerPolicy>
std::optional<inode_view> filesystem_<LoggerPolicy>::find(int inode) const {
  return meta_.find(inode);
}

template <typename LoggerPolicy>
std::optional<inode_view>
filesystem_<LoggerPolicy>::find(int inode, const char* name) const {
  return meta_.find(inode, name);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::getattr(inode_view entry,
                                       struct ::stat* stbuf) const {
  return meta_.getattr(entry, stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::access(inode_view entry, int mode, uid_t uid,
                                      gid_t gid) const {
  return meta_.access(entry, mode, uid, gid);
}

template <typename LoggerPolicy>
std::optional<directory_view>
filesystem_<LoggerPolicy>::opendir(inode_view entry) const {
  return meta_.opendir(entry);
}

template <typename LoggerPolicy>
std::optional<std::pair<inode_view, std::string>>
filesystem_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
  return meta_.readdir(dir, offset);
}

template <typename LoggerPolicy>
size_t filesystem_<LoggerPolicy>::dirsize(directory_view dir) const {
  return meta_.dirsize(dir);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::readlink(inode_view entry,
                                        std::string* buf) const {
  return meta_.readlink(entry, buf);
}

template <typename LoggerPolicy>
folly::Expected<std::string, int>
filesystem_<LoggerPolicy>::readlink(inode_view entry) const {
  return meta_.readlink(entry);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  // TODO: not sure if that's the right abstraction...
  return meta_.statvfs(stbuf);
}

template <typename LoggerPolicy>
int filesystem_<LoggerPolicy>::open(inode_view entry) const {
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

template <typename LoggerPolicy>
std::optional<folly::ByteRange> filesystem_<LoggerPolicy>::header() const {
  return header_;
}

} // namespace

filesystem_v2::filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm)
    : filesystem_v2(lgr, std::move(mm), filesystem_options()) {}

filesystem_v2::filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm,
                             const filesystem_options& options,
                             int inode_offset)
    : impl_(make_unique_logging_object<filesystem_v2::impl, filesystem_,
                                       logger_policies>(
          lgr, std::move(mm), options, inode_offset)) {}

void filesystem_v2::rewrite(logger& lgr, progress& prog,
                            std::shared_ptr<mmif> mm, filesystem_writer& writer,
                            rewrite_options const& opts) {
  // TODO:
  LOG_PROXY(debug_logger_policy, lgr);
  filesystem_parser parser(mm, opts.image_offset);

  if (auto hdr = parser.header()) {
    writer.copy_header(*hdr);
  }

  std::vector<section_type> section_types;
  section_map sections;

  while (auto s = parser.next_section()) {
    LOG_DEBUG << "section " << s->description() << " @ " << s->start() << " ["
              << s->length() << " bytes]";
    if (!s->check_fast(*mm)) {
      DWARFS_THROW(runtime_error, "checksum error in section: " + s->name());
    }
    if (!s->verify(*mm)) {
      DWARFS_THROW(runtime_error,
                   "integrity check error in section: " + s->name());
    }
    prog.original_size += s->length();
    prog.filesystem_size += s->length();
    if (s->type() == section_type::BLOCK) {
      ++prog.block_count;
    } else if (s->type() != section_type::SECTION_INDEX) {
      if (!sections.emplace(s->type(), *s).second) {
        DWARFS_THROW(runtime_error, "duplicate section: " + s->name());
      }
      section_types.push_back(s->type());
    }
  }

  std::vector<uint8_t> schema_raw;
  std::vector<uint8_t> meta_raw;

  // force metadata check
  make_metadata(lgr, mm, sections, schema_raw, meta_raw, metadata_options(), 0,
                true, mlock_mode::NONE, !parser.has_checksums());

  parser.rewind();

  while (auto s = parser.next_section()) {
    // TODO: multi-thread this?
    if (s->type() == section_type::BLOCK) {
      if (opts.recompress_block) {
        auto block =
            std::make_shared<block_data>(block_decompressor::decompress(
                s->compression(), mm->as<uint8_t>(s->start()), s->length()));
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

int filesystem_v2::identify(logger& lgr, std::shared_ptr<mmif> mm,
                            std::ostream& os, int detail_level,
                            size_t num_readers, bool check_integrity,
                            off_t image_offset) {
  // TODO:
  LOG_PROXY(debug_logger_policy, lgr);
  filesystem_parser parser(mm, image_offset);

  if (detail_level > 0) {
    os << "DwarFS version " << parser.version();
    if (auto off = parser.image_offset(); off > 0) {
      os << " at offset " << off;
    }
    os << std::endl;
  }

  worker_group wg("reader", num_readers);
  std::vector<std::future<fs_section>> sections;

  while (auto sp = parser.next_section()) {
    LOG_DEBUG << "section " << sp->description() << " @ " << sp->start() << " ["
              << sp->length() << " bytes]";
    std::packaged_task<fs_section()> task{[&, s = *sp] {
      if (!s.check_fast(*mm)) {
        DWARFS_THROW(runtime_error, "checksum error in section: " + s.name());
      }

      if (check_integrity and !s.verify(*mm)) {
        DWARFS_THROW(runtime_error,
                     "integrity check error in section: " + s.name());
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

      auto uncompressed_size = get_uncompressed_section_size(mm, s);
      float compression_ratio = float(s.length()) / uncompressed_size;

      if (detail_level > 2) {
        os << "SECTION " << s.description()
           << ", blocksize=" << uncompressed_size
           << ", ratio=" << fmt::format("{:.2f}%", 100.0 * compression_ratio)
           << std::endl;
      }

      if (s.type() != section_type::BLOCK) {
        if (!seen.emplace(s.type()).second) {
          DWARFS_THROW(runtime_error, "duplicate section: " + s.name());
        }
      }
    } catch (runtime_error const& e) {
      LOG_ERROR << e.what();
      ++errors;
    }
  }

  if (errors == 0 and detail_level > 0) {
    filesystem_options fsopts;
    fsopts.metadata.check_consistency = true;
    fsopts.metadata.enable_nlink = true;
    fsopts.image_offset = image_offset;
    filesystem_v2(lgr, mm, fsopts).dump(os, detail_level);
  }

  return errors;
}

std::optional<folly::ByteRange>
filesystem_v2::header(std::shared_ptr<mmif> mm) {
  return header(std::move(mm), filesystem_options::IMAGE_OFFSET_AUTO);
}

std::optional<folly::ByteRange>
filesystem_v2::header(std::shared_ptr<mmif> mm, off_t image_offset) {
  return filesystem_parser(mm, image_offset).header();
}

} // namespace dwarfs

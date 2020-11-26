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

#include <cstring>

#include <unistd.h>

#include "dwarfs/metadata_v2.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"
#include "dwarfs/gen-cpp2/metadata_types.h"
#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs {

template <typename LoggerPolicy>
class metadata_v2_ : public metadata_v2::impl {
 public:
  // TODO: pass folly::ByteRange instead of vector (so we can support memory mapping)
  metadata_v2_(logger& lgr, std::vector<uint8_t>&& meta,
               const struct ::stat* /*defaults*/)
      : data_(std::move(meta))
      , meta_(::apache::thrift::frozen::mapFrozen<thrift::metadata::metadata>(
            data_))
      , root_(meta_.entries()[meta_.entry_index()[0]])
      , inode_offset_(meta_.chunk_index_offset())
      , log_(lgr) {
    // TODO: defaults?
    log_.debug() << ::apache::thrift::debugString(meta_.thaw());

    ::apache::thrift::frozen::Layout<thrift::metadata::metadata> layout;
    ::apache::thrift::frozen::schema::Schema schema;
    folly::ByteRange range(data_);
    apache::thrift::CompactSerializer::deserialize(range, schema);
    log_.debug() << ::apache::thrift::debugString(schema);
  }

  void dump(std::ostream& os,
            std::function<void(const std::string&, uint32_t)> const& icb)
      const override;

  size_t size() const override { return data_.size(); }

  bool empty() const override { return data_.empty(); }

  void walk(std::function<void(entry_view)> const& func) const override;

#if 0
  size_t block_size() const override {
    return static_cast<size_t>(1) << cfg_->block_size_bits;
  }

  unsigned block_size_bits() const override { return cfg_->block_size_bits; }

  const dir_entry* find(const char* path) const override;
  const dir_entry* find(int inode) const override;
  const dir_entry* find(int inode, const char* name) const override;
  int getattr(const dir_entry* de, struct ::stat* stbuf) const override;
  int access(const dir_entry* de, int mode, uid_t uid,
             gid_t gid) const override;
  const directory* opendir(const dir_entry* de) const override;
  const dir_entry*
  readdir(const directory* d, size_t offset, std::string* name) const override;
  size_t dirsize(const directory* d) const override {
    return d->count + 2; // adds '.' and '..', which we fake in ;-)
  }
  int readlink(const dir_entry* de, char* buf, size_t size) const override;
  int readlink(const dir_entry* de, std::string* buf) const override;
  int statvfs(struct ::statvfs* stbuf) const override;
  int open(const dir_entry* de) const override;

  const chunk_type* get_chunks(int inode, size_t& num) const override;
#endif

 private:
  void dump(std::ostream& os, const std::string& indent, entry_view entry,
            std::function<void(const std::string&, uint32_t)> const& icb) const;
  void dump(std::ostream& os, const std::string& indent, directory_view dir,
            std::function<void(const std::string&, uint32_t)> const& icb) const;

  std::string modestring(uint16_t mode) const;

  size_t reg_filesize(uint32_t inode) const {
    uint32_t cur = meta_.chunk_index()[inode];
    uint32_t end = meta_.chunk_index()[inode + 1];
    size_t size = 0;
    while (cur < end) {
      size += meta_.chunks()[cur++].size();
    }
    return size;
  }

  size_t filesize(entry_view entry, uint16_t mode) const {
    if (S_ISREG(mode)) {
      return reg_filesize(entry.inode());
    } else if (S_ISLNK(mode)) {
      return meta_.links()[meta_.link_index()[entry.inode() - meta_.link_index_offset()]].size();
    } else {
      return 0;
    }
  }

  directory_view getdir(entry_view entry) const {
    return meta_.directories()[entry.inode()];
  }

  void walk(entry_view entry,
            std::function<void(entry_view)> const& func) const;

#if 0
  std::string name(const dir_entry* de) const {
    return std::string(as<char>(de->name_offset), de->name_size);
  }

  size_t linksize(const dir_entry* de) const {
    return *as<uint16_t>(de->u.offset);
  }

  std::string linkname(const dir_entry* de) const {
    size_t offs = de->u.offset;
    return std::string(as<char>(offs + sizeof(uint16_t)), *as<uint16_t>(offs));
  }

  const char* linkptr(const dir_entry* de) const {
    return as<char>(de->u.offset + sizeof(uint16_t));
  }

  template <typename T>
  const T* as(size_t offset = 0) const {
    return reinterpret_cast<const T*>(
        reinterpret_cast<const char*>(data_.data()) + offset);
  }

  const dir_entry* get_entry(int inode) const {
    inode -= inode_offset_;
    return inode >= 0 && inode < static_cast<int>(cfg_->inode_count)
               ? as<dir_entry>(inode_index_[inode])
               : nullptr;
  }

  void parse(const struct ::stat* defaults);

  const uint32_t* chunk_index_ = nullptr;
  const uint32_t* inode_index_ = nullptr;
  const dir_entry* root_ = nullptr;
  const meta_config* cfg_ = nullptr;
  std::shared_ptr<dir_reader> dir_reader_;
#endif
  std::vector<uint8_t> data_;
  ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata> meta_;
  entry_view root_;
  const int inode_offset_;
  log_proxy<LoggerPolicy> log_;
};

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, entry_view entry,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto mode = meta_.modes()[entry.mode()];
  auto inode = entry.inode();

  os << indent << "<inode:" << inode << "> " << modestring(mode);

  if (inode > 0) {
    os << " " << meta_.names()[entry.name_index()];
  }

  if (S_ISREG(mode)) {
    uint32_t cur = meta_.chunk_index()[inode - inode_offset_];
    uint32_t end = meta_.chunk_index()[inode - inode_offset_ + 1];
    os << " [" << cur << ", " << end << "]";
    size_t size = 0;
    while (cur < end) {
      size += meta_.chunks()[cur++].size();
    }
    os << " " << size << "\n";
    // os << " " << filesize(entry, mode) << "\n";
    // icb(indent + "  ", de->inode);
  } else if (S_ISDIR(mode)) {
    dump(os, indent + "  ", meta_.directories()[inode], std::move(icb));
  } else if (S_ISLNK(mode)) {
    os << " -> " << meta_.links()[meta_.link_index()[inode] - meta_.link_index_offset()] << "\n";
  } else {
    os << " (unknown type)\n";
  }
}

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, directory_view dir,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto count = dir.entry_count();
  auto first = dir.first_entry();
  os << indent << "(" << count << ") entries\n";

  for (size_t i = 0; i < count; ++i) {
    dump(os, indent, meta_.entries()[first + i], icb);
  }
}

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::dump(
    std::ostream& os,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  dump(os, "", root_, icb);
}

template <typename LoggerPolicy>
std::string metadata_v2_<LoggerPolicy>::modestring(uint16_t mode) const {
  std::ostringstream oss;

  oss << (mode & S_ISUID ? 'U' : '-');
  oss << (mode & S_ISGID ? 'G' : '-');
  oss << (mode & S_ISVTX ? 'S' : '-');
  oss << (S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : '-');
  oss << (mode & S_IRUSR ? 'r' : '-');
  oss << (mode & S_IWUSR ? 'w' : '-');
  oss << (mode & S_IXUSR ? 'x' : '-');
  oss << (mode & S_IRGRP ? 'r' : '-');
  oss << (mode & S_IWGRP ? 'w' : '-');
  oss << (mode & S_IXGRP ? 'x' : '-');
  oss << (mode & S_IROTH ? 'r' : '-');
  oss << (mode & S_IWOTH ? 'w' : '-');
  oss << (mode & S_IXOTH ? 'x' : '-');

  return oss.str();
}

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::walk(
    entry_view entry,
    std::function<void(entry_view)> const& func) const {
  func(entry);
  if (S_ISDIR(entry.mode())) {
    auto dir = getdir(entry);
    auto curr = dir.first_entry();
    auto last = curr + dir.entry_count();
    while (curr < last) {
      walk(meta_.entries()[curr++], func);
    }
  }
}

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  walk(root_, func);
}

#if 0
template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::parse(const struct ::stat* defaults) {
  size_t offset = 0;

  while (offset + sizeof(section_header) <= size()) {
    const section_header* sh = as<section_header>(offset);

    log_.debug() << "section_header@" << offset << " (" << sh->to_string()
                 << ")";

    offset += sizeof(section_header);

    if (offset + sh->length > size()) {
      throw std::runtime_error("truncated metadata");
    }

    if (sh->compression != compression_type::NONE) {
      throw std::runtime_error("unsupported metadata compression type");
    }

    switch (sh->type) {
    case section_type::META_TABLEDATA:
    case section_type::META_DIRECTORIES:
      // ok, ignore
      break;

    case section_type::META_CHUNK_INDEX:
      chunk_index_ = as<uint32_t>(offset);
      break;

    case section_type::META_INODE_INDEX:
      inode_index_ = as<uint32_t>(offset);
      break;

    case section_type::META_CONFIG:
      cfg_ = as<meta_config>(offset);
      break;

    default:
      throw std::runtime_error("unknown metadata section");
    }

    offset += sh->length;
  }

  // TODO: moar checkz

  if (!cfg_) {
    throw std::runtime_error("no metadata configuration found");
  }

  struct ::stat stat_defaults;

  if (defaults) {
    stat_defaults = *defaults;
  } else {
    metadata::get_stat_defaults(&stat_defaults);
  }

  chunk_index_ -= cfg_->chunk_index_offset;
  inode_index_ -= cfg_->inode_index_offset;

  root_ = as<dir_entry>(inode_index_[0]);

  dir_reader_ = dir_reader::create(cfg_->de_type, stat_defaults,
                                   reinterpret_cast<const char*>(data_.data()),
                                   inode_offset_);
}

template <typename LoggerPolicy>
const dir_entry* metadata_<LoggerPolicy>::find(const char* path) const {
  while (*path and *path == '/') {
    ++path;
  }

  const dir_entry* de = root_;

  while (*path) {
    const char* next = ::strchr(path, '/');
    size_t clen = next ? next - path : ::strlen(path);

    de = dir_reader_->find(getdir(de), path, clen);

    if (!de) {
      break;
    }

    path = next ? next + 1 : path + clen;
  }

  return de;
}

template <typename LoggerPolicy>
const dir_entry* metadata_<LoggerPolicy>::find(int inode) const {
  return get_entry(inode);
}

template <typename LoggerPolicy>
const dir_entry*
metadata_<LoggerPolicy>::find(int inode, const char* name) const {
  auto de = get_entry(inode);

  if (de) {
    de = dir_reader_->find(getdir(de), name, ::strlen(name));
  }

  return de;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::getattr(const dir_entry* de,
                                     struct ::stat* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));
  dir_reader_->getattr(de, stbuf, filesize(de));
  return 0;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::access(const dir_entry* de, int mode, uid_t uid,
                                    gid_t gid) const {
  return dir_reader_->access(de, mode, uid, gid);
}

template <typename LoggerPolicy>
const directory* metadata_<LoggerPolicy>::opendir(const dir_entry* de) const {
  if (S_ISDIR(de->mode)) {
    return getdir(de);
  }

  return nullptr;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::open(const dir_entry* de) const {
  if (S_ISREG(de->mode)) {
    return de->inode;
  }

  return -1;
}

template <typename LoggerPolicy>
const dir_entry*
metadata_<LoggerPolicy>::readdir(const directory* d, size_t offset,
                                 std::string* name) const {
  const dir_entry* de;

  switch (offset) {
  case 0:
    de = as<dir_entry>(d->self);

    if (name) {
      name->assign(".");
    }
    break;

  case 1:
    de = as<dir_entry>(d->parent);

    if (name) {
      name->assign("..");
    }
    break;

  default:
    offset -= 2;

    if (offset < d->count) {
      de = dir_reader_->readdir(d, offset, name);
    } else {
      return nullptr;
    }

    break;
  }

  return de;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::readlink(const dir_entry* de, char* buf,
                                      size_t size) const {
  if (S_ISLNK(de->mode)) {
    size_t lsize = linksize(de);

    ::memcpy(buf, linkptr(de), std::min(lsize, size));

    if (size > lsize) {
      buf[lsize] = '\0';
    }

    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::readlink(const dir_entry* de,
                                      std::string* buf) const {
  if (S_ISLNK(de->mode)) {
    size_t lsize = linksize(de);

    buf->assign(linkptr(de), lsize);

    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  stbuf->f_bsize = 1UL << cfg_->block_size_bits;
  stbuf->f_frsize = 1UL;
  stbuf->f_blocks = cfg_->orig_fs_size;
  stbuf->f_files = cfg_->inode_count;
  stbuf->f_flag = ST_RDONLY;
  stbuf->f_namemax = PATH_MAX;

  return 0;
}

template <typename LoggerPolicy>
const chunk_type*
metadata_<LoggerPolicy>::get_chunks(int inode, size_t& num) const {
  inode -= inode_offset_;
  if (inode < static_cast<int>(cfg_->chunk_index_offset) ||
      inode >= static_cast<int>(cfg_->inode_count)) {
    return nullptr;
  }
  uint32_t off = chunk_index_[inode];
  num = (chunk_index_[inode + 1] - off) / sizeof(chunk_type);
  return as<chunk_type>(off);
}

#endif

void metadata_v2::get_stat_defaults(struct ::stat* defaults) {
  ::memset(defaults, 0, sizeof(struct ::stat));
  defaults->st_uid = ::geteuid();
  defaults->st_gid = ::getegid();
  time_t t = ::time(nullptr);
  defaults->st_atime = t;
  defaults->st_mtime = t;
  defaults->st_ctime = t;
}

metadata_v2::metadata_v2(logger& lgr, std::vector<uint8_t>&& data,
                         const struct ::stat* defaults)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_v2_,
                                       logger_policies>(lgr, std::move(data),
                                                        defaults)) {}
} // namespace dwarfs

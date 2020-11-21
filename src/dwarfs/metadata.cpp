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

#include "dwarfs/metadata.h"

namespace dwarfs {

namespace {

const uint16_t READ_ONLY_MASK = ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

class dir_reader {
 public:
  static std::shared_ptr<dir_reader>
  create(dir_entry_type de_type, const struct ::stat& defaults,
         const char* data, int inode_offset);

  virtual ~dir_reader() = default;

  virtual const dir_entry*
  find(const directory* d, const char* path, size_t clen) const = 0;
  virtual void
  getattr(const dir_entry* de, struct ::stat* stbuf, size_t filesize) const = 0;
  virtual int
  access(const dir_entry* de, int mode, uid_t uid, gid_t gid) const = 0;
  virtual const dir_entry* readdir(const directory* d, size_t offset,
                                   std::string* name = nullptr) const = 0;
};

template <typename DirEntryType>
class dir_reader_ : public dir_reader {
 public:
  dir_reader_(const struct ::stat& defaults, const char* data, int inode_offset)
      : defaults_(defaults)
      , data_(data)
      , inode_offset_(inode_offset) {}

  const dir_entry*
  find(const directory* d, const char* path, size_t clen) const override {
    auto begin = reinterpret_cast<const DirEntryType*>(&d->u);
    auto end = begin + d->count;

    auto de = std::lower_bound(
        begin, end, path, [&](const DirEntryType& de, const char* p) {
          const dir_entry* e = reinterpret_cast<const dir_entry*>(&de);
          int cmp = ::strncmp(
              nameptr(e), p, std::min(static_cast<size_t>(e->name_size), clen));
          return cmp < 0 or (cmp == 0 and e->name_size < clen);
        });

    auto e = reinterpret_cast<const dir_entry*>(de);

    if (de != end and e->name_size == clen and
        ::strncmp(nameptr(e), path, clen) == 0) {
      return e;
    }

    return nullptr;
  }

  void getattr(const dir_entry* de, struct ::stat* stbuf,
               size_t filesize) const override {
    stbuf->st_mode = de->mode & READ_ONLY_MASK;
    stbuf->st_size = filesize;
    stbuf->st_ino = de->inode + inode_offset_;
    stbuf->st_blocks = (stbuf->st_size + 511) / 512;
    stbuf->st_uid = getuid(de);
    stbuf->st_gid = getgid(de);
    gettimes(de, stbuf);
  }

  int access(const dir_entry* de, int mode, uid_t uid,
             gid_t gid) const override {
    if (mode == F_OK) {
      // easy; we're only interested in the file's existance
      return 0;
    }

    int de_mode = 0;
    auto test = [de, &de_mode](uint16_t r_bit, uint16_t x_bit) {
      if (de->mode & r_bit) {
        de_mode |= R_OK;
      }
      if (de->mode & x_bit) {
        de_mode |= X_OK;
      }
    };

    // Let's build the entry's access mask
    test(S_IROTH, S_IXOTH);

    if (getgid(de) == gid) {
      test(S_IRGRP, S_IXGRP);
    }

    if (getuid(de) == uid) {
      test(S_IRUSR, S_IXUSR);
    }

    return (de_mode & mode) == mode ? 0 : EACCES;
  }

  const dir_entry*
  readdir(const directory* d, size_t offset, std::string* name) const override {
    auto begin = reinterpret_cast<const DirEntryType*>(&d->u);
    auto de = reinterpret_cast<const dir_entry*>(begin + offset);

    if (name) {
      name->assign(nameptr(de), de->name_size);
    }

    return de;
  }

 private:
  uid_t getuid(const dir_entry* de) const;
  gid_t getgid(const dir_entry* de) const;
  void gettimes(const dir_entry* de, struct ::stat* stbuf) const;

  template <typename T>
  const T* as(size_t offset = 0) const {
    return reinterpret_cast<const T*>(data_ + offset);
  }

  const char* nameptr(const dir_entry* de) const {
    return as<char>(de->name_offset);
  }

  const struct ::stat defaults_;
  const char* data_;
  const int inode_offset_;
};

template <>
uid_t dir_reader_<dir_entry>::getuid(const dir_entry*) const {
  return defaults_.st_uid;
}

template <>
gid_t dir_reader_<dir_entry>::getgid(const dir_entry*) const {
  return defaults_.st_gid;
}

template <>
uid_t dir_reader_<dir_entry_ug>::getuid(const dir_entry* de) const {
  auto real_de = reinterpret_cast<const dir_entry_ug*>(de);
  return real_de->owner;
}

template <>
gid_t dir_reader_<dir_entry_ug>::getgid(const dir_entry* de) const {
  auto real_de = reinterpret_cast<const dir_entry_ug*>(de);
  return real_de->group;
}

template <>
uid_t dir_reader_<dir_entry_ug_time>::getuid(const dir_entry* de) const {
  auto real_de = reinterpret_cast<const dir_entry_ug_time*>(de);
  return real_de->ug.owner;
}

template <>
gid_t dir_reader_<dir_entry_ug_time>::getgid(const dir_entry* de) const {
  auto real_de = reinterpret_cast<const dir_entry_ug_time*>(de);
  return real_de->ug.group;
}

template <>
void dir_reader_<dir_entry>::gettimes(const dir_entry*,
                                      struct ::stat* stbuf) const {
  stbuf->st_atime = defaults_.st_atime;
  stbuf->st_mtime = defaults_.st_mtime;
  stbuf->st_ctime = defaults_.st_ctime;
}

template <>
void dir_reader_<dir_entry_ug>::gettimes(const dir_entry*,
                                         struct ::stat* stbuf) const {
  stbuf->st_atime = defaults_.st_atime;
  stbuf->st_mtime = defaults_.st_mtime;
  stbuf->st_ctime = defaults_.st_ctime;
}

template <>
void dir_reader_<dir_entry_ug_time>::gettimes(const dir_entry* de,
                                              struct ::stat* stbuf) const {
  auto real_de = reinterpret_cast<const dir_entry_ug_time*>(de);
  stbuf->st_atime = real_de->atime;
  stbuf->st_mtime = real_de->mtime;
  stbuf->st_ctime = real_de->ctime;
}

std::shared_ptr<dir_reader>
dir_reader::create(dir_entry_type de_type, const struct ::stat& defaults,
                   const char* data, int inode_offset) {
  switch (de_type) {
  case dir_entry_type::DIR_ENTRY:
    return std::make_shared<dir_reader_<dir_entry>>(defaults, data,
                                                    inode_offset);

  case dir_entry_type::DIR_ENTRY_UG:
    return std::make_shared<dir_reader_<dir_entry_ug>>(defaults, data,
                                                       inode_offset);

  case dir_entry_type::DIR_ENTRY_UG_TIME:
    return std::make_shared<dir_reader_<dir_entry_ug_time>>(defaults, data,
                                                            inode_offset);

  default:
    throw std::runtime_error("unknown dir_entry_type");
  }
}

// TODO: move out of here
template <typename LoggerPolicy>
class metadata_ : public metadata::impl {
 public:
  metadata_(logger& lgr, std::vector<uint8_t>&& meta,
            const struct ::stat* defaults, int inode_offset)
      : data_(std::move(meta))
      , inode_offset_(inode_offset)
      , log_(lgr) {
    parse(defaults);
  }

  size_t size() const override { return data_.size(); }

  bool empty() const override { return data_.empty(); }

  size_t block_size() const override {
    return static_cast<size_t>(1) << cfg_->block_size_bits;
  }

  unsigned block_size_bits() const override { return cfg_->block_size_bits; }

  void dump(std::ostream& os,
            std::function<void(const std::string&, uint32_t)> const& icb)
      const override;
  void walk(std::function<void(const dir_entry*)> const& func) const override;
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

 private:
  void dump(std::ostream& os, const std::string& indent, const dir_entry* de,
            std::function<void(const std::string&, uint32_t)> const& icb) const;
  void dump(std::ostream& os, const std::string& indent, const directory* dir,
            std::function<void(const std::string&, uint32_t)> const& icb) const;

  void walk(const dir_entry* de,
            std::function<void(const dir_entry*)> const& func) const;

  std::string modestring(const dir_entry* de) const;

  std::string name(const dir_entry* de) const {
    return std::string(as<char>(de->name_offset), de->name_size);
  }

  size_t filesize(const dir_entry* de) const {
    if (S_ISREG(de->mode)) {
      return de->u.file_size;
    } else if (S_ISLNK(de->mode)) {
      return linksize(de);
    } else {
      return 0;
    }
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

  const directory* getdir(const dir_entry* de) const {
    return as<directory>(de->u.offset);
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

  std::vector<uint8_t> data_;
  const uint32_t* chunk_index_ = nullptr;
  const uint32_t* inode_index_ = nullptr;
  const dir_entry* root_ = nullptr;
  const meta_config* cfg_ = nullptr;
  const int inode_offset_;
  std::shared_ptr<dir_reader> dir_reader_;
  log_proxy<LoggerPolicy> log_;
};

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
std::string metadata_<LoggerPolicy>::modestring(const dir_entry* de) const {
  std::ostringstream oss;

  oss << (de->mode & S_ISUID ? 'U' : '-');
  oss << (de->mode & S_ISGID ? 'G' : '-');
  oss << (de->mode & S_ISVTX ? 'S' : '-');
  oss << (de->mode & S_IRUSR ? 'r' : '-');
  oss << (de->mode & S_IWUSR ? 'w' : '-');
  oss << (de->mode & S_IXUSR ? 'x' : '-');
  oss << (de->mode & S_IRGRP ? 'r' : '-');
  oss << (de->mode & S_IWGRP ? 'w' : '-');
  oss << (de->mode & S_IXGRP ? 'x' : '-');
  oss << (de->mode & S_IROTH ? 'r' : '-');
  oss << (de->mode & S_IWOTH ? 'w' : '-');
  oss << (de->mode & S_IXOTH ? 'x' : '-');

  return oss.str();
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, const dir_entry* de,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  os << indent << "<" << de->inode << ":"
     << (reinterpret_cast<const uint8_t*>(de) - data_.data()) << "> "
     << modestring(de) << " " << name(de);

  if (S_ISREG(de->mode)) {
    os << " " << filesize(de) << "\n";
    icb(indent + "  ", de->inode);
  } else if (S_ISDIR(de->mode)) {
    auto dir = getdir(de);
    os << " => " << (reinterpret_cast<const uint8_t*>(dir) - data_.data())
       << "\n";
    dump(os, indent + "  ", dir, std::move(icb));
  } else if (S_ISLNK(de->mode)) {
    os << " -> " << linkname(de) << "\n";
  } else {
    os << " (unknown type)\n";
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, const directory* dir,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  os << indent << "(" << dir->count << ") entries\n";

  for (size_t i = 0; i < dir->count; ++i) {
    dump(os, indent, dir_reader_->readdir(dir, i), icb);
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  dump(os, "", root_, icb);
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::walk(
    const dir_entry* de,
    std::function<void(const dir_entry*)> const& func) const {
  func(de);
  if (S_ISDIR(de->mode)) {
    auto dir = getdir(de);
    for (size_t i = 0; i < dir->count; ++i) {
      walk(dir_reader_->readdir(dir, i), func);
    }
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::walk(
    std::function<void(const dir_entry*)> const& func) const {
  walk(root_, func);
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

void metadata::get_stat_defaults(struct ::stat* defaults) {
  ::memset(defaults, 0, sizeof(struct ::stat));
  defaults->st_uid = ::geteuid();
  defaults->st_gid = ::getegid();
  time_t t = ::time(nullptr);
  defaults->st_atime = t;
  defaults->st_mtime = t;
  defaults->st_ctime = t;
}

metadata::metadata(logger& lgr, std::vector<uint8_t>&& data,
                   const struct ::stat* defaults, int inode_offset)
    : impl_(make_unique_logging_object<metadata::impl, metadata_,
                                       logger_policies>(
          lgr, std::move(data), defaults, inode_offset)) {}
} // namespace dwarfs

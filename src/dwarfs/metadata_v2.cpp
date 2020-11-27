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

#include <cassert>

#include <unistd.h>

#include "dwarfs/metadata_v2.h"

#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs {

namespace {

const uint16_t READ_ONLY_MASK = ~(S_IWUSR | S_IWGRP | S_IWOTH);

}

std::string_view entry_view::name() const {
  return meta_->names()[name_index()];
}

uint16_t entry_view::mode() const { return meta_->modes()[mode_index()]; }

uint16_t entry_view::getuid() const { return meta_->uids()[owner_index()]; }

uint16_t entry_view::getgid() const { return meta_->gids()[group_index()]; }

boost::integer_range<uint32_t> directory_view::entry_range() const {
  auto first = first_entry();
  return boost::irange(first, first + entry_count());
}

uint32_t directory_view::self_inode() {
  auto pos = getPosition().bitOffset;
  if (pos > 0) {
    // XXX: this is evil trickery...
    auto one = meta_->directories()[1].getPosition().bitOffset;
    assert(pos % one == 0);
    pos /= one;
  }
  return pos;
}

template <typename LoggerPolicy>
class metadata_v2_ : public metadata_v2::impl {
 public:
  // TODO: pass folly::ByteRange instead of vector (so we can support memory
  // mapping)
  metadata_v2_(logger& lgr, std::vector<uint8_t>&& meta,
               const struct ::stat* /*defaults*/, int inode_offset)
      : data_(std::move(meta))
      , meta_(::apache::thrift::frozen::mapFrozen<thrift::metadata::metadata>(
            data_))
      , root_(meta_.entries()[meta_.entry_index()[0]], &meta_)
      , inode_offset_(inode_offset)
      , chunk_index_offset_(meta_.chunk_index_offset())
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

  std::optional<entry_view> find(const char* path) const override;
  std::optional<entry_view> find(int inode) const override;
  std::optional<entry_view> find(int inode, const char* name) const override;

  int getattr(entry_view entry, struct ::stat* stbuf) const override;

  std::optional<directory_view> opendir(entry_view entry) const override;

  std::optional<std::pair<entry_view, std::string_view>>
  readdir(directory_view dir, size_t offset) const override;

  size_t dirsize(directory_view dir) const override {
    return 2 + dir.entry_count(); // adds '.' and '..', which we fake in ;-)
  }

  int access(entry_view entry, int mode, uid_t uid, gid_t gid) const override;

  int open(entry_view entry) const override;

  int readlink(entry_view entry, std::string* buf) const override;

  folly::Expected<std::string_view, int>
  readlink(entry_view entry) const override;

#if 0
  size_t block_size() const override {
    return static_cast<size_t>(1) << cfg_->block_size_bits;
  }

  unsigned block_size_bits() const override { return cfg_->block_size_bits; }

  int statvfs(struct ::statvfs* stbuf) const override;

  const chunk_type* get_chunks(int inode, size_t& num) const override;
#endif

 private:
  entry_view make_entry_view(size_t index) const {
    return entry_view(meta_.entries()[index], &meta_);
  }

  entry_view make_entry_view_from_inode(uint32_t inode) const {
    return make_entry_view(meta_.entry_index()[inode]);
  }

  directory_view make_directory_view(size_t index) const {
    return directory_view(meta_.directories()[index], &meta_);
  }

  void dump(std::ostream& os, const std::string& indent, entry_view entry,
            std::function<void(const std::string&, uint32_t)> const& icb) const;
  void dump(std::ostream& os, const std::string& indent, directory_view dir,
            std::function<void(const std::string&, uint32_t)> const& icb) const;

  std::optional<entry_view>
  find(directory_view dir, std::string_view name) const;

  std::string modestring(uint16_t mode) const;

  size_t reg_file_size(entry_view entry) const {
    auto inode = entry.inode() - chunk_index_offset_;
    uint32_t cur = meta_.chunk_index()[inode];
    uint32_t end = meta_.chunk_index()[inode + 1];
    size_t size = 0;
    while (cur < end) {
      size += meta_.chunks()[cur++].size();
    }
    return size;
  }

  size_t file_size(entry_view entry, uint16_t mode) const {
    if (S_ISREG(mode)) {
      return reg_file_size(entry);
    } else if (S_ISLNK(mode)) {
      return link_value(entry).size();
    } else {
      return 0;
    }
  }

  directory_view getdir(entry_view entry) const {
    return make_directory_view(entry.inode());
  }

  void
  walk(entry_view entry, std::function<void(entry_view)> const& func) const;

  std::optional<entry_view> get_entry(int inode) const {
    inode -= inode_offset_;
    std::optional<entry_view> rv;
    if (inode >= 0 && inode < int(meta_.entry_index().size())) {
      rv = make_entry_view_from_inode(inode);
    }
    return rv;
  }

  std::string_view link_value(entry_view entry) const {
    return meta_
        .links()[meta_.link_index()[entry.inode()] - meta_.link_index_offset()];
  }

#if 0
  const char* linkptr(entry_view entry) const {
    return as<char>(entry->u.offset + sizeof(uint16_t));
  }
#endif

  std::vector<uint8_t> data_;
  ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata> meta_;
  entry_view root_;
  const int inode_offset_;
  const int chunk_index_offset_;
  log_proxy<LoggerPolicy> log_;
};

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, entry_view entry,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto mode = entry.mode();
  auto inode = entry.inode();

  os << indent << "<inode:" << inode << "> " << modestring(mode);

  if (inode > 0) {
    os << " " << entry.name();
  }

  if (S_ISREG(mode)) {
    uint32_t beg = meta_.chunk_index()[inode - chunk_index_offset_];
    uint32_t end = meta_.chunk_index()[inode - chunk_index_offset_ + 1];
    os << " [" << beg << ", " << end << "]";
    os << " " << file_size(entry, mode) << "\n";
    icb(indent + "  ", inode);
  } else if (S_ISDIR(mode)) {
    dump(os, indent + "  ", make_directory_view(inode), std::move(icb));
  } else if (S_ISLNK(mode)) {
    os << " -> " << link_value(entry) << "\n";
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

  os << indent << "(" << count << ") entries [" << dir.self_inode() << ":"
     << dir.parent_inode() << "]\n";

  for (size_t i = 0; i < count; ++i) {
    dump(os, indent, make_entry_view(first + i), icb);
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
    entry_view entry, std::function<void(entry_view)> const& func) const {
  func(entry);
  if (S_ISDIR(entry.mode())) {
    auto dir = getdir(entry);
    for (auto cur : dir.entry_range()) {
      walk(make_entry_view(cur), func);
    }
  }
}

template <typename LoggerPolicy>
void metadata_v2_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  walk(root_, func);
}

template <typename LoggerPolicy>
std::optional<entry_view>
metadata_v2_<LoggerPolicy>::find(directory_view dir,
                                 std::string_view name) const {
  auto range = dir.entry_range();

  auto it = std::lower_bound(range.begin(), range.end(), name,
                             [&](auto ix, std::string_view name) {
                               return make_entry_view(ix).name() < name;
                             });

  std::optional<entry_view> rv;

  if (it != range.end()) {
    auto cand = make_entry_view(*it);

    if (cand.name() == name) {
      rv = cand;
    }
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<entry_view>
metadata_v2_<LoggerPolicy>::find(const char* path) const {
  while (*path and *path == '/') {
    ++path;
  }

  std::optional<entry_view> entry = root_;

  while (*path) {
    const char* next = ::strchr(path, '/');
    size_t clen = next ? next - path : ::strlen(path);

    entry = find(getdir(*entry), std::string_view(path, clen));

    if (!entry) {
      break;
    }

    path = next ? next + 1 : path + clen;
  }

  return entry;
}

template <typename LoggerPolicy>
std::optional<entry_view> metadata_v2_<LoggerPolicy>::find(int inode) const {
  return get_entry(inode);
}

template <typename LoggerPolicy>
std::optional<entry_view>
metadata_v2_<LoggerPolicy>::find(int inode, const char* name) const {
  auto entry = get_entry(inode);

  if (entry) {
    entry = find(getdir(*entry), std::string_view(name));
  }

  return entry;
}

template <typename LoggerPolicy>
int metadata_v2_<LoggerPolicy>::getattr(entry_view entry,
                                        struct ::stat* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  auto mode = entry.mode();
  auto timebase = meta_.timestamp_base();

  stbuf->st_mode = mode & READ_ONLY_MASK;
  stbuf->st_size = file_size(entry, mode);
  stbuf->st_ino = entry.inode() + inode_offset_;
  stbuf->st_blocks = (stbuf->st_size + 511) / 512;
  stbuf->st_uid = entry.getuid();
  stbuf->st_gid = entry.getgid();
  stbuf->st_atime = timebase + entry.atime_offset();
  stbuf->st_mtime = timebase + entry.mtime_offset();
  stbuf->st_ctime = timebase + entry.ctime_offset();

  return 0;
}

template <typename LoggerPolicy>
std::optional<directory_view>
metadata_v2_<LoggerPolicy>::opendir(entry_view entry) const {
  std::optional<directory_view> rv;

  if (S_ISDIR(entry.mode())) {
    rv = getdir(entry);
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<std::pair<entry_view, std::string_view>>
metadata_v2_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
  switch (offset) {
  case 0:
    return std::pair(make_entry_view_from_inode(dir.self_inode()), ".");

  case 1:
    return std::pair(make_entry_view_from_inode(dir.parent_inode()), "..");

  default:
    offset -= 2;

    if (offset >= dir.entry_count()) {
      break;
    }

    auto entry = make_entry_view(dir.first_entry() + offset);

    return std::pair(entry, entry.name());
  }

  return std::nullopt;
}

template <typename LoggerPolicy>
int metadata_v2_<LoggerPolicy>::access(entry_view entry, int mode, uid_t uid,
                                       gid_t gid) const {
  if (mode == F_OK) {
    // easy; we're only interested in the file's existance
    return 0;
  }

  int access_mode = 0;
  int e_mode = entry.mode();

  auto test = [e_mode, &access_mode](uint16_t r_bit, uint16_t x_bit) {
    if (e_mode & r_bit) {
      access_mode |= R_OK;
    }
    if (e_mode & x_bit) {
      access_mode |= X_OK;
    }
  };

  // Let's build the entry's access mask
  test(S_IROTH, S_IXOTH);

  if (entry.getgid() == gid) {
    test(S_IRGRP, S_IXGRP);
  }

  if (entry.getuid() == uid) {
    test(S_IRUSR, S_IXUSR);
  }

  return (access_mode & mode) == mode ? 0 : EACCES;
}

template <typename LoggerPolicy>
int metadata_v2_<LoggerPolicy>::open(entry_view entry) const {
  if (S_ISREG(entry.mode())) {
    return entry.inode();
  }

  return -1;
}

template <typename LoggerPolicy>
int metadata_v2_<LoggerPolicy>::readlink(entry_view entry,
                                         std::string* buf) const {
  if (S_ISLNK(entry.mode())) {
    buf->assign(link_value(entry));
    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
folly::Expected<std::string_view, int>
metadata_v2_<LoggerPolicy>::readlink(entry_view entry) const {
  if (S_ISLNK(entry.mode())) {
    return link_value(entry);
  }

  return folly::makeUnexpected(-EINVAL);
}
#if 0

template <typename LoggerPolicy>
int metadata_v2_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
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
metadata_v2_<LoggerPolicy>::get_chunks(int inode, size_t& num) const {
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
                         const struct ::stat* defaults, int inode_offset)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_v2_,
                                       logger_policies>(
          lgr, std::move(data), defaults, inode_offset)) {}
} // namespace dwarfs

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
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs {

namespace {

template <class T>
std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
freeze_to_buffer(const T& x) {
  using namespace ::apache::thrift::frozen;

  Layout<T> layout;
  size_t content_size = LayoutRoot::layout(x, layout);

  std::string schema;
  serializeRootLayout(layout, schema);

  size_t schema_size = schema.size();
  auto schema_begin = reinterpret_cast<uint8_t const*>(schema.data());
  std::vector<uint8_t> schema_buffer(schema_begin, schema_begin + schema_size);

  std::vector<uint8_t> data_buffer;
  data_buffer.resize(content_size, 0);

  folly::MutableByteRange content_range(data_buffer.data(), data_buffer.size());
  ByteRangeFreezer::freeze(layout, x, content_range);

  data_buffer.resize(data_buffer.size() - content_range.size());

  return {schema_buffer, data_buffer};
}

template <class T>
::apache::thrift::frozen::MappedFrozen<T>
map_frozen(folly::ByteRange schema, folly::ByteRange data) {
  using namespace ::apache::thrift::frozen;
  auto layout = std::make_unique<Layout<T>>();
  deserializeRootLayout(schema, *layout);
  MappedFrozen<T> ret(layout->view({data.begin(), 0}));
  ret.hold(std::move(layout));
  return ret;
}

const uint16_t READ_ONLY_MASK = ~(S_IWUSR | S_IWGRP | S_IWOTH);

template <typename LoggerPolicy>
class metadata_ : public metadata_v2::impl {
 public:
  metadata_(logger& lgr, folly::ByteRange schema, folly::ByteRange data,
            const struct ::stat* /*defaults*/, int inode_offset)
      : data_(data)
      , meta_(map_frozen<thrift::metadata::metadata>(schema, data_))
      , root_(meta_.entries()[meta_.entry_index()[0]], &meta_)
      , inode_offset_(inode_offset)
      , chunk_index_offset_(meta_.chunk_index_offset())
      , log_(lgr) {
    // TODO: defaults?, remove
    log_.debug() << ::apache::thrift::debugString(meta_.thaw());

    ::apache::thrift::frozen::Layout<thrift::metadata::metadata> layout;
    ::apache::thrift::frozen::schema::Schema mem_schema;
    ::apache::thrift::CompactSerializer::deserialize(schema, mem_schema);
    log_.debug() << ::apache::thrift::debugString(mem_schema);
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

  int statvfs(struct ::statvfs* stbuf) const override;

  std::optional<chunk_range> get_chunks(int inode) const override;

  size_t block_size() const override { return meta_.block_size(); }

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
    if (inode >= 0 && inode < static_cast<int>(meta_.entry_index().size())) {
      rv = make_entry_view_from_inode(inode);
    }
    return rv;
  }

  std::string_view link_value(entry_view entry) const {
    return meta_
        .links()[meta_.link_index()[entry.inode()] - meta_.link_index_offset()];
  }

  folly::ByteRange data_;
  ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata> meta_;
  entry_view root_;
  const int inode_offset_;
  const int chunk_index_offset_;
  log_proxy<LoggerPolicy> log_;
};

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
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
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, directory_view dir,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto count = dir.entry_count();
  auto first = dir.first_entry();

  os << "(" << count << " entries) [" << dir.self_inode() << ", "
     << dir.parent_inode() << "]\n";

  for (size_t i = 0; i < count; ++i) {
    dump(os, indent, make_entry_view(first + i), icb);
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  dump(os, "", root_, icb);
}

template <typename LoggerPolicy>
std::string metadata_<LoggerPolicy>::modestring(uint16_t mode) const {
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
void metadata_<LoggerPolicy>::walk(
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
void metadata_<LoggerPolicy>::walk(
    std::function<void(entry_view)> const& func) const {
  walk(root_, func);
}

template <typename LoggerPolicy>
std::optional<entry_view>
metadata_<LoggerPolicy>::find(directory_view dir, std::string_view name) const {
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
metadata_<LoggerPolicy>::find(const char* path) const {
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
std::optional<entry_view> metadata_<LoggerPolicy>::find(int inode) const {
  return get_entry(inode);
}

template <typename LoggerPolicy>
std::optional<entry_view>
metadata_<LoggerPolicy>::find(int inode, const char* name) const {
  auto entry = get_entry(inode);

  if (entry) {
    entry = find(getdir(*entry), std::string_view(name));
  }

  return entry;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::getattr(entry_view entry,
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
metadata_<LoggerPolicy>::opendir(entry_view entry) const {
  std::optional<directory_view> rv;

  if (S_ISDIR(entry.mode())) {
    rv = getdir(entry);
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<std::pair<entry_view, std::string_view>>
metadata_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
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
int metadata_<LoggerPolicy>::access(entry_view entry, int mode, uid_t uid,
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
int metadata_<LoggerPolicy>::open(entry_view entry) const {
  if (S_ISREG(entry.mode())) {
    return entry.inode();
  }

  return -1;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::readlink(entry_view entry,
                                      std::string* buf) const {
  if (S_ISLNK(entry.mode())) {
    buf->assign(link_value(entry));
    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
folly::Expected<std::string_view, int>
metadata_<LoggerPolicy>::readlink(entry_view entry) const {
  if (S_ISLNK(entry.mode())) {
    return link_value(entry);
  }

  return folly::makeUnexpected(-EINVAL);
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  stbuf->f_bsize = meta_.block_size();
  stbuf->f_frsize = 1UL;
  stbuf->f_blocks = meta_.total_fs_size();
  stbuf->f_files = meta_.entry_index().size();
  stbuf->f_flag = ST_RDONLY;
  stbuf->f_namemax = PATH_MAX;

  return 0;
}

template <typename LoggerPolicy>
std::optional<chunk_range>
metadata_<LoggerPolicy>::get_chunks(int inode) const {
  std::optional<chunk_range> rv;
  inode -= inode_offset_ + meta_.chunk_index_offset();
  if (inode >= 0 &&
      inode < (static_cast<int>(meta_.chunk_index().size()) - 1)) {
    uint32_t begin = meta_.chunk_index()[inode];
    uint32_t end = meta_.chunk_index()[inode + 1];
    rv = chunk_range(&meta_, begin, end);
  }
  return rv;
}

} // namespace

void metadata_v2::get_stat_defaults(struct ::stat* defaults) {
  ::memset(defaults, 0, sizeof(struct ::stat));
  defaults->st_uid = ::geteuid();
  defaults->st_gid = ::getegid();
  time_t t = ::time(nullptr);
  defaults->st_atime = t;
  defaults->st_mtime = t;
  defaults->st_ctime = t;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
metadata_v2::freeze(const thrift::metadata::metadata& data) {
  return freeze_to_buffer(data);
}

metadata_v2::metadata_v2(logger& lgr, folly::ByteRange schema,
                         folly::ByteRange data, const struct ::stat* defaults,
                         int inode_offset)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_,
                                       logger_policies>(
          lgr, schema, data, defaults, inode_offset)) {}

} // namespace dwarfs

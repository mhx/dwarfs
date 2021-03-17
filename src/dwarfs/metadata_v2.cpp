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
#include <cerrno>
#include <climits>
#include <cstring>
#include <numeric>
#include <ostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/format.h>

#include <folly/container/F14Set.h>

#include "dwarfs/error.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/options.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"
#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"

namespace dwarfs {

namespace {

using ::apache::thrift::frozen::MappedFrozen;

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

template <typename T>
MappedFrozen<T> map_frozen(folly::ByteRange schema, folly::ByteRange data) {
  using namespace ::apache::thrift::frozen;
  auto layout = std::make_unique<Layout<T>>();
  deserializeRootLayout(schema, *layout);
  MappedFrozen<T> ret(layout->view({data.begin(), 0}));
  ret.hold(std::move(layout));
  return ret;
}

void analyze_frozen(std::ostream& os,
                    MappedFrozen<thrift::metadata::metadata> const& meta) {
  using namespace ::apache::thrift::frozen;
  auto layout = meta.findFirstOfType<
      std::unique_ptr<Layout<thrift::metadata::metadata>>>();
  (*layout)->print(os, 0);
  os << '\n';
}

const uint16_t READ_ONLY_MASK = ~(S_IWUSR | S_IWGRP | S_IWOTH);

} // namespace

template <typename LoggerPolicy>
class metadata_ final : public metadata_v2::impl {
 public:
  // TODO: defaults?, remove
  metadata_(logger& lgr, folly::ByteRange schema, folly::ByteRange data,
            metadata_options const& options, struct ::stat const* /*defaults*/,
            int inode_offset)
      : data_(data)
      , meta_(map_frozen<thrift::metadata::metadata>(schema, data_))
      , root_(dir_entry_view::from_dir_entry_index(0, &meta_))
      , log_(lgr)
      , inode_offset_(inode_offset)
      , symlink_table_offset_(find_index_offset(inode_rank::INO_LNK))
      , file_index_offset_(find_index_offset(inode_rank::INO_REG))
      , dev_index_offset_(find_index_offset(inode_rank::INO_DEV))
      , inode_count_(meta_.dir_entries() ? meta_.entries().size()
                                         : meta_.entry_table_v2_2().size())
      , nlinks_(build_nlinks(options))
      , options_(options) {
    LOG_DEBUG << "inode count: " << inode_count_;
    LOG_DEBUG << "symlink table offset: " << symlink_table_offset_;
    LOG_DEBUG << "chunk index offset: " << file_index_offset_;
    LOG_DEBUG << "device index offset: " << dev_index_offset_;

    if (int(meta_.directories().size() - 1) != symlink_table_offset_) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("metadata inconsistency: number of directories ({}) does "
                      "not match link index ({})",
                      meta_.directories().size() - 1, symlink_table_offset_));
    }

    if (int(meta_.symlink_table().size()) !=
        (file_index_offset_ - symlink_table_offset_)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format(
              "metadata inconsistency: number of symlinks ({}) does not match "
              "chunk/symlink table delta ({} - {} = {})",
              meta_.symlink_table().size(), file_index_offset_,
              symlink_table_offset_,
              file_index_offset_ - symlink_table_offset_));
    }

    // TODO: this might be a silly check for v2.3
    //
    // if (int(meta_.chunk_table().size() - 1) !=
    //     (dev_index_offset_ - file_index_offset_)) {
    //   DWARFS_THROW(
    //       runtime_error,
    //       fmt::format(
    //           "metadata inconsistency: number of files ({}) does not match "
    //           "device/chunk index delta ({} - {} = {})",
    //           meta_.chunk_table().size() - 1, dev_index_offset_,
    //           file_index_offset_, dev_index_offset_ - file_index_offset_));
    // }

    if (auto devs = meta_.devices()) {
      auto other_offset = find_index_offset(inode_rank::INO_OTH);

      if (devs->size() != (other_offset - dev_index_offset_)) {
        DWARFS_THROW(
            runtime_error,
            fmt::format("metadata inconsistency: number of devices ({}) does "
                        "not match other/device index delta ({} - {} = {})",
                        devs->size(), other_offset, dev_index_offset_,
                        other_offset - dev_index_offset_));
      }
    }
  }

  void dump(std::ostream& os, int detail_level,
            std::function<void(const std::string&, uint32_t)> const& icb)
      const override;

  folly::dynamic as_dynamic() const override;
  std::string serialize_as_json(bool simple) const override;

  size_t size() const override { return data_.size(); }

  bool empty() const override { return data_.empty(); }

  void walk(std::function<void(dir_entry_view)> const& func) const override {
    walk_tree([&](uint32_t self_index, uint32_t parent_index) {
      walk_call(func, self_index, parent_index);
    });
  }

  void walk_inode_order(
      std::function<void(dir_entry_view)> const& func) const override {
    walk_inode_order_impl(func);
  }

  std::optional<inode_view> find(const char* path) const override;
  std::optional<inode_view> find(int inode) const override;
  std::optional<inode_view> find(int inode, const char* name) const override;

  int getattr(inode_view entry, struct ::stat* stbuf) const override;

  std::optional<directory_view> opendir(inode_view entry) const override;

  std::optional<std::pair<inode_view, std::string_view>>
  readdir(directory_view dir, size_t offset) const override;

  size_t dirsize(directory_view dir) const override {
    return 2 + dir.entry_count(); // adds '.' and '..', which we fake in ;-)
  }

  int access(inode_view entry, int mode, uid_t uid, gid_t gid) const override;

  int open(inode_view entry) const override;

  int readlink(inode_view entry, std::string* buf) const override;

  folly::Expected<std::string_view, int>
  readlink(inode_view entry) const override;

  int statvfs(struct ::statvfs* stbuf) const override;

  std::optional<chunk_range> get_chunks(int inode) const override;

  size_t block_size() const override { return meta_.block_size(); }

 private:
  template <typename K>
  using set_type = folly::F14ValueSet<K>;

  inode_view make_inode_view(uint32_t inode) const {
    // TODO: move compatibility details to metadata_types
    uint32_t index =
        meta_.dir_entries() ? inode : meta_.entry_table_v2_2()[inode];
    return inode_view(meta_.entries()[index], inode, &meta_);
  }

  dir_entry_view
  make_dir_entry_view(uint32_t self_index, uint32_t parent_index) const {
    return dir_entry_view::from_dir_entry_index(self_index, parent_index,
                                                &meta_);
  }

  // This represents the order in which inodes are stored in entry_table_v2_2
  enum class inode_rank {
    INO_DIR,
    INO_LNK,
    INO_REG,
    INO_DEV,
    INO_OTH,
  };

  static inode_rank get_inode_rank(uint16_t mode) {
    switch ((mode)&S_IFMT) {
    case S_IFDIR:
      return inode_rank::INO_DIR;
    case S_IFLNK:
      return inode_rank::INO_LNK;
    case S_IFREG:
      return inode_rank::INO_REG;
    case S_IFBLK:
    case S_IFCHR:
      return inode_rank::INO_DEV;
    case S_IFSOCK:
    case S_IFIFO:
      return inode_rank::INO_OTH;
    default:
      DWARFS_THROW(runtime_error,
                   fmt::format("unknown file type: {:#06x}", mode));
    }
  }

  static char get_filetype_label(uint16_t mode) {
    switch ((mode)&S_IFMT) {
    case S_IFDIR:
      return 'd';
    case S_IFLNK:
      return 'l';
    case S_IFREG:
      return '-';
    case S_IFBLK:
      return 'b';
    case S_IFCHR:
      return 'c';
    case S_IFSOCK:
      return 's';
    case S_IFIFO:
      return 'p';
    default:
      DWARFS_THROW(runtime_error,
                   fmt::format("unknown file type: {:#06x}", mode));
    }
  }

  size_t find_index_offset(inode_rank rank) const {
    if (meta_.dir_entries()) {
      auto range = boost::irange(size_t(0), meta_.entries().size());

      auto it = std::lower_bound(
          range.begin(), range.end(), rank, [&](auto inode, inode_rank r) {
            auto mode = meta_.modes()[meta_.entries()[inode].mode_index()];
            return get_inode_rank(mode) < r;
          });

      return *it;
    } else {
      auto range = boost::irange(size_t(0), meta_.entry_table_v2_2().size());

      auto it = std::lower_bound(range.begin(), range.end(), rank,
                                 [&](auto inode, inode_rank r) {
                                   auto e = make_inode_view(inode);
                                   return get_inode_rank(e.mode()) < r;
                                 });

      return *it;
    }
  }

  directory_view make_directory_view(inode_view inode) const {
    // TODO: revisit: is this the way to do it?
    return directory_view(inode.inode_num(), &meta_);
  }

  void dump(std::ostream& os, const std::string& indent, dir_entry_view entry,
            int detail_level,
            std::function<void(const std::string&, uint32_t)> const& icb) const;
  void dump(std::ostream& os, const std::string& indent, directory_view dir,
            dir_entry_view entry, int detail_level,
            std::function<void(const std::string&, uint32_t)> const& icb) const;

  folly::dynamic as_dynamic(dir_entry_view entry) const;
  folly::dynamic as_dynamic(directory_view dir, dir_entry_view entry) const;

  std::optional<inode_view>
  find(directory_view dir, std::string_view name) const;

  std::string modestring(uint16_t mode) const;

  std::optional<chunk_range> get_chunk_range(int inode) const {
    std::optional<chunk_range> rv;

    inode -= file_index_offset_;

    if (auto uf = meta_.unique_files_table()) {
      if (inode < 0 or inode >= static_cast<int>(uf->size())) {
        return rv;
      }

      inode = (*uf)[inode];
    }

    if (inode >= 0 &&
        inode < (static_cast<int>(meta_.chunk_table().size()) - 1)) {
      uint32_t begin = meta_.chunk_table()[inode];
      uint32_t end = meta_.chunk_table()[inode + 1];
      rv = chunk_range(&meta_, begin, end);
    }

    return rv;
  }

  size_t reg_file_size(inode_view entry) const {
    auto cr = get_chunk_range(entry.inode_num());
    DWARFS_CHECK(cr, "invalid chunk range");
    return std::accumulate(
        cr->begin(), cr->end(), static_cast<size_t>(0),
        [](size_t s, chunk_view cv) { return s + cv.size(); });
  }

  size_t file_size(inode_view entry, uint16_t mode) const {
    if (S_ISREG(mode)) {
      return reg_file_size(entry);
    } else if (S_ISLNK(mode)) {
      return link_value(entry).size();
    } else {
      return 0;
    }
  }

  void walk_call(std::function<void(dir_entry_view)> const& func,
                 uint32_t self_index, uint32_t parent_index) const {
    func(make_dir_entry_view(self_index, parent_index));
  }

  template <typename T>
  void walk(uint32_t self_index, uint32_t parent_index, set_type<int>& seen,
            T&& func) const;

  template <typename T>
  void walk_tree(T&& func) const {
    set_type<int> seen;
    walk(0, 0, seen, std::forward<T>(func));
  }

  void
  walk_inode_order_impl(std::function<void(dir_entry_view)> const& func) const;

  std::optional<inode_view> get_entry(int inode) const {
    inode -= inode_offset_;
    std::optional<inode_view> rv;
    if (inode >= 0 && inode < inode_count_) {
      rv = make_inode_view(inode);
    }
    return rv;
  }

  std::string_view link_value(inode_view entry) const {
    return meta_.symlinks()[meta_.symlink_table()[entry.inode_num() -
                                                  symlink_table_offset_]];
  }

  uint64_t get_device_id(int inode) const {
    if (auto devs = meta_.devices()) {
      return (*devs)[inode - dev_index_offset_];
    }
    LOG_ERROR << "get_device_id() called, but no devices in file system";
    return 0;
  }

  std::vector<uint32_t> build_nlinks(metadata_options const& options) const {
    std::vector<uint32_t> nlinks;

    if (options.enable_nlink) {
      auto ti = LOG_TIMED_DEBUG;

      nlinks.resize(dev_index_offset_ - file_index_offset_);

      if (auto de = meta_.dir_entries()) {
        for (auto e : *de) {
          auto index = int(e.inode_num()) - file_index_offset_;
          if (index >= 0 && index < int(nlinks.size())) {
            ++DWARFS_NOTHROW(nlinks.at(index));
          }
        }
      } else {
        for (auto e : meta_.entries()) {
          auto index = int(e.inode_v2_2()) - file_index_offset_;
          if (index >= 0 && index < int(nlinks.size())) {
            ++DWARFS_NOTHROW(nlinks.at(index));
          }
        }
      }

      ti << "build hardlink table (" << sizeof(uint32_t) * nlinks.capacity()
         << " bytes)";
    }

    return nlinks;
  }

  folly::ByteRange data_;
  MappedFrozen<thrift::metadata::metadata> meta_;
  dir_entry_view root_;
  log_proxy<LoggerPolicy> log_;
  const int inode_offset_;
  const int symlink_table_offset_;
  const int file_index_offset_;
  const int dev_index_offset_;
  const int inode_count_;
  const std::vector<uint32_t> nlinks_;
  const metadata_options options_;
};

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, dir_entry_view entry,
    int detail_level,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto inode_data = entry.inode();
  auto mode = inode_data.mode();
  auto inode = inode_data.inode_num(); // TODO: rename inode appropriately

  os << indent << "<inode:" << inode << "> " << modestring(mode);

  if (inode > 0) {
    os << " " << entry.name();
  }

  if (S_ISREG(mode)) {
    auto cr = get_chunk_range(inode);
    DWARFS_CHECK(cr, "invalid chunk range");
    os << " [" << cr->begin_ << ", " << cr->end_ << "]";
    os << " " << file_size(inode_data, mode) << "\n";
    if (detail_level > 3) {
      icb(indent + "  ", inode);
    }
  } else if (S_ISDIR(mode)) {
    dump(os, indent + "  ", make_directory_view(inode_data), entry,
         detail_level, std::move(icb));
  } else if (S_ISLNK(mode)) {
    os << " -> " << link_value(inode_data) << "\n";
  } else if (S_ISBLK(mode)) {
    os << " (block device: " << get_device_id(inode) << ")\n";
  } else if (S_ISCHR(mode)) {
    os << " (char device: " << get_device_id(inode) << ")\n";
  } else if (S_ISFIFO(mode)) {
    os << " (named pipe)\n";
  } else if (S_ISSOCK(mode)) {
    os << " (socket)\n";
  }
}

// TODO: can we move this to dir_entry_view?
template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, directory_view dir,
    dir_entry_view entry, int detail_level,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto count = dir.entry_count();
  auto first = dir.first_entry();

  os << " (" << count << " entries, parent=" << dir.parent_entry() << ")\n";

  for (size_t i = 0; i < count; ++i) {
    dump(os, indent, make_dir_entry_view(first + i, entry.self_index()),
         detail_level, icb);
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, int detail_level,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  struct ::statvfs stbuf;
  statvfs(&stbuf);

  if (detail_level > 0) {
    os << "block size: " << stbuf.f_bsize << std::endl;
    os << "inode count: " << stbuf.f_files << std::endl;
    os << "original filesystem size: " << stbuf.f_blocks << std::endl;
  }

  if (detail_level > 1) {
    os << "chunks: " << meta_.chunks().size() << std::endl;
    os << "directories: " << meta_.directories().size() << std::endl;
    os << "entries: " << meta_.entries().size() << std::endl;
    os << "chunk_table: " << meta_.chunk_table().size() << std::endl;
    os << "entry_table_v2_2: " << meta_.entry_table_v2_2().size() << std::endl;
    os << "symlink_table: " << meta_.symlink_table().size() << std::endl;
    os << "uids: " << meta_.uids().size() << std::endl;
    os << "gids: " << meta_.gids().size() << std::endl;
    os << "modes: " << meta_.modes().size() << std::endl;
    os << "names: " << meta_.names().size() << std::endl;
    os << "symlinks: " << meta_.symlinks().size() << std::endl;
    os << "hardlinks: " << std::accumulate(nlinks_.begin(), nlinks_.end(), 0)
       << std::endl;
    if (auto dev = meta_.devices()) {
      os << "devices: " << dev->size() << std::endl;
    }
    if (auto de = meta_.dir_entries()) {
      os << "dir_entries: " << de->size() << std::endl;
    }
    if (auto uf = meta_.unique_files_table()) {
      os << "unique_files_table: " << uf->size() << std::endl;
    }
    os << "symlink_table_offset: " << symlink_table_offset_ << std::endl;
    os << "file_index_offset: " << file_index_offset_ << std::endl;
    os << "dev_index_offset: " << dev_index_offset_ << std::endl;

    analyze_frozen(os, meta_);
  }

  if (detail_level > 4) {
    os << ::apache::thrift::debugString(meta_.thaw()) << '\n';
  }

  if (detail_level > 2) {
    dump(os, "", root_, detail_level, icb);
  }
}

template <typename LoggerPolicy>
folly::dynamic metadata_<LoggerPolicy>::as_dynamic(directory_view dir,
                                                   dir_entry_view entry) const {
  folly::dynamic obj = folly::dynamic::array;

  auto count = dir.entry_count();
  auto first = dir.first_entry();

  for (size_t i = 0; i < count; ++i) {
    obj.push_back(
        as_dynamic(make_dir_entry_view(first + i, entry.self_index())));
  }

  return obj;
}

template <typename LoggerPolicy>
folly::dynamic metadata_<LoggerPolicy>::as_dynamic(dir_entry_view entry) const {
  folly::dynamic obj = folly::dynamic::object;

  auto inode_data = entry.inode();
  auto mode = inode_data.mode();
  auto inode = inode_data.inode_num(); // TODO: rename all the things

  obj["mode"] = mode;
  obj["modestring"] = modestring(mode);
  obj["inode"] = inode;

  if (inode > 0) {
    obj["name"] = std::string(entry.name());
  }

  if (S_ISREG(mode)) {
    obj["type"] = "file";
    obj["size"] = file_size(inode_data, mode);
  } else if (S_ISDIR(mode)) {
    obj["type"] = "directory";
    obj["entries"] = as_dynamic(make_directory_view(inode_data), entry);
  } else if (S_ISLNK(mode)) {
    obj["type"] = "link";
    obj["target"] = std::string(link_value(inode_data));
  } else if (S_ISBLK(mode)) {
    obj["type"] = "blockdev";
    obj["device_id"] = get_device_id(inode);
  } else if (S_ISCHR(mode)) {
    obj["type"] = "chardev";
    obj["device_id"] = get_device_id(inode);
  } else if (S_ISFIFO(mode)) {
    obj["type"] = "fifo";
  } else if (S_ISSOCK(mode)) {
    obj["type"] = "socket";
  }

  return obj;
}

template <typename LoggerPolicy>
folly::dynamic metadata_<LoggerPolicy>::as_dynamic() const {
  folly::dynamic obj = folly::dynamic::object;

  struct ::statvfs stbuf;
  statvfs(&stbuf);

  obj["statvfs"] = folly::dynamic::object("f_bsize", stbuf.f_bsize)(
      "f_files", stbuf.f_files)("f_blocks", stbuf.f_blocks);

  obj["root"] = as_dynamic(root_);

  return obj;
}

template <typename LoggerPolicy>
std::string metadata_<LoggerPolicy>::serialize_as_json(bool simple) const {
  std::string json;
  if (simple) {
    apache::thrift::SimpleJSONSerializer serializer;
    serializer.serialize(meta_.thaw(), &json);
  } else {
    apache::thrift::JSONSerializer serializer;
    serializer.serialize(meta_.thaw(), &json);
  }
  return json;
}

template <typename LoggerPolicy>
std::string metadata_<LoggerPolicy>::modestring(uint16_t mode) const {
  std::ostringstream oss;

  oss << (mode & S_ISUID ? 'U' : '-');
  oss << (mode & S_ISGID ? 'G' : '-');
  oss << (mode & S_ISVTX ? 'S' : '-');
  oss << get_filetype_label(mode);
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
template <typename T>
void metadata_<LoggerPolicy>::walk(uint32_t self_index, uint32_t parent_index,
                                   set_type<int>& seen, T&& func) const {
  func(self_index, parent_index);

  auto entry = make_dir_entry_view(self_index, parent_index);
  auto inode_data = entry.inode();

  if (S_ISDIR(inode_data.mode())) {
    auto inode = inode_data.inode_num();

    if (!seen.emplace(inode).second) {
      DWARFS_THROW(runtime_error, "cycle detected during directory walk");
    }

    auto dir = make_directory_view(inode_data);

    for (auto cur_index : dir.entry_range()) {
      walk(cur_index, self_index, seen, func);
    }

    seen.erase(inode);
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::walk_inode_order_impl(
    std::function<void(dir_entry_view)> const& func) const {
  std::vector<std::pair<uint32_t, uint32_t>> entries;

  {
    auto td = LOG_TIMED_DEBUG;

    walk_tree([&](uint32_t self_index, uint32_t parent_index) {
      entries.emplace_back(self_index, parent_index);
    });

    if (auto dep = meta_.dir_entries()) {
      std::sort(entries.begin(), entries.end(),
                [de = *dep](auto const& a, auto const& b) {
                  return de[a.first].inode_num() < de[b.first].inode_num();
                });
    } else {
      std::sort(entries.begin(), entries.end(),
                [this](auto const& a, auto const& b) {
                  return meta_.entries()[a.first].inode_v2_2() <
                         meta_.entries()[b.first].inode_v2_2();
                });
    }

    td << "ordered " << entries.size() << " entries by inode";
  }

  for (auto [self_index, parent_index] : entries) {
    walk_call(func, self_index, parent_index);
  }
}

template <typename LoggerPolicy>
std::optional<inode_view>
metadata_<LoggerPolicy>::find(directory_view dir, std::string_view name) const {
  auto range = dir.entry_range();

  auto it = std::lower_bound(range.begin(), range.end(), name,
                             [&](auto ix, std::string_view name) {
                               return dir_entry_view::name(ix, &meta_) < name;
                             });

  std::optional<inode_view> rv;

  if (it != range.end()) {
    if (dir_entry_view::name(*it, &meta_) == name) {
      rv = dir_entry_view::inode(*it, &meta_);
    }
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<inode_view>
metadata_<LoggerPolicy>::find(const char* path) const {
  while (*path and *path == '/') {
    ++path;
  }

  std::optional<inode_view> entry = root_.inode();

  while (*path) {
    const char* next = ::strchr(path, '/');
    size_t clen = next ? next - path : ::strlen(path);

    entry = find(make_directory_view(*entry), std::string_view(path, clen));

    if (!entry) {
      break;
    }

    path = next ? next + 1 : path + clen;
  }

  return entry;
}

template <typename LoggerPolicy>
std::optional<inode_view> metadata_<LoggerPolicy>::find(int inode) const {
  return get_entry(inode);
}

template <typename LoggerPolicy>
std::optional<inode_view>
metadata_<LoggerPolicy>::find(int inode, const char* name) const {
  auto entry = get_entry(inode);

  if (entry) {
    entry = find(make_directory_view(*entry), std::string_view(name));
  }

  return entry;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::getattr(inode_view entry,
                                     struct ::stat* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  auto mode = entry.mode();
  auto timebase = meta_.timestamp_base();
  auto inode = entry.inode_num();
  bool mtime_only = meta_.options() && meta_.options()->mtime_only();
  uint32_t resolution = 1;
  if (meta_.options()) {
    if (auto res = meta_.options()->time_resolution_sec()) {
      resolution = *res;
      assert(resolution > 0);
    }
  }

  stbuf->st_mode = mode;

  if (options_.readonly) {
    stbuf->st_mode &= READ_ONLY_MASK;
  }

  stbuf->st_size = S_ISDIR(mode) ? make_directory_view(entry).entry_count()
                                 : file_size(entry, mode);
  stbuf->st_ino = inode + inode_offset_;
  stbuf->st_blocks = (stbuf->st_size + 511) / 512;
  stbuf->st_uid = entry.getuid();
  stbuf->st_gid = entry.getgid();
  stbuf->st_mtime = resolution * (timebase + entry.mtime_offset());
  stbuf->st_atime = mtime_only ? stbuf->st_mtime
                               : resolution * (timebase + entry.atime_offset());
  stbuf->st_ctime = mtime_only ? stbuf->st_mtime
                               : resolution * (timebase + entry.ctime_offset());
  stbuf->st_nlink = options_.enable_nlink && S_ISREG(mode)
                        ? DWARFS_NOTHROW(nlinks_.at(inode - file_index_offset_))
                        : 1;

  if (S_ISBLK(mode) || S_ISCHR(mode)) {
    stbuf->st_rdev = get_device_id(inode);
  }

  return 0;
}

template <typename LoggerPolicy>
std::optional<directory_view>
metadata_<LoggerPolicy>::opendir(inode_view entry) const {
  std::optional<directory_view> rv;

  if (S_ISDIR(entry.mode())) {
    rv = make_directory_view(entry);
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<std::pair<inode_view, std::string_view>>
metadata_<LoggerPolicy>::readdir(directory_view dir, size_t offset) const {
  switch (offset) {
  case 0:
    return std::pair(make_inode_view(dir.inode()), ".");

  case 1:
    return std::pair(make_inode_view(dir.parent_inode()), "..");

  default:
    offset -= 2;

    if (offset >= dir.entry_count()) {
      break;
    }

    auto index = dir.first_entry() + offset;
    auto inode = dir_entry_view::inode(index, &meta_);
    return std::pair(inode, dir_entry_view::name(index, &meta_));
  }

  return std::nullopt;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::access(inode_view entry, int mode, uid_t uid,
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
int metadata_<LoggerPolicy>::open(inode_view entry) const {
  if (S_ISREG(entry.mode())) {
    return entry.inode_num();
  }

  return -1;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::readlink(inode_view entry,
                                      std::string* buf) const {
  if (S_ISLNK(entry.mode())) {
    buf->assign(link_value(entry));
    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
folly::Expected<std::string_view, int>
metadata_<LoggerPolicy>::readlink(inode_view entry) const {
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
  stbuf->f_files = inode_count_;
  stbuf->f_flag = ST_RDONLY;
  stbuf->f_namemax = PATH_MAX;

  return 0;
}

template <typename LoggerPolicy>
std::optional<chunk_range>
metadata_<LoggerPolicy>::get_chunks(int inode) const {
  return get_chunk_range(inode - inode_offset_);
}

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
                         folly::ByteRange data, metadata_options const& options,
                         struct ::stat const* defaults, int inode_offset)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_,
                                       logger_policies>(
          lgr, schema, data, options, defaults, inode_offset)) {}

} // namespace dwarfs

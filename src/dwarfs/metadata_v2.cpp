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
#include <ctime>
#include <numeric>
#include <ostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/format.h>
#include <fmt/locale.h>

#include <folly/container/F14Set.h>

#include <fsst.h>

#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/options.h"
#include "dwarfs/string_table.h"
#include "dwarfs/util.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"
#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"

#include "thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h"

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

void check_schema(folly::ByteRange data) {
  using namespace ::apache::thrift;
  frozen::schema::Schema schema;
  size_t schemaSize = CompactSerializer::deserialize(data, schema);
  // std::cerr << debugString(schema) << '\n';
  if (schemaSize != data.size()) {
    DWARFS_THROW(runtime_error, "invalid schema size");
  }
  if (schema.layouts_ref()->count(*schema.rootLayout_ref()) == 0) {
    DWARFS_THROW(runtime_error, "invalid rootLayout in schema");
  }
  for (auto const& kvl : *schema.layouts_ref()) {
    auto const& layout = kvl.second;
    if (kvl.first >= static_cast<int64_t>(schema.layouts_ref()->size())) {
      DWARFS_THROW(runtime_error, "invalid layout key in schema");
    }
    if (*layout.size_ref() < 0) {
      DWARFS_THROW(runtime_error, "negative size in schema");
    }
    if (*layout.bits_ref() < 0) {
      DWARFS_THROW(runtime_error, "negative bits in schema");
    }
    for (auto const& kvf : *layout.fields_ref()) {
      auto const& field = kvf.second;
      if (schema.layouts_ref()->count(*field.layoutId_ref()) == 0) {
        DWARFS_THROW(runtime_error, "invalid layoutId in field");
      }
    }
  }
}

template <typename T>
MappedFrozen<T> map_frozen(folly::ByteRange schema, folly::ByteRange data) {
  using namespace ::apache::thrift::frozen;
  check_schema(schema);
  auto layout = std::make_unique<Layout<T>>();
  deserializeRootLayout(schema, *layout);
  MappedFrozen<T> ret(layout->view({data.begin(), 0}));
  ret.hold(std::move(layout));
  return ret;
}

void analyze_frozen(std::ostream& os,
                    MappedFrozen<thrift::metadata::metadata> const& meta,
                    size_t total_size, int detail) {
  using namespace ::apache::thrift::frozen;
  std::locale loc("");
  std::ostringstream oss;
  stream_logger lgr(oss);

  auto layout = meta.findFirstOfType<
      std::unique_ptr<Layout<thrift::metadata::metadata>>>();

  auto& l = *layout;
  std::vector<std::pair<size_t, std::string>> usage;

#if FMT_VERSION >= 70000
#define DWARFS_FMT_L "L"
#else
#define DWARFS_FMT_L "n"
#endif

  auto fmt_size = [&](auto const& name, size_t count, size_t size) {
    return fmt::format(loc,
                       "{0:>14" DWARFS_FMT_L "} {1:.<20}{2:.>16" DWARFS_FMT_L
                       "} bytes {3:5.1f}% {4:5.1f} bytes/item\n",
                       count, name, size, 100.0 * size / total_size,
                       count > 0 ? static_cast<double>(size) / count : 0.0);
  };

  auto fmt_detail = [&](auto const& name, size_t count, size_t size,
                        std::string num) {
    return fmt::format(
        loc,
        "               {0:<20}{1:>16" DWARFS_FMT_L "} bytes {2:>6} "
        "{3:5.1f} bytes/item\n",
        name, size, num, count > 0 ? static_cast<double>(size) / count : 0.0);
  };

  auto fmt_detail_pct = [&](auto const& name, size_t count, size_t size) {
    return fmt_detail(name, count, size,
                      fmt::format("{0:5.1f}%", 100.0 * size / total_size));
  };

  auto add_size = [&](auto const& name, size_t count, size_t size) {
    usage.emplace_back(size, fmt_size(name, count, size));
  };

  auto list_size = [&](auto const& list, auto const& field) {
    return (list.size() * field.layout.itemField.layout.bits + 7) / 8;
  };

  auto add_list_size = [&](auto const& name, auto const& list,
                           auto const& field) {
    add_size(name, list.size(), list_size(list, field));
  };

  auto add_string_list_size = [&](auto const& name, auto const& list,
                                  auto const& field) {
    auto count = list.size();
    if (count > 0) {
      auto index_size = list_size(list, field);
      auto data_size = list.back().end() - list.front().begin();
      auto size = index_size + data_size;
      auto fmt = fmt_size(name, count, size) +
                 fmt_detail_pct("|- data", count, data_size) +
                 fmt_detail_pct("'- index", count, index_size);
      usage.emplace_back(size, fmt);
    }
  };

  auto add_string_table_size = [&](auto const& name, auto const& table,
                                   auto const& field) {
    if (auto data_size = table.buffer().size(); data_size > 0) {
      auto dict_size =
          table.symtab() ? table.symtab()->size() : static_cast<size_t>(0);
      auto index_size = list_size(table.index(), field.layout.indexField);
      auto size = index_size + data_size + dict_size;
      auto count = table.index().size() - (table.packed_index() ? 0 : 1);
      auto fmt = fmt_size(name, count, size) +
                 fmt_detail_pct("|- data", count, data_size);
      if (table.symtab()) {
        string_table st(lgr, "tmp", table);
        auto unpacked_size = st.unpacked_size();
        fmt += fmt_detail(
            "|- unpacked", count, unpacked_size,
            fmt::format("{0:5.2f}x",
                        static_cast<double>(unpacked_size) / data_size));
        fmt += fmt_detail_pct("|- dict", count, dict_size);
      }
      fmt += fmt_detail_pct("'- index", count, index_size);
      usage.emplace_back(size, fmt);
    }
  };

#define META_LIST_SIZE(x) add_list_size(#x, meta.x(), l->x##Field)

#define META_STRING_LIST_SIZE(x) add_string_list_size(#x, meta.x(), l->x##Field)

#define META_OPT_LIST_SIZE(x)                                                  \
  do {                                                                         \
    if (auto list = meta.x()) {                                                \
      add_list_size(#x, *list, l->x##Field.layout.valueField);                 \
    }                                                                          \
  } while (0)

#define META_OPT_STRING_TABLE_SIZE(x)                                          \
  do {                                                                         \
    if (auto table = meta.x()) {                                               \
      add_string_table_size(#x, *table, l->x##Field.layout.valueField);        \
    }                                                                          \
  } while (0)

  META_LIST_SIZE(chunks);
  META_LIST_SIZE(directories);
  META_LIST_SIZE(inodes);
  META_LIST_SIZE(chunk_table);
  META_LIST_SIZE(symlink_table);
  META_LIST_SIZE(uids);
  META_LIST_SIZE(gids);
  META_LIST_SIZE(modes);

  META_OPT_LIST_SIZE(devices);
  META_OPT_LIST_SIZE(dir_entries);
  META_OPT_LIST_SIZE(shared_files_table);

  META_OPT_STRING_TABLE_SIZE(compact_names);
  META_OPT_STRING_TABLE_SIZE(compact_symlinks);

  META_STRING_LIST_SIZE(names);
  META_STRING_LIST_SIZE(symlinks);

#undef META_LIST_SIZE
#undef META_STRING_LIST_SIZE
#undef META_OPT_LIST_SIZE
#undef META_OPT_STRING_TABLE_SIZE

  std::sort(usage.begin(), usage.end(), [](auto const& a, auto const& b) {
    return a.first > b.first || (a.first == b.first && a.second < b.second);
  });

  os << "metadata memory usage:\n";
  os << fmt::format(loc,
                    "               {0:.<20}{1:.>16" DWARFS_FMT_L
                    "} bytes       {2:6.1f} bytes/inode\n",
                    "total metadata", total_size,
                    static_cast<double>(total_size) / meta.inodes().size());

#undef DWARFS_FMT_L

  for (auto const& u : usage) {
    os << u.second;
  }

  if (detail > 3) {
    l->print(os, 0);
    os << '\n';
  }
}

const uint16_t READ_ONLY_MASK = ~(S_IWUSR | S_IWGRP | S_IWOTH);

} // namespace

template <typename LoggerPolicy>
class metadata_ final : public metadata_v2::impl {
 public:
  metadata_(logger& lgr, folly::ByteRange schema, folly::ByteRange data,
            metadata_options const& options, int inode_offset,
            bool force_consistency_check)
      : data_(data)
      , meta_(map_frozen<thrift::metadata::metadata>(schema, data_))
      , global_(lgr, &meta_,
                options.check_consistency || force_consistency_check)
      , root_(dir_entry_view::from_dir_entry_index(0, &global_))
      , LOG_PROXY_INIT(lgr)
      , inode_offset_(inode_offset)
      , symlink_inode_offset_(find_inode_offset(inode_rank::INO_LNK))
      , file_inode_offset_(find_inode_offset(inode_rank::INO_REG))
      , dev_inode_offset_(find_inode_offset(inode_rank::INO_DEV))
      , inode_count_(meta_.dir_entries() ? meta_.inodes().size()
                                         : meta_.entry_table_v2_2().size())
      , nlinks_(build_nlinks(options))
      , chunk_table_(unpack_chunk_table())
      , shared_files_(decompress_shared_files())
      , unique_files_(dev_inode_offset_ - file_inode_offset_ -
                      (shared_files_.empty()
                           ? meta_.shared_files_table()
                                 ? meta_.shared_files_table()->size()
                                 : 0
                           : shared_files_.size()))
      , options_(options)
      , symlinks_(meta_.compact_symlinks()
                      ? string_table(lgr, "symlinks", *meta_.compact_symlinks())
                      : string_table(meta_.symlinks())) {
    if (static_cast<int>(meta_.directories().size() - 1) !=
        symlink_inode_offset_) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("metadata inconsistency: number of directories ({}) does "
                      "not match link index ({})",
                      meta_.directories().size() - 1, symlink_inode_offset_));
    }

    if (static_cast<int>(meta_.symlink_table().size()) !=
        (file_inode_offset_ - symlink_inode_offset_)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format(
              "metadata inconsistency: number of symlinks ({}) does not match "
              "chunk/symlink table delta ({} - {} = {})",
              meta_.symlink_table().size(), file_inode_offset_,
              symlink_inode_offset_,
              file_inode_offset_ - symlink_inode_offset_));
    }

    if (!meta_.shared_files_table()) {
      if (static_cast<int>(meta_.chunk_table().size() - 1) !=
          (dev_inode_offset_ - file_inode_offset_)) {
        DWARFS_THROW(
            runtime_error,
            fmt::format(
                "metadata inconsistency: number of files ({}) does not match "
                "device/chunk index delta ({} - {} = {})",
                meta_.chunk_table().size() - 1, dev_inode_offset_,
                file_inode_offset_, dev_inode_offset_ - file_inode_offset_));
      }
    }

    if (auto devs = meta_.devices()) {
      int other_offset = find_inode_offset(inode_rank::INO_OTH);

      if (static_cast<int>(devs->size()) !=
          (other_offset - dev_inode_offset_)) {
        DWARFS_THROW(
            runtime_error,
            fmt::format("metadata inconsistency: number of devices ({}) does "
                        "not match other/device index delta ({} - {} = {})",
                        devs->size(), other_offset, dev_inode_offset_,
                        other_offset - dev_inode_offset_));
      }
    }
  }

  void dump(std::ostream& os, int detail_level, filesystem_info const& fsinfo,
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

  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override {
    walk_data_order_impl(func);
  }

  std::optional<inode_view> find(const char* path) const override;
  std::optional<inode_view> find(int inode) const override;
  std::optional<inode_view> find(int inode, const char* name) const override;

  int getattr(inode_view iv, struct ::stat* stbuf) const override;

  std::optional<directory_view> opendir(inode_view iv) const override;

  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const override;

  size_t dirsize(directory_view dir) const override {
    return 2 + dir.entry_count(); // adds '.' and '..', which we fake in ;-)
  }

  int access(inode_view iv, int mode, uid_t uid, gid_t gid) const override;

  int open(inode_view iv) const override;

  int readlink(inode_view iv, std::string* buf) const override;

  folly::Expected<std::string, int> readlink(inode_view iv) const override;

  int statvfs(struct ::statvfs* stbuf) const override;

  std::optional<chunk_range> get_chunks(int inode) const override;

  size_t block_size() const override { return meta_.block_size(); }

 private:
  template <typename K>
  using set_type = folly::F14ValueSet<K>;

  thrift::metadata::metadata unpack_metadata() const;

  inode_view make_inode_view(uint32_t inode) const {
    // TODO: move compatibility details to metadata_types
    uint32_t index =
        meta_.dir_entries() ? inode : meta_.entry_table_v2_2()[inode];
    return inode_view(meta_.inodes()[index], inode, &meta_);
  }

  dir_entry_view
  make_dir_entry_view(uint32_t self_index, uint32_t parent_index) const {
    return dir_entry_view::from_dir_entry_index(self_index, parent_index,
                                                &global_);
  }

  // This represents the order in which inodes are stored in inodes
  // (or entry_table_v2_2 for older file systems)
  enum class inode_rank {
    INO_DIR,
    INO_LNK,
    INO_REG,
    INO_DEV,
    INO_OTH,
  };

  static inode_rank get_inode_rank(uint16_t mode) {
    switch (mode & S_IFMT) {
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

  size_t find_inode_offset(inode_rank rank) const {
    if (meta_.dir_entries()) {
      auto range = boost::irange(size_t(0), meta_.inodes().size());

      auto it = std::lower_bound(
          range.begin(), range.end(), rank, [&](auto inode, inode_rank r) {
            auto mode = meta_.modes()[meta_.inodes()[inode].mode_index()];
            return get_inode_rank(mode) < r;
          });

      return *it;
    } else {
      auto range = boost::irange(size_t(0), meta_.entry_table_v2_2().size());

      auto it = std::lower_bound(range.begin(), range.end(), rank,
                                 [&](auto inode, inode_rank r) {
                                   auto iv = make_inode_view(inode);
                                   return get_inode_rank(iv.mode()) < r;
                                 });

      return *it;
    }
  }

  directory_view make_directory_view(inode_view iv) const {
    // TODO: revisit: is this the way to do it?
    return directory_view(iv.inode_num(), &global_);
  }

  // TODO: see if we really need to pass the extra dir_entry_view in
  //       addition to directory_view
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

  uint32_t chunk_table_lookup(uint32_t ino) const {
    return chunk_table_.empty() ? meta_.chunk_table()[ino] : chunk_table_[ino];
  }

  int file_inode_to_chunk_index(int inode) const {
    inode -= file_inode_offset_;

    if (inode >= unique_files_) {
      inode -= unique_files_;

      if (!shared_files_.empty()) {
        if (inode < static_cast<int>(shared_files_.size())) {
          inode = shared_files_[inode] + unique_files_;
        }
      } else if (auto sfp = meta_.shared_files_table()) {
        if (inode < static_cast<int>(sfp->size())) {
          inode = (*sfp)[inode] + unique_files_;
        }
      }
    }

    return inode;
  }

  std::optional<chunk_range> get_chunk_range(int inode) const {
    std::optional<chunk_range> rv;

    inode = file_inode_to_chunk_index(inode);

    if (inode >= 0 &&
        inode < (static_cast<int>(meta_.chunk_table().size()) - 1)) {
      uint32_t begin = chunk_table_lookup(inode);
      uint32_t end = chunk_table_lookup(inode + 1);
      rv = chunk_range(&meta_, begin, end);
    }

    return rv;
  }

  size_t reg_file_size(inode_view iv) const {
    auto cr = get_chunk_range(iv.inode_num());
    DWARFS_CHECK(cr, "invalid chunk range");
    return std::accumulate(
        cr->begin(), cr->end(), static_cast<size_t>(0),
        [](size_t s, chunk_view cv) { return s + cv.size(); });
  }

  size_t file_size(inode_view iv, uint16_t mode) const {
    if (S_ISREG(mode)) {
      return reg_file_size(iv);
    } else if (S_ISLNK(mode)) {
      return link_value(iv).size();
    } else {
      return 0;
    }
  }

  // TODO: cleanup the walk logic
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
  walk_data_order_impl(std::function<void(dir_entry_view)> const& func) const;

  std::optional<inode_view> get_entry(int inode) const {
    inode -= inode_offset_;
    std::optional<inode_view> rv;
    if (inode >= 0 && inode < inode_count_) {
      rv = make_inode_view(inode);
    }
    return rv;
  }

  std::string link_value(inode_view iv) const {
    return symlinks_[meta_.symlink_table()[iv.inode_num() -
                                           symlink_inode_offset_]];
  }

  uint64_t get_device_id(int inode) const {
    if (auto devs = meta_.devices()) {
      return (*devs)[inode - dev_inode_offset_];
    }
    LOG_ERROR << "get_device_id() called, but no devices in file system";
    return 0;
  }

  std::vector<uint32_t> unpack_chunk_table() const {
    std::vector<uint32_t> chunk_table;

    if (auto opts = meta_.options(); opts and opts->packed_chunk_table()) {
      auto ti = LOG_TIMED_DEBUG;

      chunk_table.resize(meta_.chunk_table().size());
      std::partial_sum(meta_.chunk_table().begin(), meta_.chunk_table().end(),
                       chunk_table.begin());

      ti << "unpacked chunk table ("
         << size_with_unit(sizeof(chunk_table.front()) * chunk_table.capacity())
         << ")";
    }

    return chunk_table;
  }

  std::vector<uint32_t> decompress_shared_files() const {
    std::vector<uint32_t> decompressed;

    if (auto opts = meta_.options();
        opts and opts->packed_shared_files_table()) {
      if (auto sfp = meta_.shared_files_table(); sfp and !sfp->empty()) {
        auto ti = LOG_TIMED_DEBUG;

        auto size = std::accumulate(sfp->begin(), sfp->end(), 2 * sfp->size());
        decompressed.reserve(size);

        uint32_t index = 0;
        for (auto c : *sfp) {
          decompressed.insert(decompressed.end(), c + 2, index++);
        }

        DWARFS_CHECK(decompressed.size() == size,
                     "unexpected decompressed shared files count");

        ti << "decompressed shared files table ("
           << size_with_unit(sizeof(decompressed.front()) *
                             decompressed.capacity())
           << ")";
      }
    }

    return decompressed;
  }

  std::vector<uint32_t> build_nlinks(metadata_options const& options) const {
    std::vector<uint32_t> nlinks;

    if (options.enable_nlink) {
      auto ti = LOG_TIMED_DEBUG;

      nlinks.resize(dev_inode_offset_ - file_inode_offset_);

      if (auto de = meta_.dir_entries()) {
        for (auto e : *de) {
          int index = int(e.inode_num()) - file_inode_offset_;
          if (index >= 0 && index < int(nlinks.size())) {
            ++nlinks[index];
          }
        }
      } else {
        for (auto e : meta_.inodes()) {
          int index = int(e.inode_v2_2()) - file_inode_offset_;
          if (index >= 0 && index < int(nlinks.size())) {
            ++nlinks[index];
          }
        }
      }

      ti << "built hardlink table ("
         << size_with_unit(sizeof(nlinks.front()) * nlinks.capacity()) << ")";
    }

    return nlinks;
  }

  folly::ByteRange data_;
  MappedFrozen<thrift::metadata::metadata> meta_;
  const global_metadata global_;
  dir_entry_view root_;
  LOG_PROXY_DECL(LoggerPolicy);
  const int inode_offset_;
  const int symlink_inode_offset_;
  const int file_inode_offset_;
  const int dev_inode_offset_;
  const int inode_count_;
  const std::vector<uint32_t> nlinks_;
  const std::vector<uint32_t> chunk_table_;
  const std::vector<uint32_t> shared_files_;
  const int unique_files_;
  const metadata_options options_;
  const string_table symlinks_;
};

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::dump(
    std::ostream& os, const std::string& indent, dir_entry_view entry,
    int detail_level,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  auto iv = entry.inode();
  auto mode = iv.mode();
  auto inode = iv.inode_num();

  os << indent << "<inode:" << inode << "> " << modestring(mode);

  if (inode > 0) {
    os << " " << entry.name();
  }

  if (S_ISREG(mode)) {
    auto cr = get_chunk_range(inode);
    DWARFS_CHECK(cr, "invalid chunk range");
    os << " [" << cr->begin_ << ", " << cr->end_ << "]";
    os << " " << file_size(iv, mode) << "\n";
    if (detail_level > 4) {
      icb(indent + "  ", inode);
    }
  } else if (S_ISDIR(mode)) {
    dump(os, indent + "  ", make_directory_view(iv), entry, detail_level,
         std::move(icb));
  } else if (S_ISLNK(mode)) {
    os << " -> " << link_value(iv) << "\n";
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
    std::ostream& os, int detail_level, filesystem_info const& fsinfo,
    std::function<void(const std::string&, uint32_t)> const& icb) const {
  struct ::statvfs stbuf;
  statvfs(&stbuf);

  if (auto version = meta_.dwarfs_version()) {
    os << "created by: " << *version << std::endl;
  }

  if (auto ts = meta_.create_timestamp()) {
    time_t tp = *ts;
    std::string str(32, '\0');
    str.resize(
        std::strftime(str.data(), str.size(), "%F %T", std::localtime(&tp)));
    os << "created on: " << str << std::endl;
  }

  if (detail_level > 0) {
    os << "block size: " << size_with_unit(stbuf.f_bsize) << std::endl;
    os << "block count: " << fsinfo.block_count << std::endl;
    os << "inode count: " << stbuf.f_files << std::endl;
    os << "original filesystem size: " << size_with_unit(stbuf.f_blocks)
       << std::endl;
    os << "compressed block size: "
       << size_with_unit(fsinfo.compressed_block_size)
       << fmt::format(" ({0:.2f}%)", (100.0 * fsinfo.compressed_block_size) /
                                         fsinfo.uncompressed_block_size)
       << std::endl;
    os << "uncompressed block size: "
       << size_with_unit(fsinfo.uncompressed_block_size) << std::endl;
    os << "compressed metadata size: "
       << size_with_unit(fsinfo.compressed_metadata_size)
       << fmt::format(" ({0:.2f}%)", (100.0 * fsinfo.compressed_metadata_size) /
                                         fsinfo.uncompressed_metadata_size)
       << std::endl;
    os << "uncompressed metadata size: "
       << size_with_unit(fsinfo.uncompressed_metadata_size) << std::endl;
    if (auto opt = meta_.options()) {
      std::vector<std::string> options;
      auto boolopt = [&](auto const& name, bool value) {
        if (value) {
          options.push_back(name);
        }
      };
      boolopt("mtime_only", opt->mtime_only());
      boolopt("packed_chunk_table", opt->packed_chunk_table());
      boolopt("packed_directories", opt->packed_directories());
      boolopt("packed_shared_files_table", opt->packed_shared_files_table());
      if (auto names = meta_.compact_names()) {
        boolopt("packed_names", static_cast<bool>(names->symtab()));
        boolopt("packed_names_index", names->packed_index());
      }
      if (auto symlinks = meta_.compact_symlinks()) {
        boolopt("packed_symlinks", static_cast<bool>(symlinks->symtab()));
        boolopt("packed_symlinks_index", symlinks->packed_index());
      }
      os << "options: " << boost::join(options, "\n         ") << std::endl;
      if (auto res = opt->time_resolution_sec()) {
        os << "time resolution: " << *res << " seconds" << std::endl;
      }
    }
  }

  if (detail_level > 1) {
    analyze_frozen(os, meta_, data_.size(), detail_level);
  }

  if (detail_level > 2) {
    os << "symlink_inode_offset: " << symlink_inode_offset_ << std::endl;
    os << "file_inode_offset: " << file_inode_offset_ << std::endl;
    os << "dev_inode_offset: " << dev_inode_offset_ << std::endl;
    os << "chunks: " << meta_.chunks().size() << std::endl;
    os << "directories: " << meta_.directories().size() << std::endl;
    os << "inodes: " << meta_.inodes().size() << std::endl;
    os << "chunk_table: " << meta_.chunk_table().size() << std::endl;
    os << "entry_table_v2_2: " << meta_.entry_table_v2_2().size() << std::endl;
    os << "symlink_table: " << meta_.symlink_table().size() << std::endl;
    os << "uids: " << meta_.uids().size() << std::endl;
    os << "gids: " << meta_.gids().size() << std::endl;
    os << "modes: " << meta_.modes().size() << std::endl;
    os << "names: " << meta_.names().size() << std::endl;
    os << "symlinks: " << meta_.symlinks().size() << std::endl;
    if (auto dev = meta_.devices()) {
      os << "devices: " << dev->size() << std::endl;
    }
    if (auto de = meta_.dir_entries()) {
      os << "dir_entries: " << de->size() << std::endl;
    }
    if (auto sfp = meta_.shared_files_table()) {
      if (meta_.options()->packed_shared_files_table()) {
        os << "packed shared_files_table: " << sfp->size() << std::endl;
        os << "unpacked shared_files_table: " << shared_files_.size()
           << std::endl;
      } else {
        os << "shared_files_table: " << sfp->size() << std::endl;
      }
      os << "unique files: " << unique_files_ << std::endl;
    }
  }

  if (detail_level > 5) {
    os << ::apache::thrift::debugString(meta_.thaw()) << '\n';
  }

  if (detail_level > 3) {
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

  auto iv = entry.inode();
  auto mode = iv.mode();
  auto inode = iv.inode_num();

  obj["mode"] = mode;
  obj["modestring"] = modestring(mode);
  obj["inode"] = inode;

  if (inode > 0) {
    obj["name"] = std::string(entry.name());
  }

  if (S_ISREG(mode)) {
    obj["type"] = "file";
    obj["size"] = file_size(iv, mode);
  } else if (S_ISDIR(mode)) {
    obj["type"] = "directory";
    obj["inodes"] = as_dynamic(make_directory_view(iv), entry);
  } else if (S_ISLNK(mode)) {
    obj["type"] = "link";
    obj["target"] = std::string(link_value(iv));
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
thrift::metadata::metadata metadata_<LoggerPolicy>::unpack_metadata() const {
  auto meta = meta_.thaw();

  if (auto opts = meta.options_ref()) {
    if (opts->packed_chunk_table) {
      meta.chunk_table = chunk_table_;
    }
    if (opts->packed_directories) {
      meta.directories = global_.directories();
    }
    if (opts->packed_shared_files_table) {
      meta.shared_files_table_ref() = shared_files_;
    }
    if (auto const& names = global_.names(); names.is_packed()) {
      meta.names = names.unpack();
      meta.compact_names_ref().reset();
    }
    if (symlinks_.is_packed()) {
      meta.symlinks = symlinks_.unpack();
      meta.compact_symlinks_ref().reset();
    }
    opts->packed_chunk_table = false;
    opts->packed_directories = false;
    opts->packed_shared_files_table = false;
  }

  return meta;
}

template <typename LoggerPolicy>
std::string metadata_<LoggerPolicy>::serialize_as_json(bool simple) const {
  std::string json;
  if (simple) {
    apache::thrift::SimpleJSONSerializer serializer;
    serializer.serialize(unpack_metadata(), &json);
  } else {
    apache::thrift::JSONSerializer serializer;
    serializer.serialize(unpack_metadata(), &json);
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
  auto iv = entry.inode();

  if (S_ISDIR(iv.mode())) {
    auto inode = iv.inode_num();

    if (!seen.emplace(inode).second) {
      DWARFS_THROW(runtime_error, "cycle detected during directory walk");
    }

    auto dir = make_directory_view(iv);

    for (auto cur_index : dir.entry_range()) {
      walk(cur_index, self_index, seen, func);
    }

    seen.erase(inode);
  }
}

template <typename LoggerPolicy>
void metadata_<LoggerPolicy>::walk_data_order_impl(
    std::function<void(dir_entry_view)> const& func) const {
  std::vector<std::pair<uint32_t, uint32_t>> entries;

  if (auto dep = meta_.dir_entries()) {
    entries.reserve(dep->size());
  } else {
    entries.reserve(meta_.inodes().size());
  }

  {
    auto td = LOG_TIMED_DEBUG;

    walk_tree([&](uint32_t self_index, uint32_t parent_index) {
      entries.emplace_back(self_index, parent_index);
    });

    if (auto dep = meta_.dir_entries()) {
      // 1. partition non-files / files
      auto mid =
          std::stable_partition(entries.begin(), entries.end(),
                                [de = *dep, beg = file_inode_offset_,
                                 end = dev_inode_offset_](auto const& e) {
                                  int ino = de[e.first].inode_num();
                                  return ino < beg or ino >= end;
                                });

      // 2. order files by chunk block number
      // 2a. build mapping inode -> first chunk block
      std::vector<uint32_t> first_chunk_block;

      {
        auto td2 = LOG_TIMED_DEBUG;

        first_chunk_block.resize(dep->size());

        for (size_t ix = 0; ix < first_chunk_block.size(); ++ix) {
          int ino = (*dep)[ix].inode_num();
          if (ino >= file_inode_offset_ and ino < dev_inode_offset_) {
            ino = file_inode_to_chunk_index(ino);
            if (auto beg = chunk_table_lookup(ino);
                beg != chunk_table_lookup(ino + 1)) {
              first_chunk_block[ix] = meta_.chunks()[beg].block();
            }
          }
        }

        td2 << "prepare first chunk block vector";
      }

      // 2b. sort second partition accordingly
      {
        auto td2 = LOG_TIMED_DEBUG;

        std::stable_sort(mid, entries.end(),
                         [&first_chunk_block](auto const& a, auto const& b) {
                           return first_chunk_block[a.first] <
                                  first_chunk_block[b.first];
                         });

        td2 << "final sort of " << std::distance(mid, entries.end())
            << " file entries";
      }
    } else {
      std::sort(entries.begin(), entries.end(),
                [this](auto const& a, auto const& b) {
                  return meta_.inodes()[a.first].inode_v2_2() <
                         meta_.inodes()[b.first].inode_v2_2();
                });
    }

    td << "ordered " << entries.size() << " entries by file data order";
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
                               return dir_entry_view::name(ix, &global_) < name;
                             });

  std::optional<inode_view> rv;

  if (it != range.end()) {
    if (dir_entry_view::name(*it, &global_) == name) {
      rv = dir_entry_view::inode(*it, &global_);
    }
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<inode_view>
metadata_<LoggerPolicy>::find(const char* path) const {
  while (*path == '/') {
    ++path;
  }

  std::optional<inode_view> iv = root_.inode();

  while (*path) {
    const char* next = ::strchr(path, '/');
    size_t clen = next ? next - path : ::strlen(path); // Flawfinder: ignore

    iv = find(make_directory_view(*iv), std::string_view(path, clen));

    if (!iv) {
      break;
    }

    path = next ? next + 1 : path + clen;
  }

  return iv;
}

template <typename LoggerPolicy>
std::optional<inode_view> metadata_<LoggerPolicy>::find(int inode) const {
  return get_entry(inode);
}

template <typename LoggerPolicy>
std::optional<inode_view>
metadata_<LoggerPolicy>::find(int inode, const char* name) const {
  auto iv = get_entry(inode);

  if (iv) {
    iv = find(make_directory_view(*iv), std::string_view(name));
  }

  return iv;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::getattr(inode_view iv,
                                     struct ::stat* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  auto mode = iv.mode();
  auto timebase = meta_.timestamp_base();
  auto inode = iv.inode_num();
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

  stbuf->st_size = S_ISDIR(mode) ? make_directory_view(iv).entry_count()
                                 : file_size(iv, mode);
  stbuf->st_ino = inode + inode_offset_;
  stbuf->st_blocks = (stbuf->st_size + 511) / 512;
  stbuf->st_uid = iv.getuid();
  stbuf->st_gid = iv.getgid();
  stbuf->st_mtime = resolution * (timebase + iv.mtime_offset());
  if (mtime_only) {
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime;
  } else {
    stbuf->st_atime = resolution * (timebase + iv.atime_offset());
    stbuf->st_ctime = resolution * (timebase + iv.ctime_offset());
  }
  stbuf->st_nlink = options_.enable_nlink && S_ISREG(mode)
                        ? DWARFS_NOTHROW(nlinks_.at(inode - file_inode_offset_))
                        : 1;

  if (S_ISBLK(mode) || S_ISCHR(mode)) {
    stbuf->st_rdev = get_device_id(inode);
  }

  return 0;
}

template <typename LoggerPolicy>
std::optional<directory_view>
metadata_<LoggerPolicy>::opendir(inode_view iv) const {
  std::optional<directory_view> rv;

  if (S_ISDIR(iv.mode())) {
    rv = make_directory_view(iv);
  }

  return rv;
}

template <typename LoggerPolicy>
std::optional<std::pair<inode_view, std::string>>
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
    auto inode = dir_entry_view::inode(index, &global_);
    return std::pair(inode, dir_entry_view::name(index, &global_));
  }

  return std::nullopt;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::access(inode_view iv, int mode, uid_t uid,
                                    gid_t gid) const {
  if (mode == F_OK) {
    // easy; we're only interested in the file's existance
    return 0;
  }

  int access_mode = 0;
  int e_mode = iv.mode();

  auto test = [e_mode, &access_mode](uint16_t r_bit, uint16_t x_bit) {
    if (e_mode & r_bit) {
      access_mode |= R_OK;
    }
    if (e_mode & x_bit) {
      access_mode |= X_OK;
    }
  };

  // Let's build the inode's access mask
  test(S_IROTH, S_IXOTH);

  if (iv.getgid() == gid) {
    test(S_IRGRP, S_IXGRP);
  }

  if (iv.getuid() == uid) {
    test(S_IRUSR, S_IXUSR);
  }

  return (access_mode & mode) == mode ? 0 : EACCES;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::open(inode_view iv) const {
  if (S_ISREG(iv.mode())) {
    return iv.inode_num();
  }

  return -1;
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::readlink(inode_view iv, std::string* buf) const {
  if (S_ISLNK(iv.mode())) {
    buf->assign(link_value(iv));
    return 0;
  }

  return -EINVAL;
}

template <typename LoggerPolicy>
folly::Expected<std::string, int>
metadata_<LoggerPolicy>::readlink(inode_view iv) const {
  if (S_ISLNK(iv.mode())) {
    return link_value(iv);
  }

  return folly::makeUnexpected(-EINVAL);
}

template <typename LoggerPolicy>
int metadata_<LoggerPolicy>::statvfs(struct ::statvfs* stbuf) const {
  ::memset(stbuf, 0, sizeof(*stbuf));

  stbuf->f_bsize = meta_.block_size();
  stbuf->f_frsize = 1UL;
  stbuf->f_blocks = meta_.total_fs_size();
  if (!options_.enable_nlink) {
    if (auto ths = meta_.total_hardlink_size()) {
      stbuf->f_blocks += *ths;
    }
  }
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

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
metadata_v2::freeze(const thrift::metadata::metadata& data) {
  return freeze_to_buffer(data);
}

metadata_v2::metadata_v2(logger& lgr, folly::ByteRange schema,
                         folly::ByteRange data, metadata_options const& options,
                         int inode_offset, bool force_consistency_check)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_,
                                       logger_policies>(
          lgr, schema, data, options, inode_offset, force_consistency_check)) {}

} // namespace dwarfs

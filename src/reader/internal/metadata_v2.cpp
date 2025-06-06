/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <bit>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <numeric>
#include <ostream>

#include <boost/algorithm/string.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/chrono.h>
#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <folly/portability/Stdlib.h>
#include <folly/portability/Unistd.h>
#include <folly/small_vector.h>
#include <folly/stats/Histogram.h>

#include <parallel_hashmap/phmap.h>

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

#include <dwarfs/error.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/logger.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/getattr_options.h>
#include <dwarfs/reader/metadata_options.h>
#include <dwarfs/util.h>
#include <dwarfs/vfs_stat.h>

#include <dwarfs/internal/features.h>
#include <dwarfs/internal/metadata_utils.h>
#include <dwarfs/internal/packed_int_vector.h>
#include <dwarfs/internal/string_table.h>
#include <dwarfs/internal/unicode_case_folding.h>
#include <dwarfs/reader/internal/metadata_v2.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>
#include <dwarfs/gen-cpp2/metadata_types_custom_protocol.h>

#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;
namespace fs = std::filesystem;

namespace {

using ::apache::thrift::frozen::MappedFrozen;

::apache::thrift::frozen::schema::Schema
deserialize_schema(std::span<uint8_t const> data) {
  ::apache::thrift::frozen::schema::Schema schema;
  size_t schema_size =
      ::apache::thrift::CompactSerializer::deserialize(data, schema);
  if (schema_size != data.size()) {
    DWARFS_THROW(runtime_error, "invalid schema size");
  }
  return schema;
}

void check_schema(std::span<uint8_t const> data) {
  auto schema = deserialize_schema(data);
  if (!schema.layouts()->contains(*schema.rootLayout())) {
    DWARFS_THROW(runtime_error, "invalid rootLayout in schema");
  }
  for (auto const& kvl : *schema.layouts()) {
    auto const& layout = kvl.second;
    if (std::cmp_greater_equal(kvl.first, schema.layouts()->size())) {
      DWARFS_THROW(runtime_error, "invalid layout key in schema");
    }
    if (*layout.size() < 0) {
      DWARFS_THROW(runtime_error, "negative size in schema");
    }
    if (*layout.bits() < 0) {
      DWARFS_THROW(runtime_error, "negative bits in schema");
    }
    for (auto const& kvf : *layout.fields()) {
      auto const& field = kvf.second;
      if (!schema.layouts()->contains(*field.layoutId())) {
        DWARFS_THROW(runtime_error, "invalid layoutId in field");
      }
    }
  }
}

template <typename T>
MappedFrozen<T>
map_frozen(std::span<uint8_t const> schema, std::span<uint8_t const> data) {
  using namespace ::apache::thrift::frozen;
  check_schema(schema);
  auto layout = std::make_unique<Layout<T>>();
  folly::ByteRange tmp(schema.data(), schema.size());
  deserializeRootLayout(tmp, *layout);
  MappedFrozen<T> ret(layout->view({data.data(), 0}));
  ret.hold(std::move(layout));
  return ret;
}

MappedFrozen<thrift::metadata::metadata>
check_frozen(MappedFrozen<thrift::metadata::metadata> meta) {
  if (meta.features()) {
    auto unsupported = feature_set::get_unsupported(meta.features()->thaw());
    if (!unsupported.empty()) {
      DWARFS_THROW(runtime_error,
                   fmt::format("file system uses the following features "
                               "unsupported by this build: {}",
                               boost::join(unsupported, ", ")));
    }
  }
  return meta;
}

global_metadata::Meta const&
check_metadata_consistency(logger& lgr, global_metadata::Meta const& meta,
                           bool force_consistency_check) {
  if (force_consistency_check) {
    global_metadata::check_consistency(lgr, meta);
  }
  return meta; // NOLINT(bugprone-return-const-ref-from-parameter)
}

::apache::thrift::frozen::Layout<thrift::metadata::metadata> const&
get_layout(MappedFrozen<thrift::metadata::metadata> const& meta) {
  auto layout = meta.findFirstOfType<std::unique_ptr<
      ::apache::thrift::frozen::Layout<thrift::metadata::metadata>>>();
  DWARFS_CHECK(layout, "no layout found");
  return **layout;
}

void analyze_frozen(std::ostream& os,
                    MappedFrozen<thrift::metadata::metadata> const& meta,
                    size_t total_size) {
  using namespace ::apache::thrift::frozen;
  null_logger lgr;

  auto& l = get_layout(meta);
  std::vector<std::pair<size_t, std::string>> usage;

#if FMT_VERSION >= 70000
#define DWARFS_FMT_L "L"
#else
#define DWARFS_FMT_L "n"
#endif

  auto fmt_size = [&](std::string_view name, std::optional<size_t> count_opt,
                      size_t size) {
    auto count = count_opt.value_or(1);
    std::string count_str;
    if (count_opt.has_value()) {
      count_str = fmt::format("{0:" DWARFS_FMT_L "}", count);
    }
    return fmt::format("{0:>14} {1:.<24}{2:.>16" DWARFS_FMT_L
                       "} bytes {3:5.1f}% {4:5.1f} bytes/item\n",
                       count_str, name, size, 100.0 * size / total_size,
                       count > 0 ? static_cast<double>(size) / count : 0.0);
  };

  auto fmt_detail = [&](std::string_view name, size_t count, size_t size,
                        std::string num) {
    return fmt::format(
        "               {0:<24}{1:>16" DWARFS_FMT_L "} bytes {2:>6} "
        "{3:5.1f} bytes/item\n",
        name, size, num, count > 0 ? static_cast<double>(size) / count : 0.0);
  };

  auto fmt_detail_pct = [&](std::string_view name, size_t count, size_t size) {
    return fmt_detail(name, count, size,
                      fmt::format("{0:5.1f}%", 100.0 * size / total_size));
  };

  auto add_size = [&](std::string_view name, size_t count, size_t size) {
    usage.emplace_back(size, fmt_size(name, count, size));
  };

  auto add_size_unique = [&](std::string_view name, size_t size) {
    usage.emplace_back(size, fmt_size(name, std::nullopt, size));
  };

  auto list_size = [&](auto const& list, auto const& field) {
    return (list.size() * field.layout.itemField.layout.bits + 7) / 8;
  };

  auto add_list_size = [&](std::string_view name, auto const& list,
                           auto const& field) {
    add_size(name, list.size(), list_size(list, field));
  };

  auto add_string_list_size = [&](std::string_view name, auto const& list,
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

  auto add_string_table_size = [&](std::string_view name, auto const& table,
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

  using detail_bits_t = std::pair<std::string_view, size_t>;

  auto summarize_details = [&](std::string_view name, size_t count, size_t size,
                               std::span<detail_bits_t> details) {
    std::ranges::stable_sort(details, ranges::greater{},
                             &detail_bits_t::second);
    auto fmt = fmt_size(name, count, size);
    for (size_t i = 0; i < details.size(); ++i) {
      auto [member, bits] = details[i];
      auto tree = i == details.size() - 1 ? "'" : "|";
      fmt += fmt_detail_pct(fmt::format("{}- {} [{}]", tree, member, bits),
                            count, (count * bits + 7) / 8);
    }
    usage.emplace_back(size, fmt);
  };

#define META_LIST_SIZE(x) add_list_size(#x, meta.x(), l.x##Field)

#define META_STRING_LIST_SIZE(x) add_string_list_size(#x, meta.x(), l.x##Field)

#define META_OPT_LIST_SIZE(x)                                                  \
  do {                                                                         \
    if (auto list = meta.x()) {                                                \
      add_list_size(#x, *list, l.x##Field.layout.valueField);                  \
    }                                                                          \
  } while (0)

#define META_OPT_STRING_LIST_SIZE(x)                                           \
  do {                                                                         \
    if (auto list = meta.x()) {                                                \
      add_string_list_size(#x, *list, l.x##Field.layout.valueField);           \
    }                                                                          \
  } while (0)

#define META_OPT_STRING_SET_SIZE(x) META_OPT_STRING_LIST_SIZE(x)

#define META_OPT_STRING_TABLE_SIZE(x)                                          \
  do {                                                                         \
    if (auto table = meta.x()) {                                               \
      add_string_table_size(#x, *table, l.x##Field.layout.valueField);         \
    }                                                                          \
  } while (0)

#define META_LIST_SIZE_DETAIL_BEGIN                                            \
  do {                                                                         \
    std::vector<detail_bits_t> detail_bits;

#define META_ADD_DETAIL_BITS(field, x)                                         \
  do {                                                                         \
    if (auto bits =                                                            \
            l.field##Field.layout.itemField.layout.x##Field.layout.bits;       \
        bits > 0) {                                                            \
      detail_bits.emplace_back(#x, bits);                                      \
    }                                                                          \
  } while (0)

#define META_LIST_SIZE_DETAIL_END(x)                                           \
  summarize_details(#x, meta.x().size(), list_size(meta.x(), l.x##Field),      \
                    detail_bits);                                              \
  }                                                                            \
  while (0)

#define META_OPT_LIST_SIZE_DETAIL_BEGIN(x)                                     \
  do {                                                                         \
    if (auto list = meta.x()) {                                                \
      std::vector<detail_bits_t> detail_bits;

#define META_OPT_ADD_DETAIL_BITS(field, x)                                     \
  do {                                                                         \
    if (auto bits = l.field##Field.layout.valueField.layout.itemField.layout   \
                        .x##Field.layout.bits;                                 \
        bits > 0) {                                                            \
      detail_bits.emplace_back(#x, bits);                                      \
    }                                                                          \
  } while (0)

#define META_OPT_LIST_SIZE_DETAIL_END(x)                                       \
  summarize_details(#x, list->size(),                                          \
                    list_size(*list, l.x##Field.layout.valueField),            \
                    detail_bits);                                              \
  }                                                                            \
  }                                                                            \
  while (0)

  META_LIST_SIZE_DETAIL_BEGIN;
  META_ADD_DETAIL_BITS(chunks, block);
  META_ADD_DETAIL_BITS(chunks, offset);
  META_ADD_DETAIL_BITS(chunks, size);
  META_LIST_SIZE_DETAIL_END(chunks);

  META_LIST_SIZE_DETAIL_BEGIN;
  META_ADD_DETAIL_BITS(directories, parent_entry);
  META_ADD_DETAIL_BITS(directories, first_entry);
  META_ADD_DETAIL_BITS(directories, self_entry);
  META_LIST_SIZE_DETAIL_END(directories);

  META_LIST_SIZE_DETAIL_BEGIN;
  META_ADD_DETAIL_BITS(inodes, mode_index);
  META_ADD_DETAIL_BITS(inodes, owner_index);
  META_ADD_DETAIL_BITS(inodes, group_index);
  META_ADD_DETAIL_BITS(inodes, atime_offset);
  META_ADD_DETAIL_BITS(inodes, mtime_offset);
  META_ADD_DETAIL_BITS(inodes, ctime_offset);
  META_ADD_DETAIL_BITS(inodes, name_index_v2_2);
  META_ADD_DETAIL_BITS(inodes, inode_v2_2);
  META_LIST_SIZE_DETAIL_END(inodes);

  META_OPT_LIST_SIZE_DETAIL_BEGIN(dir_entries);
  META_OPT_ADD_DETAIL_BITS(dir_entries, name_index);
  META_OPT_ADD_DETAIL_BITS(dir_entries, inode_num);
  META_OPT_LIST_SIZE_DETAIL_END(dir_entries);

  META_LIST_SIZE(chunk_table);
  if (!meta.entry_table_v2_2().empty()) {
    // deprecated, so only list if non-empty
    META_LIST_SIZE(entry_table_v2_2);
  }
  META_LIST_SIZE(symlink_table);
  META_LIST_SIZE(uids);
  META_LIST_SIZE(gids);
  META_LIST_SIZE(modes);

  META_OPT_LIST_SIZE(devices);
  META_OPT_LIST_SIZE(shared_files_table);

  META_OPT_STRING_TABLE_SIZE(compact_names);
  META_OPT_STRING_TABLE_SIZE(compact_symlinks);

  META_STRING_LIST_SIZE(names);
  META_STRING_LIST_SIZE(symlinks);

  META_OPT_STRING_SET_SIZE(features);

  META_OPT_STRING_LIST_SIZE(category_names);
  META_OPT_LIST_SIZE(block_categories);

#undef META_LIST_SIZE
#undef META_OPT_STRING_SET_SIZE
#undef META_OPT_STRING_LIST_SIZE
#undef META_STRING_LIST_SIZE
#undef META_OPT_LIST_SIZE
#undef META_OPT_STRING_TABLE_SIZE
#undef META_LIST_SIZE_DETAIL_BEGIN
#undef META_ADD_DETAIL_BITS
#undef META_LIST_SIZE_DETAIL_END
#undef META_OPT_LIST_SIZE_DETAIL_BEGIN
#undef META_OPT_ADD_DETAIL_BITS
#undef META_OPT_LIST_SIZE_DETAIL_END

  if (auto cache = meta.reg_file_size_cache()) {
    add_list_size(
        "inode_size_cache", cache->lookup(),
        l.reg_file_size_cacheField.layout.valueField.layout.lookupField);
  }

  if (auto version = meta.dwarfs_version()) {
    add_size_unique("dwarfs_version", version->size());
  }

  add_size_unique("metadata_root", l.size);

  std::ranges::sort(usage, [](auto const& a, auto const& b) {
    return a.first > b.first || (a.first == b.first && a.second < b.second);
  });

  os << "metadata memory usage:\n";
  os << fmt::format("               {0:.<24}{1:.>16" DWARFS_FMT_L
                    "} bytes       {2:6.1f} bytes/inode\n",
                    "total metadata", total_size,
                    static_cast<double>(total_size) / meta.inodes().size());

#undef DWARFS_FMT_L

  for (auto const& u : usage) {
    os << u.second;
  }
}

template <typename Function>
void parse_metadata_options(
    MappedFrozen<thrift::metadata::metadata> const& meta,
    Function const& func) {
  if (auto opt = meta.options()) {
    func("mtime_only", opt->mtime_only());
    func("packed_chunk_table", opt->packed_chunk_table());
    func("packed_directories", opt->packed_directories());
    func("packed_shared_files_table", opt->packed_shared_files_table());
  }
  if (auto names = meta.compact_names()) {
    func("packed_names", static_cast<bool>(names->symtab()));
    func("packed_names_index", names->packed_index());
  }
  if (auto symlinks = meta.compact_symlinks()) {
    func("packed_symlinks", static_cast<bool>(symlinks->symtab()));
    func("packed_symlinks_index", symlinks->packed_index());
  }
}

struct category_info {
  size_t count{0};
  std::optional<size_t> compressed_size;
  std::optional<size_t> uncompressed_size;
  bool uncompressed_size_is_estimate{false};
};

std::map<size_t, category_info>
get_category_info(MappedFrozen<thrift::metadata::metadata> const& meta,
                  filesystem_info const* fsinfo) {
  std::map<size_t, category_info> catinfo;

  if (auto blockcat = meta.block_categories()) {
    for (auto [block, category] : ranges::views::enumerate(blockcat.value())) {
      auto& ci = catinfo[category];
      ++ci.count;
      if (fsinfo) {
        if (!ci.compressed_size) {
          ci.compressed_size = 0;
          ci.uncompressed_size = 0;
        }
        *ci.compressed_size += fsinfo->compressed_block_sizes.at(block);
        if (auto size = fsinfo->uncompressed_block_sizes.at(block)) {
          *ci.uncompressed_size += *size;
        } else {
          ci.uncompressed_size_is_estimate = true;
        }
      }
    }
  }

  return catinfo;
}

uint16_t const READ_ONLY_MASK = ~uint16_t(
    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write);

} // namespace

class metadata_v2_data {
 public:
  template <typename LoggerPolicy>
  metadata_v2_data(LoggerPolicy const&, logger& lgr,
                   std::span<uint8_t const> schema,
                   std::span<uint8_t const> data,
                   metadata_options const& options, int inode_offset,
                   bool force_consistency_check,
                   std::shared_ptr<performance_monitor const> const& perfmon);

  size_t size() const { return data_.size(); }

  dir_entry_view root() const { return root_; }

  size_t block_size() const { return meta_.block_size(); }

  bool has_symlinks() const { return !meta_.symlink_table().empty(); }

  std::optional<inode_view> get_entry(int inode) const {
    inode -= inode_offset_;
    std::optional<inode_view> rv;
    if (inode >= 0 && inode < inode_count_) {
      rv = make_inode_view(inode);
    }
    return rv;
  }

  chunk_range get_chunks(int inode, std::error_code& ec) const {
    return get_chunk_range(inode - inode_offset_, ec);
  }

  directory_view make_directory_view(inode_view const& iv) const {
    return make_directory_view(iv.raw());
  }

  std::string link_value(inode_view const& iv,
                         readlink_mode mode = readlink_mode::raw) const {
    return link_value(iv.raw(), mode);
  }

  void statvfs(vfs_stat* stbuf) const;

  std::optional<dir_entry_view> find(std::string_view path) const;

  std::optional<dir_entry_view>
  find(directory_view dir, std::string_view name) const;

  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const;

  template <typename LoggerPolicy>
  file_stat getattr(LOG_PROXY_REF_(LoggerPolicy) inode_view const& iv) const {
    PERFMON_CLS_SCOPED_SECTION(getattr)
    return getattr_impl(LOG_PROXY_ARG_ iv, {});
  }

  template <typename LoggerPolicy>
  file_stat getattr(LOG_PROXY_REF_(LoggerPolicy) inode_view const& iv,
                    getattr_options const& opts) const {
    PERFMON_CLS_SCOPED_SECTION(getattr_opts)
    return getattr_impl(LOG_PROXY_ARG_ iv, opts);
  }

  void access(inode_view const& iv, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const;

  template <typename LoggerPolicy>
  void walk(LOG_PROXY_REF_(LoggerPolicy)
                std::function<void(dir_entry_view)> const& func) const {
    walk_tree(LOG_PROXY_ARG_[&](uint32_t self_index, uint32_t parent_index) {
      walk_call(func, self_index, parent_index);
    });
  }

  template <typename LoggerPolicy>
  void
  walk_data_order(LOG_PROXY_REF_(LoggerPolicy)
                      std::function<void(dir_entry_view)> const& func) const {
    walk_data_order_impl(LOG_PROXY_ARG_ func);
  }

  std::optional<std::string> get_block_category(size_t block_number) const {
    if (auto catnames = meta_.category_names()) {
      if (auto categories = meta_.block_categories()) {
        return std::string(catnames.value()[categories.value()[block_number]]);
      }
    }
    return std::nullopt;
  }

  std::vector<std::string> get_all_block_categories() const {
    std::vector<std::string> rv;

    if (auto catnames = meta_.category_names()) {
      rv.reserve(catnames.value().size());
      for (auto const& name : catnames.value()) {
        rv.emplace_back(name);
      }
    }

    return rv;
  }

  std::vector<size_t>
  get_block_numbers_by_category(std::string_view category) const;

  std::vector<file_stat::uid_type> get_all_uids() const;

  std::vector<file_stat::gid_type> get_all_gids() const;

  template <typename LoggerPolicy>
  void check_consistency(LOG_PROXY_REF(LoggerPolicy)) const {
    // TODO: can we easily use LOG_PROXY here?
    global_.check_consistency(LOG_GET_LOGGER);
    check_inode_size_cache(LOG_PROXY_ARG);
  }

  nlohmann::json as_json() const;

  nlohmann::json
  info_as_json(fsinfo_options const& opts, filesystem_info const* fsinfo) const;

  nlohmann::json get_inode_info(inode_view const& iv, size_t max_chunks) const;

  std::string serialize_as_json(bool simple) const;

  void dump(std::ostream& os, fsinfo_options const& opts,
            filesystem_info const* fsinfo,
            std::function<void(std::string const&, uint32_t)> const& icb) const;

 private:
  template <typename K>
  using set_type = phmap::flat_hash_set<K>;

  int find_inode_offset(inode_rank rank) const {
    return find_inode_rank_offset(meta_, rank);
  }

  thrift::metadata::metadata unpack_metadata() const;

  template <typename LoggerPolicy>
  void check_inode_size_cache(LOG_PROXY_REF(LoggerPolicy)) const;

  template <typename LoggerPolicy>
  std::vector<packed_int_vector<uint32_t>>
  build_dir_icase_cache(logger& lgr) const;

  template <typename LoggerPolicy>
  packed_int_vector<uint32_t>
  build_nlinks(logger& lgr, metadata_options const& options) const;

  template <typename LoggerPolicy>
  packed_int_vector<uint32_t> unpack_chunk_table(logger& lgr) const;

  template <typename LoggerPolicy>
  packed_int_vector<uint32_t> unpack_shared_files(logger& lgr) const;

  void analyze_chunks(std::ostream& os) const;

  std::optional<dir_entry_view>
  find_impl(directory_view dir, auto const& range, auto const& name,
            auto const& index_map, auto const& entry_name_transform) const;

  template <typename LoggerPolicy>
  file_stat getattr_impl(LOG_PROXY_REF_(LoggerPolicy) inode_view const& iv,
                         getattr_options const& opts) const;

  template <typename TraceFunc>
  size_t reg_file_size_impl(inode_view_impl const& iv, bool use_cache,
                            TraceFunc const& trace) const;

  size_t reg_file_size_notrace(inode_view_impl const& iv) const {
    return reg_file_size_impl(iv, true, [](int) {});
  }

  template <typename LoggerPolicy>
  size_t
  reg_file_size_impl(LOG_PROXY_REF_(LoggerPolicy) inode_view_impl const& iv,
                     bool use_cache) const {
    return reg_file_size_impl(iv, use_cache, [&](int index) {
      LOG_TRACE << "using size cache lookup for inode " << iv.inode_num()
                << " (index " << index << ")";
    });
  }

  size_t reg_file_size_notrace(inode_view const& iv) const {
    return reg_file_size_notrace(iv.raw());
  }

  template <typename LoggerPolicy>
  size_t file_size(LOG_PROXY_REF_(LoggerPolicy) inode_view_impl const& iv,
                   uint32_t mode) const {
    switch (posix_file_type::from_mode(mode)) {
    case posix_file_type::regular:
      return reg_file_size_impl(LOG_PROXY_ARG_ iv, true);
    case posix_file_type::symlink:
      return link_value(iv).size();
    default:
      return 0;
    }
  }

  template <typename LoggerPolicy>
  size_t file_size(LOG_PROXY_REF_(LoggerPolicy) inode_view const& iv,
                   uint32_t mode) const {
    return file_size(LOG_PROXY_ARG_ iv.raw(), mode);
  }

  template <typename LoggerPolicy, typename T>
  void walk(LOG_PROXY_REF_(LoggerPolicy) uint32_t self_index,
            uint32_t parent_index, set_type<int>& seen, T const& func) const;

  template <typename LoggerPolicy>
  void walk_data_order_impl(LOG_PROXY_REF_(
      LoggerPolicy) std::function<void(dir_entry_view)> const& func) const;

  template <typename LoggerPolicy, typename T>
  void walk_tree(LOG_PROXY_REF_(LoggerPolicy) T&& func) const {
    set_type<int> seen;
    walk(LOG_PROXY_ARG_ 0, 0, seen, std::forward<T>(func));
  }

  // TODO: cleanup the walk logic
  void walk_call(std::function<void(dir_entry_view)> const& func,
                 uint32_t self_index, uint32_t parent_index) const {
    func(make_dir_entry_view(self_index, parent_index));
  }

  nlohmann::json as_json(dir_entry_view const& entry) const;

  nlohmann::json as_json(directory_view dir, dir_entry_view const& entry) const;

  // TODO: see if we really need to pass the extra dir_entry_view in
  //       addition to directory_view
  void dump(std::ostream& os, std::string const& indent,
            dir_entry_view const& entry, fsinfo_options const& opts,
            std::function<void(std::string const&, uint32_t)> const& icb) const;

  void dump(std::ostream& os, std::string const& indent, directory_view dir,
            dir_entry_view const& entry, fsinfo_options const& opts,
            std::function<void(std::string const&, uint32_t)> const& icb) const;

  inode_view_impl make_inode_view_impl(uint32_t inode) const {
    // TODO: move compatibility details to metadata_types
    uint32_t index =
        meta_.dir_entries() ? inode : meta_.entry_table_v2_2()[inode];
    return {meta_.inodes()[index], inode, meta_};
  }

  inode_view make_inode_view(uint32_t inode) const {
    // TODO: move compatibility details to metadata_types
    uint32_t index =
        meta_.dir_entries() ? inode : meta_.entry_table_v2_2()[inode];
    return inode_view{
        std::make_shared<inode_view_impl>(meta_.inodes()[index], inode, meta_)};
  }

  dir_entry_view
  make_dir_entry_view(uint32_t self_index, uint32_t parent_index) const {
    return dir_entry_view{dir_entry_view_impl::from_dir_entry_index_shared(
        self_index, parent_index, global_)};
  }

  dir_entry_view_impl
  make_dir_entry_view_impl(uint32_t self_index, uint32_t parent_index) const {
    return dir_entry_view_impl::from_dir_entry_index(self_index, parent_index,
                                                     global_);
  }

  directory_view make_directory_view(uint32_t inode_num) const {
    return {inode_num, global_};
  }

  directory_view make_directory_view(inode_view_impl const& iv) const {
    // TODO: revisit: is this the way to do it?
    DWARFS_CHECK(iv.is_directory(), "not a directory");
    return make_directory_view(iv.inode_num());
  }

  uint32_t chunk_table_lookup(uint32_t ino) const {
    return chunk_table_.empty() ? meta_.chunk_table()[ino] : chunk_table_[ino];
  }

  int file_inode_to_chunk_index(int inode) const;

  chunk_range get_chunk_range_from_index(int index, std::error_code& ec) const {
    if (index >= 0 &&
        (index + 1) < static_cast<int>(meta_.chunk_table().size())) {
      ec.clear();
      uint32_t begin = chunk_table_lookup(index);
      uint32_t end = chunk_table_lookup(index + 1);
      return {meta_, begin, end};
    }

    ec = make_error_code(std::errc::invalid_argument);
    return {};
  }

  chunk_range get_chunk_range(int inode, std::error_code& ec) const {
    return get_chunk_range_from_index(file_inode_to_chunk_index(inode), ec);
  }

  std::string link_value(inode_view_impl const& iv,
                         readlink_mode mode = readlink_mode::raw) const;

  std::optional<uint64_t> get_device_id(int inode) const {
    if (auto devs = meta_.devices()) {
      return (*devs)[inode - dev_inode_offset_];
    }
    return std::nullopt;
  }

  size_t total_file_entries() const {
    return (dev_inode_offset_ - file_inode_offset_) +
           (meta_.dir_entries()
                ? meta_.dir_entries()->size() - meta_.inodes().size()
                : 0);
  }

  std::vector<uint8_t> schema_;
  std::span<uint8_t const> data_;
  MappedFrozen<thrift::metadata::metadata> meta_;
  global_metadata const global_;
  dir_entry_view root_;
  int const inode_offset_;
  int const symlink_inode_offset_;
  int const file_inode_offset_;
  int const dev_inode_offset_;
  int const inode_count_;
  packed_int_vector<uint32_t> const nlinks_;
  packed_int_vector<uint32_t> const chunk_table_;
  packed_int_vector<uint32_t> const shared_files_;
  int const unique_files_;
  metadata_options const options_;
  string_table const symlinks_;
  std::vector<packed_int_vector<uint32_t>> const dir_icase_cache_;
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(find)
  PERFMON_CLS_TIMER_DECL(find_path)
  PERFMON_CLS_TIMER_DECL(getattr)
  PERFMON_CLS_TIMER_DECL(getattr_opts)
  PERFMON_CLS_TIMER_DECL(readdir)
  PERFMON_CLS_TIMER_DECL(reg_file_size)
  PERFMON_CLS_TIMER_DECL(unpack_metadata)
};

template <typename LoggerPolicy>
metadata_v2_data::metadata_v2_data(
    LoggerPolicy const&, logger& lgr, std::span<uint8_t const> schema,
    std::span<uint8_t const> data, metadata_options const& options,
    int inode_offset, bool force_consistency_check,
    std::shared_ptr<performance_monitor const> const& perfmon [[maybe_unused]])
    : schema_{schema.begin(), schema.end()}
    , data_{data}
    , meta_{check_frozen(map_frozen<thrift::metadata::metadata>(schema, data_))}
    , global_{lgr, check_metadata_consistency(lgr, meta_,
                                              options.check_consistency ||
                                                  force_consistency_check)}
    , root_{dir_entry_view_impl::from_dir_entry_index_shared(0, global_)}
    , inode_offset_{inode_offset}
    , symlink_inode_offset_{find_inode_offset(inode_rank::INO_LNK)}
    , file_inode_offset_{find_inode_offset(inode_rank::INO_REG)}
    , dev_inode_offset_{find_inode_offset(inode_rank::INO_DEV)}
    , inode_count_(meta_.dir_entries() ? meta_.inodes().size()
                                       : meta_.entry_table_v2_2().size())
    , nlinks_{build_nlinks<LoggerPolicy>(lgr, options)}
    , chunk_table_{unpack_chunk_table<LoggerPolicy>(lgr)}
    , shared_files_{unpack_shared_files<LoggerPolicy>(lgr)}
    , unique_files_(dev_inode_offset_ - file_inode_offset_ -
                    (shared_files_.empty()
                         ? meta_.shared_files_table()
                               ? meta_.shared_files_table()->size()
                               : 0
                         : shared_files_.size()))
    , options_{options}
    , symlinks_{meta_.compact_symlinks()
                    ? string_table(lgr, "symlinks", *meta_.compact_symlinks())
                    : string_table(meta_.symlinks())}
    , dir_icase_cache_{build_dir_icase_cache<LoggerPolicy>(lgr)}
    // clang-format off
      PERFMON_CLS_PROXY_INIT(perfmon, "metadata")
      PERFMON_CLS_TIMER_INIT(find)
      PERFMON_CLS_TIMER_INIT(find_path)
      PERFMON_CLS_TIMER_INIT(getattr)
      PERFMON_CLS_TIMER_INIT(getattr_opts)
      PERFMON_CLS_TIMER_INIT(readdir)
      PERFMON_CLS_TIMER_INIT(reg_file_size)
      PERFMON_CLS_TIMER_INIT(unpack_metadata) // clang-format on
{
  if (std::cmp_not_equal(meta_.directories().size() - 1,
                         symlink_inode_offset_)) {
    DWARFS_THROW(
        runtime_error,
        fmt::format("metadata inconsistency: number of directories ({}) does "
                    "not match link index ({})",
                    meta_.directories().size() - 1, symlink_inode_offset_));
  }

  if (std::cmp_not_equal(meta_.symlink_table().size(),
                         file_inode_offset_ - symlink_inode_offset_)) {
    DWARFS_THROW(
        runtime_error,
        fmt::format(
            "metadata inconsistency: number of symlinks ({}) does not match "
            "chunk/symlink table delta ({} - {} = {})",
            meta_.symlink_table().size(), file_inode_offset_,
            symlink_inode_offset_, file_inode_offset_ - symlink_inode_offset_));
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

    if (static_cast<int>(devs->size()) != (other_offset - dev_inode_offset_)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("metadata inconsistency: number of devices ({}) does "
                      "not match other/device index delta ({} - {} = {})",
                      devs->size(), other_offset, dev_inode_offset_,
                      other_offset - dev_inode_offset_));
    }
  }
}

template <typename LoggerPolicy>
void metadata_v2_data::check_inode_size_cache(
    LOG_PROXY_REF(LoggerPolicy)) const {
  if (auto cache = meta_.reg_file_size_cache()) {
    LOG_DEBUG << "checking inode size cache";
    size_t errors{0};
    auto const min_chunk_count = cache->min_chunk_count();

    std::unordered_set<uint32_t> seen;

    for (int inode = file_inode_offset_; inode < dev_inode_offset_; ++inode) {
      auto iv = make_inode_view_impl(inode);
      auto expected = reg_file_size_impl(LOG_PROXY_ARG_ iv, false);
      auto size_cached = reg_file_size_impl(LOG_PROXY_ARG_ iv, true);

      if (size_cached != expected) {
        LOG_ERROR << "inode " << inode
                  << " cached/uncached size mismatch: " << size_cached
                  << " != " << expected;
        ++errors;
      }

      auto index = file_inode_to_chunk_index(inode);

      if (seen.contains(index)) {
        continue;
      }

      if (auto it = cache->lookup().find(index); it != cache->lookup().end()) {
        auto size = it->second();

        std::error_code ec;
        auto cr = get_chunk_range_from_index(index, ec);
        DWARFS_CHECK(
            !ec, fmt::format("get_chunk_range({}): {}", inode, ec.message()));

        LOG_TRACE << "checking inode " << inode << " [index=" << index
                  << "] size " << size << " (" << cr.size() << " chunks)";

        if (size != expected) {
          LOG_ERROR << "inode " << inode << " [" << index << "] size " << size
                    << " does not match expected " << expected;
          ++errors;
        }

        if (cr.size() < min_chunk_count) {
          LOG_ERROR << "inode " << inode << " [" << index << "] size " << size
                    << " has less than " << min_chunk_count
                    << " chunks: " << cr.size();
          ++errors;
        }

        seen.insert(index);
      }
    }

    for (auto entry : cache->lookup()) {
      auto index = entry.first();
      if (!seen.contains(index)) {
        LOG_ERROR << "unused inode size cache entry for index " << index
                  << " size " << entry.second();
        ++errors;
      }
    }

    if (errors > 0) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("inode size cache check failed: {} error(s)", errors));
    }
  }
}

template <typename LoggerPolicy>
std::vector<packed_int_vector<uint32_t>>
metadata_v2_data::build_dir_icase_cache(logger& lgr) const {
  std::vector<packed_int_vector<uint32_t>> cache;

  if (options_.case_insensitive_lookup) {
    LOG_PROXY(LoggerPolicy, lgr);
    auto ti = LOG_TIMED_INFO;
    size_t num_cached_dirs = 0;
    size_t num_cached_files = 0;
    size_t total_cache_size = 0;

    cache.resize(meta_.directories().size());

    for (uint32_t inode = 0; inode < meta_.directories().size() - 1; ++inode) {
      directory_view dir{inode, global_};
      auto range = dir.entry_range();

      // Cache the folded names of the directory entries; this significantly
      // speeds up the sorting code.
      std::vector<std::string> names(range.size());
      std::transform(range.begin(), range.end(), names.begin(), [&](auto ix) {
        return utf8_case_fold_unchecked(dir_entry_view_impl::name(ix, global_));
      });

      // Check and report any collisions in the directory
      phmap::flat_hash_map<std::string_view, folly::small_vector<uint32_t, 1>>
          collisions;
      collisions.reserve(range.size());
      for (size_t i = 0; i < names.size(); ++i) {
        collisions[names[i]].push_back(i);
      }
      for (auto& [name, indices] : collisions) {
        if (indices.size() > 1) {
          LOG_WARN << fmt::format(
              "case-insensitive collision in directory \"{}\" (inode={}): {}",
              dir.self_entry_view().unix_path(), inode,
              fmt::join(indices | ranges::views::transform([&](auto i) {
                          return dir_entry_view_impl::name(range[i], global_);
                        }),
                        ", "));
        }
      }

      // It's faster to check here if the folded names are sorted than to
      // check later if the indices in `entries` are sorted.
      if (!std::ranges::is_sorted(names)) {
        std::vector<uint32_t> entries(range.size());
        std::iota(entries.begin(), entries.end(), 0);
        std::ranges::stable_sort(
            entries, [&](auto a, auto b) { return names[a] < names[b]; });
        auto& pv = cache[inode];
        pv.reset(std::bit_width(entries.size()), entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
          pv.set(i, entries[i]);
        }
        ++num_cached_dirs;
        num_cached_files += entries.size();
        total_cache_size += pv.size_in_bytes();
      }
    }

    ti << "built case-insensitive directory cache for " << num_cached_files
       << " entries in " << num_cached_dirs << " out of "
       << meta_.directories().size() - 1 << " directories ("
       << size_with_unit(total_cache_size +
                         sizeof(decltype(cache)::value_type) * cache.size())
       << ")";
  }

  return cache;
}

template <typename LoggerPolicy>
packed_int_vector<uint32_t>
metadata_v2_data::build_nlinks(logger& lgr,
                               metadata_options const& options) const {
  packed_int_vector<uint32_t> packed_nlinks;

  if (options.enable_nlink && dev_inode_offset_ > file_inode_offset_) {
    LOG_PROXY(LoggerPolicy, lgr);
    auto td = LOG_TIMED_DEBUG;

    std::vector<uint32_t> nlinks(dev_inode_offset_ - file_inode_offset_);

    auto add_link = [&](int index) {
      if (index >= 0 && std::cmp_less(index, nlinks.size())) {
        ++nlinks[index];
      }
    };

    if (auto de = meta_.dir_entries()) {
      for (auto e : *de) {
        add_link(int(e.inode_num()) - file_inode_offset_);
      }
    } else {
      for (auto e : meta_.inodes()) {
        add_link(int(e.inode_v2_2()) - file_inode_offset_);
      }
    }

    {
      auto tt = LOG_TIMED_TRACE;

      uint32_t max = *std::ranges::max_element(nlinks);
      packed_nlinks.reset(std::bit_width(max), nlinks.size());

      for (size_t i = 0; i < nlinks.size(); ++i) {
        packed_nlinks.set(i, nlinks[i]);
      }

      tt << "packed hardlink table from "
         << size_with_unit(sizeof(nlinks.front()) * nlinks.size()) << " to "
         << size_with_unit(packed_nlinks.size_in_bytes());
    }

    td << "built hardlink table (" << packed_nlinks.size() << " entries, "
       << size_with_unit(packed_nlinks.size_in_bytes()) << ")";
  }

  return packed_nlinks;
}

template <typename LoggerPolicy>
packed_int_vector<uint32_t>
metadata_v2_data::unpack_chunk_table(logger& lgr) const {
  packed_int_vector<uint32_t> chunk_table;

  if (auto opts = meta_.options(); opts and opts->packed_chunk_table()) {
    LOG_PROXY(LoggerPolicy, lgr);
    auto td = LOG_TIMED_DEBUG;

    chunk_table.reset(std::bit_width(meta_.chunks().size()));
    chunk_table.reserve(meta_.chunk_table().size());
    std::partial_sum(meta_.chunk_table().begin(), meta_.chunk_table().end(),
                     std::back_inserter(chunk_table));

    td << "unpacked chunk table with " << chunk_table.size() << " entries ("
       << size_with_unit(chunk_table.size_in_bytes()) << ")";
  }

  return chunk_table;
}

template <typename LoggerPolicy>
packed_int_vector<uint32_t>
metadata_v2_data::unpack_shared_files(logger& lgr) const {
  packed_int_vector<uint32_t> unpacked;

  if (auto opts = meta_.options(); opts and opts->packed_shared_files_table()) {
    if (auto sfp = meta_.shared_files_table(); sfp and !sfp->empty()) {
      LOG_PROXY(LoggerPolicy, lgr);
      auto td = LOG_TIMED_DEBUG;

      auto size = std::accumulate(sfp->begin(), sfp->end(), 2 * sfp->size());
      unpacked.reset(std::bit_width(sfp->size()), size);

      uint32_t target = 0;
      size_t index = 0;

      for (auto c : *sfp) {
        for (size_t i = 0; i < c + 2; ++i) {
          unpacked.set(index++, target);
        }

        ++target;
      }

      DWARFS_CHECK(unpacked.size() == size,
                   "unexpected unpacked shared files count");

      td << "unpacked shared files table with " << unpacked.size()
         << " entries (" << size_with_unit(unpacked.size_in_bytes()) << ")";
    }
  }

  return unpacked;
}

void metadata_v2_data::analyze_chunks(std::ostream& os) const {
  folly::Histogram<size_t> block_refs{1, 0, 1024};
  folly::Histogram<size_t> chunk_count{1, 0, 65536};
  size_t mergeable_chunks{0};

  for (size_t i = 1; i < meta_.chunk_table().size(); ++i) {
    uint32_t beg = chunk_table_lookup(i - 1);
    uint32_t end = chunk_table_lookup(i);
    uint32_t num = end - beg;

    assert(beg <= end);

    if (num > 1) {
      std::unordered_set<size_t> blocks;

      for (uint32_t k = beg; k < end; ++k) {
        auto chk = meta_.chunks()[k];
        blocks.emplace(chk.block());

        if (k > beg) {
          auto prev = meta_.chunks()[k - 1];
          if (prev.block() == chk.block()) {
            if (prev.offset() + prev.size() == chk.offset()) {
              ++mergeable_chunks;
            }
          }
        }
      }

      block_refs.addValue(blocks.size());
    } else {
      block_refs.addValue(num);
    }

    chunk_count.addValue(num);
  }

  {
    auto pct = [&](double p) { return block_refs.getPercentileEstimate(p); };

    os << "single file block refs p50: " << pct(0.5) << ", p75: " << pct(0.75)
       << ", p90: " << pct(0.9) << ", p95: " << pct(0.95)
       << ", p99: " << pct(0.99) << ", p99.9: " << pct(0.999) << "\n";
  }

  {
    auto pct = [&](double p) { return chunk_count.getPercentileEstimate(p); };

    os << "single file chunk count p50: " << pct(0.5) << ", p75: " << pct(0.75)
       << ", p90: " << pct(0.9) << ", p95: " << pct(0.95)
       << ", p99: " << pct(0.99) << ", p99.9: " << pct(0.999) << "\n";
  }

  // TODO: we can remove this once we have no more mergeable chunks :-)
  os << "mergeable chunks: " << mergeable_chunks << "/" << meta_.chunks().size()
     << "\n";
}

std::string metadata_v2_data::link_value(inode_view_impl const& iv,
                                         readlink_mode mode) const {
  std::string rv =
      symlinks_[meta_.symlink_table()[iv.inode_num() - symlink_inode_offset_]];

  if (mode != readlink_mode::raw) {
    char meta_preferred = '/';
    if (auto ps = meta_.preferred_path_separator()) {
      meta_preferred = static_cast<char>(*ps);
    }
    char host_preferred =
        static_cast<char>(std::filesystem::path::preferred_separator);
    if (mode == readlink_mode::posix) {
      host_preferred = '/';
    }
    if (meta_preferred != host_preferred) {
      std::ranges::replace(rv, meta_preferred, host_preferred);
    }
  }

  return rv;
}

void metadata_v2_data::statvfs(vfs_stat* stbuf) const {
  *stbuf = {};

  // Make sure bsize and frsize are the same, as doing otherwise can confuse
  // some applications (such as `duf`).
  stbuf->bsize = 1UL;
  stbuf->frsize = 1UL;
  stbuf->blocks = meta_.total_fs_size();
  if (!options_.enable_nlink) {
    if (auto ths = meta_.total_hardlink_size()) {
      stbuf->blocks += *ths;
    }
  }
  stbuf->files = inode_count_;
  stbuf->readonly = true;
  stbuf->namemax = PATH_MAX;
}

int metadata_v2_data::file_inode_to_chunk_index(int inode) const {
  inode -= file_inode_offset_;

  if (inode >= unique_files_) {
    inode -= unique_files_;

    if (!shared_files_.empty()) {
      if (std::cmp_less(inode, shared_files_.size())) {
        inode = shared_files_[inode] + unique_files_;
      }
    } else if (auto sfp = meta_.shared_files_table()) {
      if (std::cmp_less(inode, sfp->size())) {
        inode = (*sfp)[inode] + unique_files_;
      }
    }
  }

  return inode;
}

template <typename TraceFunc>
size_t
metadata_v2_data::reg_file_size_impl(inode_view_impl const& iv, bool use_cache,
                                     TraceFunc const& trace) const {
  PERFMON_CLS_SCOPED_SECTION(reg_file_size)

  // Looking up the chunk range is cheap, and we likely have to do it anyway
  std::error_code ec;
  auto inode = iv.inode_num();
  auto index = file_inode_to_chunk_index(inode);
  auto cr = get_chunk_range_from_index(index, ec);
  DWARFS_CHECK(!ec,
               fmt::format("get_chunk_range({}): {}", inode, ec.message()));

  if (use_cache) {
    if (auto cache = meta_.reg_file_size_cache()) {
      if (cr.size() >= cache->min_chunk_count()) {
        trace(index);
        if (auto size = cache->lookup().getOptional(index)) {
          return *size;
        }
      }
    }
  }

  // This is the expensive part for highly fragmented inodes
  return std::accumulate(cr.begin(), cr.end(), static_cast<size_t>(0),
                         [](size_t s, chunk_view cv) { return s + cv.size(); });
}

std::string metadata_v2_data::serialize_as_json(bool simple) const {
  using namespace apache::thrift;
  std::string json;
  if (simple) {
    SimpleJSONSerializer::serialize(unpack_metadata(), &json);
  } else {
    JSONSerializer::serialize(unpack_metadata(), &json);
  }
  return json;
}

nlohmann::json metadata_v2_data::as_json(directory_view dir,
                                         dir_entry_view const& entry) const {
  nlohmann::json arr = nlohmann::json::array();

  auto range = dir.entry_range();

  for (auto i : range) {
    arr.push_back(as_json(make_dir_entry_view(i, entry.raw().self_index())));
  }

  return arr;
}

nlohmann::json metadata_v2_data::as_json(dir_entry_view const& entry) const {
  nlohmann::json obj;

  auto iv = entry.inode();
  auto mode = iv.mode();
  auto inode = iv.inode_num();

  obj["mode"] = mode;
  obj["modestring"] = file_stat::mode_string(mode);
  obj["inode"] = inode;

  if (inode > 0) {
    obj["name"] = entry.name();
  }

  switch (posix_file_type::from_mode(mode)) {
  case posix_file_type::regular:
    obj["type"] = "file";
    obj["size"] = reg_file_size_notrace(iv);
    break;

  case posix_file_type::directory:
    obj["type"] = "directory";
    obj["inodes"] = as_json(make_directory_view(iv), entry);
    break;

  case posix_file_type::symlink:
    obj["type"] = "link";
    obj["target"] = link_value(iv);
    break;

  case posix_file_type::block:
    obj["type"] = "blockdev";
    obj["device_id"] = get_device_id(inode).value_or(-1);
    break;

  case posix_file_type::character:
    obj["type"] = "chardev";
    obj["device_id"] = get_device_id(inode).value_or(-1);
    break;

  case posix_file_type::fifo:
    obj["type"] = "fifo";
    break;

  case posix_file_type::socket:
    obj["type"] = "socket";
    break;
  }

  return obj;
}

nlohmann::json metadata_v2_data::as_json() const {
  vfs_stat stbuf;
  statvfs(&stbuf);

  nlohmann::json obj{
      {"statvfs",
       {{"f_bsize", stbuf.bsize},
        {"f_files", stbuf.files},
        {"f_blocks", stbuf.blocks}}},
      {"root", as_json(root_)},
  };

  return obj;
}

nlohmann::json
metadata_v2_data::info_as_json(fsinfo_options const& opts,
                               filesystem_info const* fsinfo) const {
  nlohmann::json info;
  vfs_stat stbuf;
  statvfs(&stbuf);

  if (auto version = meta_.dwarfs_version()) {
    info["created_by"] = version.value();
  }

  if (auto ts = meta_.create_timestamp()) {
    info["created_on"] = fmt::format("{:%FT%T}", safe_localtime(ts.value()));
  }

  if (opts.features.has(fsinfo_feature::metadata_summary)) {
    info["block_size"] = meta_.block_size();
    if (fsinfo) {
      info["block_count"] = fsinfo->block_count;
    }
    info["inode_count"] = stbuf.files;
    if (auto ps = meta_.preferred_path_separator()) {
      info["preferred_path_separator"] = std::string(1, static_cast<char>(*ps));
    }
    info["original_filesystem_size"] = stbuf.blocks;
    if (fsinfo) {
      info["compressed_block_size"] = fsinfo->compressed_block_size;
      if (!fsinfo->uncompressed_block_size_is_estimate) {
        info["uncompressed_block_size"] = fsinfo->uncompressed_block_size;
      }
      info["compressed_metadata_size"] = fsinfo->compressed_metadata_size;
      if (!fsinfo->uncompressed_metadata_size_is_estimate) {
        info["uncompressed_metadata_size"] = fsinfo->uncompressed_metadata_size;
      }
    }
    if (auto ths = meta_.total_hardlink_size()) {
      info["total_hardlink_size"] = *ths;
    }

    if (auto opt = meta_.options()) {
      nlohmann::json options;
      parse_metadata_options(meta_, [&](auto const& name, bool value) {
        if (value) {
          options.push_back(name);
        }
      });
      info["options"] = std::move(options);
      if (auto res = opt->time_resolution_sec()) {
        info["time_resolution"] = *res;
      }
    }

    if (meta_.block_categories()) {
      auto catnames = *meta_.category_names();
      auto catinfo = get_category_info(meta_, fsinfo);
      nlohmann::json& categories = info["categories"];
      for (auto const& [category, ci] : catinfo) {
        std::string name{catnames[category]};
        categories[name] = {
            {"block_count", ci.count},
        };
        if (ci.compressed_size) {
          categories[name]["compressed_size"] = ci.compressed_size.value();
        }
        if (ci.uncompressed_size && !ci.uncompressed_size_is_estimate) {
          categories[name]["uncompressed_size"] = ci.uncompressed_size.value();
        }
      }
    }
  }

  if (opts.features.has(fsinfo_feature::metadata_details)) {
    nlohmann::json meta;

    meta["symlink_inode_offset"] = symlink_inode_offset_;
    meta["file_inode_offset"] = file_inode_offset_;
    meta["dev_inode_offset"] = dev_inode_offset_;
    meta["chunks"] = meta_.chunks().size();
    meta["directories"] = meta_.directories().size();
    meta["inodes"] = meta_.inodes().size();
    meta["chunk_table"] = meta_.chunk_table().size();
    meta["entry_table_v2_2"] = meta_.entry_table_v2_2().size();
    meta["symlink_table"] = meta_.symlink_table().size();
    meta["uids"] = meta_.uids().size();
    meta["gids"] = meta_.gids().size();
    meta["modes"] = meta_.modes().size();
    meta["names"] = meta_.names().size();
    meta["symlinks"] = meta_.symlinks().size();

    if (auto dev = meta_.devices()) {
      meta["devices"] = dev->size();
    }

    if (auto de = meta_.dir_entries()) {
      meta["dir_entries"] = de->size();
    }

    if (auto sfp = meta_.shared_files_table()) {
      if (meta_.options()->packed_shared_files_table()) {
        meta["packed_shared_files_table"] = sfp->size();
        meta["unpacked_shared_files_table"] = shared_files_.size();
      } else {
        meta["shared_files_table"] = sfp->size();
      }
      meta["unique_files"] = unique_files_;
    }

    info["meta"] = std::move(meta);
  }

  if (opts.features.has(fsinfo_feature::directory_tree)) {
    info["root"] = as_json(root_);
  }

  return info;
}

// TODO: can we move this to dir_entry_view?
void metadata_v2_data::dump(
    std::ostream& os, std::string const& indent, directory_view dir,
    dir_entry_view const& entry, fsinfo_options const& opts,
    std::function<void(std::string const&, uint32_t)> const& icb) const {
  auto range = dir.entry_range();

  os << " (" << range.size() << " entries, parent=" << dir.parent_entry()
     << ")\n";

  for (auto i : range) {
    dump(os, indent, make_dir_entry_view(i, entry.raw().self_index()), opts,
         icb);
  }
}

void metadata_v2_data::dump(
    std::ostream& os, fsinfo_options const& opts, filesystem_info const* fsinfo,
    std::function<void(std::string const&, uint32_t)> const& icb) const {
  vfs_stat stbuf;
  statvfs(&stbuf);

  if (auto version = meta_.dwarfs_version()) {
    os << "created by: " << *version << "\n";
  }

  if (auto ts = meta_.create_timestamp()) {
    os << "created on: " << fmt::format("{:%F %T}", safe_localtime(*ts))
       << "\n";
  }

  if (opts.features.has(fsinfo_feature::metadata_summary)) {
    os << "block size: " << size_with_unit(meta_.block_size()) << "\n";
    if (fsinfo) {
      os << "block count: " << fsinfo->block_count << "\n";
    }
    os << "inode count: " << stbuf.files << "\n";
    if (auto ps = meta_.preferred_path_separator()) {
      os << "preferred path separator: " << static_cast<char>(*ps) << "\n";
    }
    os << "original filesystem size: " << size_with_unit(stbuf.blocks) << "\n";
    if (fsinfo) {
      os << "compressed block size: "
         << size_with_unit(fsinfo->compressed_block_size);
      if (!fsinfo->uncompressed_block_size_is_estimate) {
        os << fmt::format(" ({0:.2f}%)",
                          (100.0 * fsinfo->compressed_block_size) /
                              fsinfo->uncompressed_block_size);
      }
      os << "\n";
      os << "uncompressed block size: ";
      if (fsinfo->uncompressed_block_size_is_estimate) {
        os << "(at least) ";
      }
      os << size_with_unit(fsinfo->uncompressed_block_size) << "\n";
      os << "compressed metadata size: "
         << size_with_unit(fsinfo->compressed_metadata_size);
      if (!fsinfo->uncompressed_metadata_size_is_estimate) {
        os << fmt::format(" ({0:.2f}%)",
                          (100.0 * fsinfo->compressed_metadata_size) /
                              fsinfo->uncompressed_metadata_size);
      }
      os << "\n";
      os << "uncompressed metadata size: ";
      if (fsinfo->uncompressed_metadata_size_is_estimate) {
        os << "(at least) ";
      }
      os << size_with_unit(fsinfo->uncompressed_metadata_size) << "\n";
    }
    if (auto ths = meta_.total_hardlink_size()) {
      os << "total hardlink size: " << size_with_unit(*ths) << "\n";
    }
    if (auto opt = meta_.options()) {
      std::vector<std::string> options;
      parse_metadata_options(meta_, [&](auto const& name, bool value) {
        if (value) {
          options.emplace_back(name);
        }
      });
      os << "options: " << boost::join(options, "\n         ") << "\n";
      if (auto res = opt->time_resolution_sec()) {
        os << "time resolution: " << *res << " seconds\n";
      }
    }

    if (meta_.block_categories()) {
      auto catnames = *meta_.category_names();
      auto catinfo = get_category_info(meta_, fsinfo);
      os << "categories:\n";
      for (auto const& [category, ci] : catinfo) {
        os << "  " << catnames[category] << ": " << ci.count << " blocks";
        if (ci.compressed_size) {
          if (ci.uncompressed_size_is_estimate ||
              ci.uncompressed_size.value() != ci.compressed_size.value()) {
            os << ", " << size_with_unit(ci.compressed_size.value())
               << " compressed";
          }
          if (!ci.uncompressed_size_is_estimate) {
            os << ", " << size_with_unit(ci.uncompressed_size.value())
               << " uncompressed";
            if (ci.uncompressed_size.value() != ci.compressed_size.value()) {
              os << fmt::format(" ({0:.2f}%)",
                                (100.0 * ci.compressed_size.value()) /
                                    ci.uncompressed_size.value());
            }
          }
        }
        os << "\n";
      }
    }
  }

  if (opts.features.has(fsinfo_feature::frozen_analysis)) {
    analyze_frozen(os, meta_, data_.size());
  }

  if (opts.features.has(fsinfo_feature::frozen_layout)) {
    get_layout(meta_).print(os, 0);
    os << '\n';
  }

  if (opts.features.has(fsinfo_feature::schema_raw_dump)) {
    os << ::apache::thrift::debugString(deserialize_schema(schema_)) << '\n';
  }

  if (opts.features.has(fsinfo_feature::metadata_details)) {
    os << "symlink_inode_offset: " << symlink_inode_offset_ << "\n";
    os << "file_inode_offset: " << file_inode_offset_ << "\n";
    os << "dev_inode_offset: " << dev_inode_offset_ << "\n";
    os << "chunks: " << meta_.chunks().size() << "\n";
    os << "directories: " << meta_.directories().size() << "\n";
    os << "inodes: " << meta_.inodes().size() << "\n";
    os << "chunk_table: " << meta_.chunk_table().size() << "\n";
    os << "entry_table_v2_2: " << meta_.entry_table_v2_2().size() << "\n";
    os << "symlink_table: " << meta_.symlink_table().size() << "\n";
    os << "uids: " << meta_.uids().size() << "\n";
    os << "gids: " << meta_.gids().size() << "\n";
    os << "modes: " << meta_.modes().size() << "\n";
    os << "names: " << meta_.names().size() << "\n";
    os << "symlinks: " << meta_.symlinks().size() << "\n";
    if (auto dev = meta_.devices()) {
      os << "devices: " << dev->size() << "\n";
    }
    if (auto de = meta_.dir_entries()) {
      os << "dir_entries: " << de->size() << "\n";
    }
    if (auto sfp = meta_.shared_files_table()) {
      if (meta_.options()->packed_shared_files_table()) {
        os << "packed shared_files_table: " << sfp->size() << "\n";
        os << "unpacked shared_files_table: " << shared_files_.size() << "\n";
      } else {
        os << "shared_files_table: " << sfp->size() << "\n";
      }
      os << "unique files: " << unique_files_ << "\n";
    }
    analyze_chunks(os);
  }

  if (opts.features.has(fsinfo_feature::metadata_full_dump)) {
    os << ::apache::thrift::debugString(meta_.thaw()) << '\n';
  }

  if (opts.features.has(fsinfo_feature::directory_tree)) {
    dump(os, "", root_, opts, icb);
  }
}

void metadata_v2_data::dump(
    std::ostream& os, std::string const& indent, dir_entry_view const& entry,
    fsinfo_options const& opts,
    std::function<void(std::string const&, uint32_t)> const& icb) const {
  auto iv = entry.inode();
  auto mode = iv.mode();
  auto inode = iv.inode_num();

  os << indent << "<inode:" << inode << "> " << file_stat::mode_string(mode);

  if (inode > 0) {
    os << " " << entry.name();
  }

  switch (posix_file_type::from_mode(mode)) {
  case posix_file_type::regular: {
    std::error_code ec;
    auto cr = get_chunk_range(inode, ec);
    DWARFS_CHECK(!ec,
                 fmt::format("get_chunk_range({}): {}", inode, ec.message()));
    os << " [" << cr.begin_ << ", " << cr.end_ << "]";
    os << " " << reg_file_size_notrace(iv) << "\n";
    if (opts.features.has(fsinfo_feature::chunk_details)) {
      icb(indent + "  ", inode);
    }
  } break;

  case posix_file_type::directory:
    dump(os, indent + "  ", make_directory_view(iv), entry, opts, icb);
    break;

  case posix_file_type::symlink:
    os << " -> " << link_value(iv) << "\n";
    break;

  case posix_file_type::block:
    os << " (block device: " << get_device_id(inode).value_or(-1) << ")\n";
    break;

  case posix_file_type::character:
    os << " (char device: " << get_device_id(inode).value_or(-1) << ")\n";
    break;

  case posix_file_type::fifo:
    os << " (named pipe)\n";
    break;

  case posix_file_type::socket:
    os << " (socket)\n";
    break;
  }
}

thrift::metadata::metadata metadata_v2_data::unpack_metadata() const {
  PERFMON_CLS_SCOPED_SECTION(unpack_metadata)

  auto meta = meta_.thaw();

  if (auto opts = meta.options()) {
    if (opts->packed_chunk_table().value()) {
      meta.chunk_table() = chunk_table_.unpack();
    }
    if (auto const& dirs = global_.bundled_directories()) {
      meta.directories() = dirs->thaw();
    }
    if (opts->packed_shared_files_table().value()) {
      meta.shared_files_table() = shared_files_.unpack();
    }
    if (auto const& names = global_.names(); names.is_packed()) {
      meta.names() = names.unpack();
      meta.compact_names().reset();
    }
    if (symlinks_.is_packed()) {
      meta.symlinks() = symlinks_.unpack();
      meta.compact_symlinks().reset();
    }
    opts->packed_chunk_table() = false;
    opts->packed_directories() = false;
    opts->packed_shared_files_table() = false;
  }

  return meta;
}

template <typename LoggerPolicy, typename T>
void metadata_v2_data::walk(LOG_PROXY_REF_(LoggerPolicy) uint32_t self_index,
                            uint32_t parent_index, set_type<int>& seen,
                            T const& func) const {
  func(self_index, parent_index);

  auto entry = make_dir_entry_view_impl(self_index, parent_index);
  auto iv = entry.inode();

  if (iv.is_directory()) {
    auto inode = iv.inode_num();

    if (!seen.emplace(inode).second) {
      DWARFS_THROW(runtime_error, "cycle detected during directory walk");
    }

    auto dir = make_directory_view(inode);

    for (auto cur_index : dir.entry_range()) {
      walk(LOG_PROXY_ARG_ cur_index, self_index, seen, func);
    }

    seen.erase(inode);
  }
}

template <typename LoggerPolicy>
void metadata_v2_data::walk_data_order_impl(LOG_PROXY_REF_(
    LoggerPolicy) std::function<void(dir_entry_view)> const& func) const {
  std::vector<std::pair<uint32_t, uint32_t>> entries;

  {
    auto tv = LOG_TIMED_VERBOSE;

    if (auto dep = meta_.dir_entries()) {
      // 1. collect and partition non-files / files
      entries.resize(dep->size());

      auto const num_files = total_file_entries();
      auto mid = entries.end() - num_files;

      // we use this first to build a mapping from self_index to inode number
      std::vector<uint32_t> first_chunk_block(dep->size());

      {
        auto td = LOG_TIMED_DEBUG;

        size_t other_ix = 0;
        size_t file_ix = entries.size() - num_files;

        walk_tree(LOG_PROXY_ARG_[&, de = *dep, beg = file_inode_offset_,
                                 end = dev_inode_offset_](
            uint32_t self_index, uint32_t parent_index) {
          int ino = de[self_index].inode_num();
          size_t index;

          if (beg <= ino && ino < end) {
            index = file_ix++;
            first_chunk_block[self_index] = ino;
          } else {
            index = other_ix++;
          }

          entries[index] = {self_index, parent_index};
        });

        DWARFS_CHECK(file_ix == entries.size(),
                     fmt::format("unexpected file index: {} != {}", file_ix,
                                 entries.size()));
        DWARFS_CHECK(other_ix == entries.size() - num_files,
                     fmt::format("unexpected other index: {} != {}", other_ix,
                                 entries.size() - num_files));

        td << "collected " << entries.size() << " entries ("
           << std::distance(entries.begin(), mid) << " non-files and "
           << std::distance(mid, entries.end()) << " files)";
      }

      // 2. order files by chunk block number
      // 2a. build mapping inode -> first chunk block

      {
        auto td = LOG_TIMED_DEBUG;

        for (auto& fcb : first_chunk_block) {
          int ino = fcb;
          if (ino >= file_inode_offset_) {
            ino = file_inode_to_chunk_index(ino);
            if (auto beg = chunk_table_lookup(ino);
                beg != chunk_table_lookup(ino + 1)) {
              fcb = meta_.chunks()[beg].block();
            }
          }
        }

        td << "prepare first chunk block vector";
      }

      // 2b. sort second partition accordingly
      {
        auto td = LOG_TIMED_DEBUG;

        std::stable_sort(mid, entries.end(),
                         [&first_chunk_block](auto const& a, auto const& b) {
                           return first_chunk_block[a.first] <
                                  first_chunk_block[b.first];
                         });

        td << "final sort of " << std::distance(mid, entries.end())
           << " file entries";
      }
    } else {
      entries.reserve(meta_.inodes().size());

      walk_tree(LOG_PROXY_ARG_[&](uint32_t self_index, uint32_t parent_index) {
        entries.emplace_back(self_index, parent_index);
      });

      std::sort(entries.begin(), entries.end(),
                [this](auto const& a, auto const& b) {
                  return meta_.inodes()[a.first].inode_v2_2() <
                         meta_.inodes()[b.first].inode_v2_2();
                });
    }

    tv << "ordered " << entries.size() << " entries by file data order";
  }

  for (auto [self_index, parent_index] : entries) {
    walk_call(func, self_index, parent_index);
  }
}

std::optional<dir_entry_view>
metadata_v2_data::find(directory_view dir, std::string_view name) const {
  PERFMON_CLS_SCOPED_SECTION(find)

  auto range = dir.entry_range();

  if (!options_.case_insensitive_lookup) {
    return find_impl(dir, range, name, std::identity{}, std::identity{});
  }

  auto const& cache = dir_icase_cache_[dir.inode()];

  return find_impl(
      dir, boost::irange(range.size()), utf8_case_fold(name),
      [&cache, &range](auto ix) {
        if (!cache.empty()) {
          ix = cache[ix];
        }
        return range[ix];
      },
      utf8_case_fold_unchecked);
}

std::optional<dir_entry_view>
metadata_v2_data::find(std::string_view path) const {
  PERFMON_CLS_SCOPED_SECTION(find_path)

  auto start = path.find_first_not_of('/');

  if (start != std::string_view::npos) {
    path.remove_prefix(start);
  } else {
    path = {};
  }

  auto dev = std::make_optional(root_);

  while (!path.empty()) {
    auto iv = dev->inode();

    if (!iv.is_directory()) {
      dev.reset();
      break;
    }

    auto name = path;

    if (auto sep = path.find('/'); sep != std::string_view::npos) {
      name = path.substr(0, sep);
      path.remove_prefix(sep + 1);
    } else {
      path = {};
    }

    dev = find(make_directory_view(iv), name);

    if (!dev) {
      break;
    }
  }

  return dev;
}

std::optional<dir_entry_view>
metadata_v2_data::find_impl(directory_view dir, auto const& range,
                            auto const& name, auto const& index_map,
                            auto const& entry_name_transform) const {
  auto entry_name = [&](auto ix) {
    return entry_name_transform(dir_entry_view_impl::name(ix, global_));
  };

  auto it = std::lower_bound(range.begin(), range.end(), name,
                             [&](auto ix, auto const& name) {
                               return entry_name(index_map(ix)) < name;
                             });

  if (it != range.end()) {
    auto ix = index_map(*it);
    if (entry_name(ix) == name) {
      return dir_entry_view{dir_entry_view_impl::from_dir_entry_index_shared(
          ix, global_.self_dir_entry(dir.inode()), global_)};
    }
  }

  return std::nullopt;
}

template <typename LoggerPolicy>
file_stat metadata_v2_data::getattr_impl(LOG_PROXY_REF_(LoggerPolicy)
                                             inode_view const& iv,
                                         getattr_options const& opts) const {
  file_stat stbuf;

  stbuf.set_dev(0); // TODO: should we make this configurable?

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

  if (options_.readonly) {
    mode &= READ_ONLY_MASK;
  }

  stbuf.set_mode(mode);

  if (!opts.no_size) {
    stbuf.set_size(stbuf.is_directory() ? make_directory_view(iv).entry_count()
                                        : file_size(LOG_PROXY_ARG_ iv, mode));
    stbuf.set_blocks((stbuf.size_unchecked() + 511) / 512);
  }

  auto& ivr = iv.raw();

  stbuf.set_ino(inode + inode_offset_);
  stbuf.set_blksize(options_.block_size);
  stbuf.set_uid(options_.fs_uid.value_or(iv.getuid()));
  stbuf.set_gid(options_.fs_gid.value_or(iv.getgid()));
  stbuf.set_mtime(resolution * (timebase + ivr.mtime_offset()));

  if (mtime_only) {
    stbuf.set_atime(stbuf.mtime_unchecked());
    stbuf.set_ctime(stbuf.mtime_unchecked());
  } else {
    stbuf.set_atime(resolution * (timebase + ivr.atime_offset()));
    stbuf.set_ctime(resolution * (timebase + ivr.ctime_offset()));
  }

  stbuf.set_nlink(options_.enable_nlink && stbuf.is_regular_file()
                      ? DWARFS_NOTHROW(nlinks_.at(inode - file_inode_offset_))
                      : 1);

  stbuf.set_rdev(stbuf.is_device() ? get_device_id(inode).value() : 0);

  return stbuf;
}

std::optional<dir_entry_view>
metadata_v2_data::readdir(directory_view dir, size_t offset) const {
  PERFMON_CLS_SCOPED_SECTION(readdir)

  switch (offset) {
  case 0:
    return dir_entry_view{dir_entry_view_impl::from_dir_entry_index_shared(
        global_.self_dir_entry(dir.inode()), global_,
        dir_entry_view_impl::entry_name_type::self)};

  case 1:
    return dir_entry_view{dir_entry_view_impl::from_dir_entry_index_shared(
        global_.self_dir_entry(dir.parent_inode()), global_,
        dir_entry_view_impl::entry_name_type::parent)};

  default:
    offset -= 2;

    if (offset >= dir.entry_count()) {
      break;
    }

    auto index = dir.first_entry() + offset;
    return dir_entry_view{dir_entry_view_impl::from_dir_entry_index_shared(
        index, global_.self_dir_entry(dir.inode()), global_)};
  }

  return std::nullopt;
}

void metadata_v2_data::access(inode_view const& iv, int mode,
                              file_stat::uid_type uid, file_stat::gid_type gid,
                              std::error_code& ec) const {
  if (mode == F_OK) {
    // easy; we're only interested in the file's existence
    ec.clear();
    return;
  }

  int access_mode = 0;

  auto set_xok = [&access_mode]() {
#ifdef _WIN32
    access_mode |= 1; // Windows has no notion of X_OK
#else
    access_mode |= X_OK;
#endif
  };

  if (uid == 0) {
    access_mode = R_OK | W_OK;

    if (iv.mode() & uint16_t(fs::perms::owner_exec | fs::perms::group_exec |
                             fs::perms::others_exec)) {
      set_xok();
    }
  } else {
    auto test = [e_mode = iv.mode(), &access_mode, &set_xok,
                 readonly = options_.readonly](fs::perms r_bit, fs::perms w_bit,
                                               fs::perms x_bit) {
      if (e_mode & uint16_t(r_bit)) {
        access_mode |= R_OK;
      }
      if (e_mode & uint16_t(w_bit)) {
        if (!readonly) {
          access_mode |= W_OK;
        }
      }
      if (e_mode & uint16_t(x_bit)) {
        set_xok();
      }
    };

    // Let's build the inode's access mask
    test(fs::perms::others_read, fs::perms::others_write,
         fs::perms::others_exec);

    if (iv.getgid() == gid) {
      test(fs::perms::group_read, fs::perms::group_write,
           fs::perms::group_exec);
    }

    if (iv.getuid() == uid) {
      test(fs::perms::owner_read, fs::perms::owner_write,
           fs::perms::owner_exec);
    }
  }

  if ((access_mode & mode) == mode) {
    ec.clear();
  } else {
    ec = std::make_error_code(std::errc::permission_denied);
  }
}

nlohmann::json metadata_v2_data::get_inode_info(inode_view const& iv,
                                                size_t max_chunks) const {
  nlohmann::json obj;

  if (iv.is_regular_file()) {
    std::error_code ec;
    auto chunk_range = get_chunk_range(iv.inode_num(), ec);

    DWARFS_CHECK(!ec, fmt::format("get_chunk_range({}): {}", iv.inode_num(),
                                  ec.message()));

    if (chunk_range.size() <= max_chunks) {
      for (auto const& chunk : chunk_range) {
        nlohmann::json& chk = obj["chunks"].emplace_back();

        chk["block"] = chunk.block();
        chk["offset"] = chunk.offset();
        chk["size"] = chunk.size();

        if (auto catname = get_block_category(chunk.block())) {
          chk["category"] = catname.value();
        }
      }
    } else {
      obj["chunks"] = fmt::format("too many chunks ({})", chunk_range.size());
    }
  }

  obj["mode"] = iv.mode();
  obj["modestring"] = file_stat::mode_string(iv.mode());
  obj["uid"] = iv.getuid();
  obj["gid"] = iv.getgid();

  return obj;
}

std::vector<file_stat::uid_type> metadata_v2_data::get_all_uids() const {
  std::vector<file_stat::uid_type> rv;
  rv.resize(meta_.uids().size());
  std::copy(meta_.uids().begin(), meta_.uids().end(), rv.begin());
  return rv;
}

std::vector<file_stat::gid_type> metadata_v2_data::get_all_gids() const {
  std::vector<file_stat::gid_type> rv;
  rv.resize(meta_.gids().size());
  std::copy(meta_.gids().begin(), meta_.gids().end(), rv.begin());
  return rv;
}

std::vector<size_t> metadata_v2_data::get_block_numbers_by_category(
    std::string_view category) const {
  std::vector<size_t> rv;

  if (auto catnames = meta_.category_names()) {
    if (auto categories = meta_.block_categories()) {
      std::optional<size_t> category_num;

      for (size_t catnum = 0; catnum < catnames.value().size(); ++catnum) {
        if (catnames.value()[catnum] == category) {
          category_num = catnum;
          break;
        }
      }

      if (category_num) {
        for (size_t blknum = 0; blknum < categories.value().size(); ++blknum) {
          if (categories.value()[blknum] == *category_num) {
            rv.push_back(blknum);
          }
        }
      }
    }
  }

  return rv;
}

template <typename LoggerPolicy>
class metadata_ final : public metadata_v2::impl {
 public:
  metadata_(logger& lgr, std::span<uint8_t const> schema,
            std::span<uint8_t const> data, metadata_options const& options,
            int inode_offset, bool force_consistency_check,
            std::shared_ptr<performance_monitor const> const& perfmon)
      : LOG_PROXY_INIT(lgr)
      , data_{LoggerPolicy{},
              lgr,
              schema,
              data,
              options,
              inode_offset,
              force_consistency_check,
              perfmon} {}

  void check_consistency() const override {
    data_.check_consistency(LOG_PROXY_ARG);
  }

  size_t size() const override { return data_.size(); }

  void walk(std::function<void(dir_entry_view)> const& func) const override {
    data_.walk(LOG_PROXY_ARG_ func);
  }

  void walk_data_order(
      std::function<void(dir_entry_view)> const& func) const override {
    data_.walk_data_order(LOG_PROXY_ARG_ func);
  }

  dir_entry_view root() const override { return data_.root(); }

  std::optional<dir_entry_view> find(std::string_view path) const override {
    return data_.find(path);
  }

  std::optional<inode_view> find(int inode) const override {
    return data_.get_entry(inode);
  }

  std::optional<dir_entry_view>
  find(int inode, std::string_view name) const override {
    if (auto iv = data_.get_entry(inode); iv and iv->is_directory()) {
      return data_.find(data_.make_directory_view(*iv), name);
    }

    return std::nullopt;
  }

  file_stat getattr(inode_view iv, std::error_code& /*ec*/) const override {
    return data_.getattr(LOG_PROXY_ARG_ iv);
  }

  file_stat getattr(inode_view iv, getattr_options const& opts,
                    std::error_code& /*ec*/) const override {
    return data_.getattr(LOG_PROXY_ARG_ iv, opts);
  }

  std::optional<directory_view> opendir(inode_view iv) const override {
    std::optional<directory_view> rv;

    if (iv.is_directory()) {
      rv = data_.make_directory_view(iv);
    }

    return rv;
  }

  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const override {
    return data_.readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const override {
    return 2 + dir.entry_count(); // adds '.' and '..', which we fake in ;-)
  }

  void access(inode_view iv, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const override {
    LOG_DEBUG << fmt::format("access([{}, {:o}, {}, {}], {:o}, {}, {})",
                             iv.inode_num(), iv.mode(), iv.getuid(),
                             iv.getgid(), mode, uid, gid);

    data_.access(iv, mode, uid, gid, ec);
  }

  int open(inode_view iv, std::error_code& ec) const override {
    if (iv.is_regular_file()) {
      ec.clear();
      return iv.inode_num();
    }

    ec = std::make_error_code(std::errc::invalid_argument);
    return 0;
  }

  std::string readlink(inode_view iv, readlink_mode mode,
                       std::error_code& ec) const override {
    if (iv.is_symlink()) {
      ec.clear();
      return data_.link_value(iv, mode);
    }

    ec = std::make_error_code(std::errc::invalid_argument);
    return {};
  }

  void statvfs(vfs_stat* stbuf) const override { data_.statvfs(stbuf); }

  chunk_range get_chunks(int inode, std::error_code& ec) const override {
    return data_.get_chunks(inode, ec);
  }

  size_t block_size() const override { return data_.block_size(); }

  bool has_symlinks() const override { return data_.has_symlinks(); }

  nlohmann::json
  get_inode_info(inode_view iv, size_t max_chunks) const override {
    return data_.get_inode_info(iv, max_chunks);
  }

  std::optional<std::string>
  get_block_category(size_t block_number) const override {
    return data_.get_block_category(block_number);
  }

  std::vector<std::string> get_all_block_categories() const override {
    return data_.get_all_block_categories();
  }

  std::vector<file_stat::uid_type> get_all_uids() const override {
    return data_.get_all_uids();
  }

  std::vector<file_stat::gid_type> get_all_gids() const override {
    return data_.get_all_gids();
  }

  std::vector<size_t>
  get_block_numbers_by_category(std::string_view category) const override {
    return data_.get_block_numbers_by_category(category);
  }

  metadata_v2_data const& internal_data() const override { return data_; }

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  metadata_v2_data const data_;
};

metadata_v2_utils::metadata_v2_utils(metadata_v2 const& meta)
    : data_{meta.internal_data()} {}

void metadata_v2_utils::dump(
    std::ostream& os, fsinfo_options const& opts, filesystem_info const* fsinfo,
    std::function<void(std::string const&, uint32_t)> const& icb) const {
  data_.dump(os, opts, fsinfo, icb);
}

nlohmann::json
metadata_v2_utils::info_as_json(fsinfo_options const& opts,
                                filesystem_info const* fsinfo) const {
  return data_.info_as_json(opts, fsinfo);
}

nlohmann::json metadata_v2_utils::as_json() const { return data_.as_json(); }

std::string metadata_v2_utils::serialize_as_json(bool simple) const {
  return data_.serialize_as_json(simple);
}

metadata_v2::metadata_v2(
    logger& lgr, std::span<uint8_t const> schema, std::span<uint8_t const> data,
    metadata_options const& options, int inode_offset,
    bool force_consistency_check,
    std::shared_ptr<performance_monitor const> const& perfmon)
    : impl_(make_unique_logging_object<metadata_v2::impl, metadata_,
                                       logger_policies>(
          lgr, schema, data, options, inode_offset, force_consistency_check,
          perfmon)) {}

} // namespace dwarfs::reader::internal

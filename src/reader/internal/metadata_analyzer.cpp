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
#include <concepts>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>

#include <dwarfs/internal/string_table.h>
#include <dwarfs/reader/internal/metadata_analyzer.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;
using namespace ::apache::thrift;
using namespace ::apache::thrift::frozen;

namespace {

Layout<thrift::metadata::metadata> const&
get_layout(MappedFrozen<thrift::metadata::metadata> const& meta) {
  auto layout = meta.findFirstOfType<
      std::unique_ptr<Layout<thrift::metadata::metadata>>>();
  DWARFS_CHECK(layout, "no layout found");
  return **layout;
}

void analyze_frozen(std::ostream& os,
                    MappedFrozen<thrift::metadata::metadata> const& meta,
                    size_t total_size, bool verbose) {
  null_logger lgr;

  struct usage_info {
    usage_info(size_t off, size_t sz, std::string text)
        : offset{off}
        , size{sz}
        , line{std::move(text)} {}

    size_t offset;
    size_t size;
    std::string line;
  };

  auto& l = get_layout(meta);
  std::vector<usage_info> usage;

#if FMT_VERSION >= 70000
#define DWARFS_FMT_L "L"
#else
#define DWARFS_FMT_L "n"
#endif

  auto get_offset = [&](auto const* ptr) {
    return ptr ? reinterpret_cast<uintptr_t>(ptr) -
                     reinterpret_cast<uintptr_t>(meta.getPosition().start)
               : 0;
  };

  auto get_list_offset = [&](auto const& v) {
    struct view_internal {
      void const* layout;
      uint8_t const* start;
      size_t bitOffset;
      uint8_t const* data;
      size_t count;
    };
    using list_t = std::decay_t<decltype(v.thaw())>;
    static_assert(sizeof(v) == sizeof(view_internal));
    static_assert(
        std::derived_from<std::decay_t<decltype(v)>,
                          typename frozen::detail::ArrayLayout<
                              list_t, typename list_t::value_type>::View>);
    auto const* vi = reinterpret_cast<view_internal const*>(&v);
    DWARFS_CHECK(vi->count == v.size(), "internal error: size mismatch");
    return get_offset(vi->data);
  };

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
                        std::optional<size_t> offset, std::string num) {
    std::string range;
    if (verbose) {
      if (offset) {
        range = fmt::format("  {:08x}..{:08x} ", *offset, *offset + size);
      } else {
        range.append(21, ' ');
      }
    }
    range.append(15, ' ');
    return fmt::format("{0}{1:<24}{2:>16" DWARFS_FMT_L "} bytes {3:>6} "
                       "{4:5.1f} bytes/item\n",
                       range, name, size, num,
                       count > 0 ? static_cast<double>(size) / count : 0.0);
  };

  auto fmt_detail_pct = [&](std::string_view name, size_t count, size_t size,
                            std::optional<size_t> offset = std::nullopt) {
    return fmt_detail(name, count, size, offset,
                      fmt::format("{0:5.1f}%", 100.0 * size / total_size));
  };

  auto add_size = [&](std::string_view name, size_t count, size_t offset,
                      size_t size) {
    usage.emplace_back(offset, size, fmt_size(name, count, size));
  };

  auto add_size_unique = [&](std::string_view name, size_t offset,
                             size_t size) {
    usage.emplace_back(offset, size, fmt_size(name, std::nullopt, size));
  };

  auto list_size = [&](auto const& list, auto const& field) {
    return (list.size() * field.layout.itemField.layout.bits + 7) / 8;
  };

  auto add_list_size = [&](std::string_view name, auto const& list,
                           auto const& field) {
    add_size(name, list.size(), get_list_offset(list), list_size(list, field));
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
      usage.emplace_back(get_list_offset(list), size, fmt);
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
                 fmt_detail_pct("|- data", count, data_size,
                                get_offset(table.buffer().data()));
      if (table.symtab()) {
        string_table st(lgr, "tmp", table);
        auto unpacked_size = st.unpacked_size();
        fmt += fmt_detail(
            "|- unpacked", count, unpacked_size, std::nullopt,
            fmt::format("{0:5.2f}x",
                        static_cast<double>(unpacked_size) / data_size));
        fmt += fmt_detail_pct("|- dict", count, dict_size,
                              get_offset(table.symtab()->data()));
      }
      fmt += fmt_detail_pct("'- index", count, index_size,
                            get_list_offset(table.index()));
      usage.emplace_back(get_offset(table.buffer().data()), size, fmt);
    }
  };

  using detail_bits_t = std::pair<std::string_view, size_t>;

  auto summarize_details = [&](std::string_view name, size_t count,
                               size_t offset, size_t size,
                               std::span<detail_bits_t> details) {
    std::ranges::stable_sort(details, std::ranges::greater{},
                             &detail_bits_t::second);
    auto fmt = fmt_size(name, count, size);
    for (size_t i = 0; i < details.size(); ++i) {
      auto [member, bits] = details[i];
      auto tree = i == details.size() - 1 ? "'" : "|";
      fmt += fmt_detail_pct(fmt::format("{}- {} [{}]", tree, member, bits),
                            count, (count * bits + 7) / 8);
    }
    usage.emplace_back(offset, size, fmt);
  };

#define META_LIST_SIZE(x) add_list_size(#x, meta.x(), l.x##Field)

#define META_STRING_LIST_SIZE(x) add_string_list_size(#x, meta.x(), l.x##Field)

#define META_OPT_LIST_SIZE(x)                                                  \
  do {                                                                         \
    if (auto list = meta.x()) {                                                \
      add_list_size(#x, *list, l.x##Field.layout.valueField);                  \
    }                                                                          \
  } while (0)

#define META_OPT_MAP_SIZE(x) META_OPT_LIST_SIZE(x)

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
  summarize_details(#x, meta.x().size(), get_list_offset(meta.x()),            \
                    list_size(meta.x(), l.x##Field), detail_bits);             \
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
  summarize_details(#x, list->size(), get_list_offset(*list),                  \
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
  META_OPT_STRING_LIST_SIZE(category_metadata_json);
  META_OPT_MAP_SIZE(block_category_metadata);

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

  if (auto list = meta.metadata_version_history()) {
    size_t history_size =
        list_size(*list, l.metadata_version_historyField.layout.valueField);
    for (auto const& entry : *list) {
      if (entry.dwarfs_version()) {
        history_size += entry.dwarfs_version()->size();
      }
    }
    add_size("metadata_version_history", list->size(), get_list_offset(*list),
             history_size);
  }

  if (auto version = meta.dwarfs_version()) {
    add_size_unique("dwarfs_version", get_offset(version->data()),
                    version->size());
  }

  add_size_unique("metadata_root", 0, l.size);
  add_size_unique("padding", total_size - LayoutRoot::kPaddingBytes,
                  LayoutRoot::kPaddingBytes);

  std::ranges::sort(usage, [verbose](auto const& a, auto const& b) {
    if (verbose) {
      return a.offset < b.offset ||
             (a.offset == b.offset &&
              (a.size < b.size || (a.size == b.size && a.line < b.line)));
    }
    return a.size > b.size || (a.size == b.size && a.line < b.line);
  });

  os << "metadata memory usage:\n";
  if (verbose) {
    os << fmt::format("  {:08x}..{:08x} ", 0, total_size);
  }
  os << fmt::format("               {0:.<24}{1:.>16" DWARFS_FMT_L
                    "} bytes       {2:6.1f} bytes/inode\n",
                    "total metadata", total_size,
                    static_cast<double>(total_size) / meta.inodes().size());

#undef DWARFS_FMT_L

  for (auto const& u : usage) {
    if (verbose) {
      os << fmt::format("  {:08x}..{:08x} ", u.offset, u.offset + u.size);
    }
    os << u.line;
  }
}

} // namespace

metadata_analyzer::metadata_analyzer(
    MappedFrozen<thrift::metadata::metadata> const& meta,
    std::span<uint8_t const> data)
    : meta_{meta}
    , data_{data} {}

void metadata_analyzer::print_layout(std::ostream& os) const {
  get_layout(meta_).print(os, 0);
  os << "\n";
}

void metadata_analyzer::print_frozen(std::ostream& os, bool verbose) const {
  analyze_frozen(os, meta_, data_.size(), verbose);
}

} // namespace dwarfs::reader::internal

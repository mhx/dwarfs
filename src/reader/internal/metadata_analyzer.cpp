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

#if FMT_VERSION >= 70000
#define DWARFS_FMT_L "L"
#else
#define DWARFS_FMT_L "n"
#endif

namespace {

template <typename T>
concept list_view_type = requires(T a) {
  requires std::derived_from<
      T, typename frozen::detail::ArrayLayout<
             std::decay_t<decltype(a.thaw())>,
             typename std::decay_t<decltype(a.thaw())>::value_type>::View>;
};

Layout<thrift::metadata::metadata> const&
get_layout(MappedFrozen<thrift::metadata::metadata> const& meta) {
  auto layout = meta.findFirstOfType<
      std::unique_ptr<Layout<thrift::metadata::metadata>>>();
  DWARFS_CHECK(layout, "no layout found");
  return **layout;
}

class frozen_analyzer {
 public:
  frozen_analyzer(MappedFrozen<thrift::metadata::metadata> const& meta,
                  std::span<uint8_t const> data, bool verbose)
      : meta_{meta}
      , total_size_{data.size()}
      , verbose_{verbose} {}

  void print(std::ostream& os) const;

 private:
  struct usage_info {
    usage_info(size_t off, size_t sz, std::string text)
        : offset{off}
        , size{sz}
        , line{std::move(text)} {}

    size_t offset;
    size_t size;
    std::string line;
  };

  using detail_bits_t = std::pair<std::string_view, size_t>;

  static size_t list_size(list_view_type auto const& list, auto const& field) {
    return (list.size() * field.layout.itemField.layout.bits + 7) / 8;
  };

  size_t get_offset(auto const* ptr) const;
  size_t get_list_offset(list_view_type auto const& v) const;

  std::string fmt_size(std::string_view name, std::optional<size_t> count_opt,
                       size_t size) const;
  std::string fmt_detail(std::string_view name, size_t count, size_t size,
                         std::optional<size_t> offset, std::string num) const;
  std::string fmt_detail_pct(std::string_view name, size_t count, size_t size,
                             std::optional<size_t> offset = std::nullopt) const;

  void add_size(std::vector<usage_info>& usage, std::string_view name,
                size_t count, size_t offset, size_t size) const;
  void add_size_unique(std::vector<usage_info>& usage, std::string_view name,
                       size_t offset, size_t size) const;
  void add_list_size(std::vector<usage_info>& usage, std::string_view name,
                     auto const& list, auto const& field) const;
  void
  add_string_list_size(std::vector<usage_info>& usage, std::string_view name,
                       auto const& list, auto const& field) const;
  void
  add_string_table_size(std::vector<usage_info>& usage, std::string_view name,
                        auto const& table, auto const& field) const;

  void summarize_details(std::vector<usage_info>& usage, std::string_view name,
                         size_t count, size_t offset, size_t size,
                         std::span<detail_bits_t> details) const;

  MappedFrozen<thrift::metadata::metadata> const& meta_;
  size_t total_size_{0};
  bool verbose_{false};
};

size_t frozen_analyzer::get_offset(auto const* ptr) const {
  return ptr ? reinterpret_cast<uintptr_t>(ptr) -
                   reinterpret_cast<uintptr_t>(meta_.getPosition().start)
             : 0;
}

size_t frozen_analyzer::get_list_offset(list_view_type auto const& v) const {
  struct view_internal {
    void const* layout;
    uint8_t const* start;
    size_t bitOffset;
    uint8_t const* data;
    size_t count;
  };
  static_assert(sizeof(v) == sizeof(view_internal));
  auto const* vi = reinterpret_cast<view_internal const*>(&v);
  DWARFS_CHECK(vi->count == v.size(), "internal error: size mismatch");
  return get_offset(vi->data);
}

std::string
frozen_analyzer::fmt_size(std::string_view name,
                          std::optional<size_t> count_opt, size_t size) const {
  auto count = count_opt.value_or(1);
  std::string count_str;
  if (count_opt.has_value()) {
    count_str = fmt::format("{0:" DWARFS_FMT_L "}", count);
  }
  return fmt::format("{0:>14} {1:.<24}{2:.>16" DWARFS_FMT_L
                     "} bytes {3:5.1f}% {4:5.1f} bytes/item\n",
                     count_str, name, size, 100.0 * size / total_size_,
                     count > 0 ? static_cast<double>(size) / count : 0.0);
}

std::string
frozen_analyzer::fmt_detail(std::string_view name, size_t count, size_t size,
                            std::optional<size_t> offset,
                            std::string num) const {
  std::string range;
  if (verbose_) {
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
}

std::string
frozen_analyzer::fmt_detail_pct(std::string_view name, size_t count,
                                size_t size,
                                std::optional<size_t> offset) const {
  return fmt_detail(name, count, size, offset,
                    fmt::format("{0:5.1f}%", 100.0 * size / total_size_));
};

void frozen_analyzer::add_size(std::vector<usage_info>& usage,
                               std::string_view name, size_t count,
                               size_t offset, size_t size) const {
  usage.emplace_back(offset, size, fmt_size(name, count, size));
}

void frozen_analyzer::add_size_unique(std::vector<usage_info>& usage,
                                      std::string_view name, size_t offset,
                                      size_t size) const {
  usage.emplace_back(offset, size, fmt_size(name, std::nullopt, size));
}

void frozen_analyzer::add_list_size(std::vector<usage_info>& usage,
                                    std::string_view name, auto const& list,
                                    auto const& field) const {
  add_size(usage, name, list.size(), get_list_offset(list),
           list_size(list, field));
}

void frozen_analyzer::add_string_list_size(std::vector<usage_info>& usage,
                                           std::string_view name,
                                           auto const& list,
                                           auto const& field) const {
  if (auto count = list.size(); count > 0) {
    auto index_size = list_size(list, field);
    auto data_size = list.back().end() - list.front().begin();
    auto size = index_size + data_size;
    auto fmt =
        fmt_size(name, count, size) +
        fmt_detail_pct("|- index", count, index_size, get_list_offset(list)) +
        fmt_detail_pct("'- data", count, data_size,
                       get_offset(list.front().data()));
    usage.emplace_back(get_list_offset(list), size, fmt);
  }
}

void frozen_analyzer::add_string_table_size(std::vector<usage_info>& usage,
                                            std::string_view name,
                                            auto const& table,
                                            auto const& field) const {
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
      null_logger lgr;
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
}

void frozen_analyzer::summarize_details(
    std::vector<usage_info>& usage, std::string_view name, size_t count,
    size_t offset, size_t size, std::span<detail_bits_t> details) const {
  std::ranges::stable_sort(details, std::ranges::greater{},
                           &detail_bits_t::second);
  auto fmt = fmt_size(name, count, size);
  for (size_t i = 0; i < details.size(); ++i) {
    auto [member, bits] = details[i];
    auto tree = i == details.size() - 1 ? "'" : "|";
    fmt += fmt_detail_pct(fmt::format("{}- {} [{}]", tree, member, bits), count,
                          (count * bits + 7) / 8);
  }
  usage.emplace_back(offset, size, fmt);
}

#define META_LIST_SIZE(x) add_list_size(usage, #x, meta_.x(), l.x##Field)

#define META_STRING_LIST_SIZE(x)                                               \
  add_string_list_size(usage, #x, meta_.x(), l.x##Field)

#define META_OPT_LIST_SIZE(x)                                                  \
  do {                                                                         \
    if (auto list = meta_.x()) {                                               \
      add_list_size(usage, #x, *list, l.x##Field.layout.valueField);           \
    }                                                                          \
  } while (0)

#define META_OPT_MAP_SIZE(x) META_OPT_LIST_SIZE(x)

#define META_OPT_STRING_LIST_SIZE(x)                                           \
  do {                                                                         \
    if (auto list = meta_.x()) {                                               \
      add_string_list_size(usage, #x, *list, l.x##Field.layout.valueField);    \
    }                                                                          \
  } while (0)

#define META_OPT_STRING_SET_SIZE(x) META_OPT_STRING_LIST_SIZE(x)

#define META_OPT_STRING_TABLE_SIZE(x)                                          \
  do {                                                                         \
    if (auto table = meta_.x()) {                                              \
      add_string_table_size(usage, #x, *table, l.x##Field.layout.valueField);  \
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
  summarize_details(usage, #x, meta_.x().size(), get_list_offset(meta_.x()),   \
                    list_size(meta_.x(), l.x##Field), detail_bits);            \
  }                                                                            \
  while (0)

#define META_OPT_LIST_SIZE_DETAIL_BEGIN(x)                                     \
  do {                                                                         \
    if (auto list = meta_.x()) {                                               \
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
  summarize_details(usage, #x, list->size(), get_list_offset(*list),           \
                    list_size(*list, l.x##Field.layout.valueField),            \
                    detail_bits);                                              \
  }                                                                            \
  }                                                                            \
  while (0)

void frozen_analyzer::print(std::ostream& os) const {
  auto& l = get_layout(meta_);
  std::vector<usage_info> usage;

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
  META_ADD_DETAIL_BITS(inodes, btime_offset);
  META_ADD_DETAIL_BITS(inodes, atime_subsec);
  META_ADD_DETAIL_BITS(inodes, mtime_subsec);
  META_ADD_DETAIL_BITS(inodes, ctime_subsec);
  META_ADD_DETAIL_BITS(inodes, btime_subsec);
  META_ADD_DETAIL_BITS(inodes, name_index_v2_2);
  META_ADD_DETAIL_BITS(inodes, inode_v2_2);
  META_LIST_SIZE_DETAIL_END(inodes);

  META_OPT_LIST_SIZE_DETAIL_BEGIN(dir_entries);
  META_OPT_ADD_DETAIL_BITS(dir_entries, name_index);
  META_OPT_ADD_DETAIL_BITS(dir_entries, inode_num);
  META_OPT_LIST_SIZE_DETAIL_END(dir_entries);

  META_LIST_SIZE(chunk_table);
  if (!meta_.entry_table_v2_2().empty()) {
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

  if (auto cache = meta_.reg_file_size_cache()) {
    add_list_size(
        usage, "inode_size_cache", cache->size_lookup(),
        l.reg_file_size_cacheField.layout.valueField.layout.size_lookupField);
  }

  if (auto list = meta_.metadata_version_history()) {
    size_t history_size =
        list_size(*list, l.metadata_version_historyField.layout.valueField);
    for (auto const& entry : *list) {
      if (entry.dwarfs_version()) {
        history_size += entry.dwarfs_version()->size();
      }
    }
    add_size(usage, "metadata_version_history", list->size(),
             get_list_offset(*list), history_size);
  }

  if (auto version = meta_.dwarfs_version()) {
    add_size_unique(usage, "dwarfs_version", get_offset(version->data()),
                    version->size());
  }

  META_OPT_LIST_SIZE(large_hole_size);

  add_size_unique(usage, "metadata_root", 0, l.size);
  add_size_unique(usage, "padding", total_size_ - LayoutRoot::kPaddingBytes,
                  LayoutRoot::kPaddingBytes);

  std::ranges::sort(usage, [this](auto const& a, auto const& b) {
    if (verbose_) {
      return a.offset < b.offset ||
             (a.offset == b.offset &&
              (a.size < b.size || (a.size == b.size && a.line < b.line)));
    }
    return a.size > b.size || (a.size == b.size && a.line < b.line);
  });

  os << "metadata memory usage:\n";
  if (verbose_) {
    os << fmt::format("  {:08x}..{:08x} ", 0, total_size_);
  }
  os << fmt::format("               {0:.<24}{1:.>16" DWARFS_FMT_L
                    "} bytes       {2:6.1f} bytes/inode\n",
                    "total metadata", total_size_,
                    static_cast<double>(total_size_) / meta_.inodes().size());

  for (auto const& u : usage) {
    if (verbose_) {
      os << fmt::format("  {:08x}..{:08x} ", u.offset, u.offset + u.size);
    }
    os << u.line;
  }
}

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
  frozen_analyzer(meta_, data_, verbose).print(os);
}

} // namespace dwarfs::reader::internal

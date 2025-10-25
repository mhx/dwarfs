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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <io.h>
#endif

#include <boost/algorithm/string/join.hpp>
#include <boost/program_options.hpp>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/block_compressor.h>
#include <dwarfs/block_compressor_parser.h>
#include <dwarfs/checksum.h>
#include <dwarfs/compressor_registry.h>
#include <dwarfs/config.h>
#include <dwarfs/conv.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/integral_value_parser.h>
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/sorted_array_map.h>
#include <dwarfs/string.h>
#include <dwarfs/terminal.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/program_options_helpers.h>
#include <dwarfs/tool/sysinfo.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/rewrite_filesystem.h>
#include <dwarfs/utility/rewrite_options.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/category_parser.h>
#include <dwarfs/writer/console_writer.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/filesystem_block_category_resolver.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/filesystem_writer_options.h>
#include <dwarfs/writer/filter_debug.h>
#include <dwarfs/writer/fragment_order_parser.h>
#include <dwarfs/writer/rule_based_entry_filter.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

namespace po = boost::program_options;

namespace dwarfs::tool {

namespace {

using namespace std::string_view_literals;
using namespace dwarfs::binary_literals;

constexpr sorted_array_map progress_modes{
    std::pair{"none"sv, writer::console_writer::NONE},
    std::pair{"simple"sv, writer::console_writer::SIMPLE},
    std::pair{"ascii"sv, writer::console_writer::ASCII},
    std::pair{"unicode"sv, writer::console_writer::UNICODE},
};

constexpr auto default_progress_mode = "unicode";

constexpr sorted_array_map debug_filter_modes{
    std::pair{"included"sv, writer::debug_filter_mode::INCLUDED},
    std::pair{"included-files"sv, writer::debug_filter_mode::INCLUDED_FILES},
    std::pair{"excluded"sv, writer::debug_filter_mode::EXCLUDED},
    std::pair{"excluded-files"sv, writer::debug_filter_mode::EXCLUDED_FILES},
    std::pair{"files"sv, writer::debug_filter_mode::FILES},
    std::pair{"all"sv, writer::debug_filter_mode::ALL},
};

constexpr size_t min_block_size_bits{10};
constexpr size_t max_block_size_bits{30};

struct level_defaults {
  unsigned block_size_bits;
  std::string_view data_compression;
  std::string_view schema_history_compression;
  std::string_view metadata_compression;
  unsigned window_size;
  unsigned window_step;
  std::string_view order;
};

#if defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_1 "lz4"
#define ALG_DATA_2 "lz4hc:level=9"
#define ALG_DATA_3 "lz4hc:level=9"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_1 "zstd:level=1"
#define ALG_DATA_2 "zstd:level=4"
#define ALG_DATA_3 "zstd:level=7"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_1 "lzma:level=1"
#define ALG_DATA_2 "lzma:level=2"
#define ALG_DATA_3 "lzma:level=3"
#else
#define ALG_DATA_1 "null"
#define ALG_DATA_2 "null"
#define ALG_DATA_3 "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_4 "zstd:level=11"
#define ALG_DATA_5 "zstd:level=19"
#define ALG_DATA_6 "zstd:level=22"
#define ALG_DATA_7 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_4 "lzma:level=3"
#define ALG_DATA_5 "lzma:level=4"
#define ALG_DATA_6 "lzma:level=5"
#define ALG_DATA_7 "lzma:level=8"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_4 "lz4hc:level=9"
#define ALG_DATA_5 "lz4hc:level=9"
#define ALG_DATA_6 "lz4hc:level=9"
#define ALG_DATA_7 "lz4hc:level=9"
#else
#define ALG_DATA_4 "null"
#define ALG_DATA_5 "null"
#define ALG_DATA_6 "null"
#define ALG_DATA_7 "null"
#endif

#if defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_8 "lzma:level=9"
#define ALG_DATA_9 "lzma:level=9"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_8 "zstd:level=22"
#define ALG_DATA_9 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_8 "lz4hc:level=9"
#define ALG_DATA_9 "lz4hc:level=9"
#else
#define ALG_DATA_8 "null"
#define ALG_DATA_9 "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_SCHEMA "zstd:level=16"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_SCHEMA "lzma:level=4"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_SCHEMA "lz4hc:level=9"
#else
#define ALG_SCHEMA "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_METADATA_7 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_METADATA_7 "lzma:level=9"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_METADATA_7 "lz4hc:level=9"
#else
#define ALG_METADATA_7 "null"
#endif

#if defined(DWARFS_HAVE_LIBLZMA)
#define ALG_METADATA_9 "lzma:level=9"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_METADATA_9 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_METADATA_9 "lz4hc:level=9"
#else
#define ALG_METADATA_9 "null"
#endif

constexpr std::array<level_defaults, 10> levels{{
    // clang-format off
    /* 0 */ {20, "null",     "null"    , "null",          0, 0, "none"},
    /* 1 */ {20, ALG_DATA_1, ALG_SCHEMA, "null",          0, 0, "path"},
    /* 2 */ {20, ALG_DATA_2, ALG_SCHEMA, "null",          0, 0, "path"},
    /* 3 */ {21, ALG_DATA_3, ALG_SCHEMA, "null",         12, 1, "similarity"},
    /* 4 */ {22, ALG_DATA_4, ALG_SCHEMA, "null",         12, 2, "similarity"},
    /* 5 */ {23, ALG_DATA_5, ALG_SCHEMA, "null",         12, 2, "similarity"},
    /* 6 */ {24, ALG_DATA_6, ALG_SCHEMA, "null",         12, 3, "nilsimsa"},
    /* 7 */ {24, ALG_DATA_7, ALG_SCHEMA, ALG_METADATA_7, 12, 3, "nilsimsa"},
    /* 8 */ {24, ALG_DATA_8, ALG_SCHEMA, ALG_METADATA_9, 12, 4, "nilsimsa"},
    /* 9 */ {26, ALG_DATA_9, ALG_SCHEMA, ALG_METADATA_9, 12, 4, "nilsimsa"},
    // clang-format on
}};

using categorize_defaults_type =
    std::unordered_map<std::string, std::vector<std::string>>;

categorize_defaults_type const& categorize_defaults_common() {
  static categorize_defaults_type const defaults{
      // clang-format off
      {"--compression", {"incompressible::null"}},
      // clang-format on
  };

  return defaults;
}

categorize_defaults_type const& categorize_defaults_level(unsigned level) {
  static categorize_defaults_type const defaults_off;

  static categorize_defaults_type const defaults_fast{
      // clang-format off
      {"--order",       {"pcmaudio/waveform::revpath", "fits/image::revpath"}},
      {"--window-size", {"pcmaudio/waveform::0", "fits/image::0"}},
      {"--compression", {
#ifdef DWARFS_HAVE_FLAC
                         "pcmaudio/waveform::flac:level=3",
#else
                         "pcmaudio/waveform::zstd:level=3",
#endif
#ifdef DWARFS_HAVE_RICEPP
                         "fits/image::ricepp",
#else
                         "fits/image::zstd:level=3",
#endif
                        }},
      // clang-format on
  };

  static categorize_defaults_type const defaults_medium{
      // clang-format off
      {"--order",       {"pcmaudio/waveform::revpath", "fits/image::revpath"}},
      {"--window-size", {"pcmaudio/waveform::20", "fits/image::0"}},
      {"--compression", {
#ifdef DWARFS_HAVE_FLAC
                         "pcmaudio/waveform::flac:level=5",
#else
                         "pcmaudio/waveform::zstd:level=5",
#endif
#ifdef DWARFS_HAVE_RICEPP
                         "fits/image::ricepp",
#else
                         "fits/image::zstd:level=5",
#endif
                        }},
      // clang-format on
  };

  static categorize_defaults_type const defaults_slow{
      // clang-format off
      {"--order",       {"fits/image::revpath"}},
      {"--window-size", {"pcmaudio/waveform::16", "fits/image::0"}},
      {"--compression", {
#ifdef DWARFS_HAVE_FLAC
                         "pcmaudio/waveform::flac:level=8",
#else
                         "pcmaudio/waveform::zstd:level=8",
#endif
#ifdef DWARFS_HAVE_RICEPP
                         "fits/image::ricepp",
#else
                         "fits/image::zstd:level=8",
#endif
                        }},
      // clang-format on
  };

  static constexpr std::array<categorize_defaults_type const*, 10>
      defaults_level{{
          // clang-format off
          /* 0 */ &defaults_off,
          /* 1 */ &defaults_fast,
          /* 2 */ &defaults_fast,
          /* 3 */ &defaults_fast,
          /* 4 */ &defaults_fast,
          /* 5 */ &defaults_medium,
          /* 6 */ &defaults_medium,
          /* 7 */ &defaults_medium,
          /* 8 */ &defaults_slow,
          /* 9 */ &defaults_slow,
          // clang-format on
      }};

  return *defaults_level.at(level);
}

constexpr unsigned default_level = 7;

class categorize_optval {
 public:
  categorize_optval() = default;
  explicit categorize_optval(std::string const& val, bool expl = false)
      : value_{val}
      , is_explicit_{expl} {}

  bool empty() const { return value_.empty(); }
  std::string const& value() const { return value_; }

  bool is_explicit() const { return is_explicit_; }

  template <typename T>
  void add_implicit_defaults(T& cop) const {
    if (cop.has_category_resolver()) {
      if (auto it = defaults_.find(cop.name()); it != defaults_.end()) {
        for (auto const& v : it->second) {
          cop.parse_fallback(v);
        }
      }
    }
  }

  void add_defaults(categorize_defaults_type const& defaults) {
    for (auto const& [key, values] : defaults) {
      auto& vs = defaults_[key];
      vs.insert(vs.end(), values.begin(), values.end());
    }
  }

 private:
  categorize_defaults_type defaults_;
  std::string value_;
  bool is_explicit_{false};
};

std::ostream& operator<<(std::ostream& os, categorize_optval const& optval) {
  return os << optval.value() << (optval.is_explicit() ? " (explicit)" : "");
}

void validate(boost::any& v, std::vector<std::string> const& values,
              categorize_optval*, int) {
  po::validators::check_first_occurrence(v);
  v = categorize_optval{po::validators::get_single_string(values), true};
}

uint64_t
compute_memory_limit(uint64_t const block_size, uint64_t const num_cpu) {
  auto const sys_mem = std::max(tool::sysinfo::get_total_memory(), 256_MiB);
  auto wanted_mem = num_cpu * block_size;
  if (wanted_mem < sys_mem / 64) {
    wanted_mem = sys_mem / 64;
  } else {
    wanted_mem += std::min(num_cpu, UINT64_C(8)) * block_size;
  }
  return std::min(wanted_mem, sys_mem / 8);
}

} // namespace

int mkdwarfs_main(int argc, sys_char** argv, iolayer const& iol) {
  using namespace std::chrono_literals;

  size_t const num_cpu = std::max(hardware_concurrency(), 1U);
  static constexpr size_t const kDefaultMaxActiveBlocks{1};
  static constexpr size_t const kDefaultBloomFilterSize{4};

  writer::segmenter_factory::config sf_config;
  sys_string path_str, input_list_str, output_str, header_str;
  std::string memory_limit, schema_compression, metadata_compression, timestamp,
      time_resolution, progress_mode, recompress_opts, pack_metadata,
      file_hash_algo, debug_filter, max_similarity_size, chmod_str,
      history_compression, recompress_categories;
  std::vector<sys_string> filter;
  std::vector<std::string> order, max_lookback_blocks, window_size, window_step,
      bloom_filter_size, compression;
  size_t num_workers, num_scanner_workers, num_segmenter_workers;
  bool no_progress = false, remove_header = false, no_section_index = false,
       force_overwrite = false, no_history = false, no_sparse_files = false,
       no_history_timestamps = false, no_history_command_line = false,
       rebuild_metadata = false, change_block_size = false;
  unsigned level;
  int compress_niceness;
  uint16_t uid, gid;
  categorize_optval categorizer_list;

  integral_value_parser<size_t> max_lookback_parser;
  integral_value_parser<unsigned> window_size_parser(0, 24);
  integral_value_parser<unsigned> window_step_parser(0, 8);
  integral_value_parser<unsigned> bloom_filter_size_parser(0, 10);
  writer::fragment_order_parser order_parser(iol.file);
  block_compressor_parser compressor_parser;

  writer::scanner_options options;
  logger_options logopts;

  auto order_desc = "inode fragments order (" +
                    dwarfs::writer::fragment_order_parser::choices() + ")";

  auto progress_desc =
      fmt::format("progress mode ({})",
                  fmt::join(ranges::views::keys(progress_modes), ", "));

  auto debug_filter_desc =
      fmt::format("show effect of filter rules without producing an image ({})",
                  fmt::join(ranges::views::keys(debug_filter_modes), ", "));

  auto hash_list = checksum::available_algorithms();

  auto file_hash_desc = fmt::format(
      "choice of file hashing function (none, {})", fmt::join(hash_list, ", "));

  writer::categorizer_registry catreg;

  auto categorize_desc =
      fmt::format("enable categorizers in the given order ({})",
                  fmt::join(catreg.categorizer_names(), ", "));

  auto lvl_def_val = [](auto opt) {
    return fmt::format("arg (={})", levels[default_level].*opt);
  };

  auto dep_def_val = [](auto dep) { return fmt::format("arg (={})", dep); };

  auto cat_def_val = [](auto def) {
    return fmt::format("[cat::]arg (={})", def);
  };

  auto lvl_cat_def_val = [](auto opt) {
    return fmt::format("[cat::]arg (={})", levels[default_level].*opt);
  };

  // clang-format off
  po::options_description basic_opts("Options");
  basic_opts.add_options()
    ("input,i",
        po_sys_value<sys_string>(&path_str),
        "path to root directory or source filesystem")
    ("input-list",
        po_sys_value<sys_string>(&input_list_str),
        "file containing list of file paths relative to root directory "
        "or - for stdin")
    ("output,o",
        po_sys_value<sys_string>(&output_str),
        "filesystem output name or - for stdout")
    ("force,f",
        po::value<bool>(&force_overwrite)->zero_tokens(),
        "force overwrite of existing output image")
    ("compress-level,l",
        po::value<unsigned>(&level)->default_value(default_level),
        "compression level (0=fast, 9=best, see -H and man page for details)")
    ;
  tool::add_common_options(basic_opts, logopts);
  basic_opts.add_options()
    ("long-help,H",
        "output full help message and exit")
    ;

  po::options_description advanced_opts("Advanced options");
  advanced_opts.add_options()
    ("block-size-bits,S",
        po::value<unsigned>(&sf_config.block_size_bits)
          ->value_name(lvl_def_val(&level_defaults::block_size_bits)),
        "block size bits (size = 2^arg bits)")
    ("num-workers,N",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of writer (compression) worker threads")
    ("compress-niceness",
        po::value<int>(&compress_niceness)->default_value(5),
        "compression worker threads niceness")
    ("num-scanner-workers",
        po::value<size_t>(&num_scanner_workers)
          ->value_name(dep_def_val("num-workers")),
        "number of scanner (hasher/categorizer) worker threads")
    ("num-segmenter-workers",
        po::value<size_t>(&num_segmenter_workers)
          ->value_name(dep_def_val("num-workers")),
        "number of segmenter worker threads")
    ("memory-limit,L",
        po::value<std::string>(&memory_limit)->default_value("auto"),
        "block manager memory limit")
    ("recompress",
        po::value<std::string>(&recompress_opts)->implicit_value("all"),
        "recompress an existing filesystem (none, block, metadata, all)")
    ("rebuild-metadata",
        po::value<bool>(&rebuild_metadata)->zero_tokens(),
        "fully rebuild metadata")
    ("change-block-size",
        po::value<bool>(&change_block_size)->zero_tokens(),
        "change block size when recompressing")
    ("no-metadata-version-history",
        po::value<bool>(&options.metadata.no_metadata_version_history)->zero_tokens(),
        "remove metadata version history")
    ("recompress-categories",
        po::value<std::string>(&recompress_categories),
        "only recompress blocks of these categories")
    ("categorize",
        po::value<categorize_optval>(&categorizer_list)
          ->implicit_value(categorize_optval("fits,pcmaudio,incompressible")),
        categorize_desc.c_str())
    ("order",
        po::value<std::vector<std::string>>(&order)
          ->value_name(lvl_cat_def_val(&level_defaults::order))
          ->multitoken()->composing(),
        order_desc.c_str())
    ("max-similarity-size",
        po::value<std::string>(&max_similarity_size),
        "maximum file size to compute similarity")
    ("file-hash",
        po::value<std::string>(&file_hash_algo)->default_value("xxh3-128"),
        file_hash_desc.c_str())
    ("progress",
        po::value<std::string>(&progress_mode)->default_value(default_progress_mode),
        progress_desc.c_str())
    ("no-progress",
        po::value<bool>(&no_progress)->zero_tokens(),
        "don't show progress")
    ;

  po::options_description filesystem_opts("File system options");
  filesystem_opts.add_options()
    ("with-devices",
        po::value<bool>(&options.with_devices)->zero_tokens(),
        "include block and character devices")
    ("with-specials",
        po::value<bool>(&options.with_specials)->zero_tokens(),
        "include named fifo and sockets")
    ("no-sparse-files",
        po::value<bool>(&no_sparse_files)->zero_tokens(),
        "don't store sparse files as sparse")
    ("header",
        po_sys_value<sys_string>(&header_str),
        "prepend output filesystem with contents of this file")
    ("remove-header",
        po::value<bool>(&remove_header)->zero_tokens(),
        "remove any header present before filesystem data"
        " (use with --recompress)")
    ("no-section-index",
        po::value<bool>(&no_section_index)->zero_tokens(),
        "don't add section index to file system")
    ("no-history",
        po::value<bool>(&no_history)->zero_tokens(),
        "don't add history to file system")
    ("no-history-timestamps",
        po::value<bool>(&no_history_timestamps)->zero_tokens(),
        "don't add timestamps to file system history")
    ("no-history-command-line",
        po::value<bool>(&no_history_command_line)->zero_tokens(),
        "don't add command line to file system history")
    ;

  po::options_description segmenter_opts("Segmenter options");
  segmenter_opts.add_options()
    ("max-lookback-blocks,B",
        po::value<std::vector<std::string>>(&max_lookback_blocks)
          ->value_name(cat_def_val(kDefaultMaxActiveBlocks))
          ->multitoken()->composing(),
        "how many blocks to scan for segments")
    ("window-size,W",
        po::value<std::vector<std::string>>(&window_size)
          ->value_name(lvl_cat_def_val(&level_defaults::window_size))
          ->multitoken()->composing(),
        "window sizes for block hashing")
    ("window-step,w",
        po::value<std::vector<std::string>>(&window_step)
          ->value_name(lvl_cat_def_val(&level_defaults::window_step))
          ->multitoken()->composing(),
        "window step (as right shift of size)")
    ("bloom-filter-size",
        po::value<std::vector<std::string>>(&bloom_filter_size)
          ->value_name(cat_def_val(kDefaultBloomFilterSize))
          ->multitoken()->composing(),
        "bloom filter size (2^N*values bits)")
    ;

  po::options_description compressor_opts("Compressor options");
  compressor_opts.add_options()
    ("compression,C",
        po::value<std::vector<std::string>>(&compression)
          ->value_name(lvl_cat_def_val(&level_defaults::data_compression))
          ->multitoken()->composing(),
        "block compression algorithm")
    ("schema-compression",
        po::value<std::string>(&schema_compression)
          ->value_name(lvl_def_val(&level_defaults::schema_history_compression)),
        "metadata schema compression algorithm")
    ("metadata-compression",
        po::value<std::string>(&metadata_compression)
          ->value_name(lvl_def_val(&level_defaults::metadata_compression)),
        "metadata compression algorithm")
    ("history-compression",
        po::value<std::string>(&history_compression)
          ->value_name(lvl_def_val(&level_defaults::schema_history_compression)),
        "history compression algorithm")
    ;

  po::options_description filter_opts("Filter options");
  filter_opts.add_options()
    ("filter,F",
        po_sys_value<std::vector<sys_string>>(&filter)
          ->multitoken()->composing(),
        "add filter rule")
    ("debug-filter",
        po::value<std::string>(&debug_filter)->implicit_value("all"),
        debug_filter_desc.c_str())
    ("remove-empty-dirs",
        po::value<bool>(&options.remove_empty_dirs)->zero_tokens(),
        "remove empty directories in file system")
    ;

  po::options_description metadata_opts("Metadata options");
  metadata_opts.add_options()
    ("set-owner",
        po::value<uint16_t>(&uid),
        "set owner (uid) for whole file system")
    ("set-group",
        po::value<uint16_t>(&gid),
        "set group (gid) for whole file system")
    ("chmod",
        po::value<std::string>(&chmod_str),
        "recursively apply permission changes")
    ("no-create-timestamp",
        po::value<bool>(&options.metadata.no_create_timestamp)->zero_tokens(),
        "don't add create timestamp to file system")
    ("set-time",
        po::value<std::string>(&timestamp),
        "set timestamp for whole file system (unixtime or 'now')")
    ("keep-all-times",
        po::value<bool>(&options.metadata.keep_all_times)->zero_tokens(),
        "save atime and ctime in addition to mtime")
    ("time-resolution",
        po::value<std::string>(&time_resolution),
        "resolution of inode timestamps (default: 1s)")
    ("no-category-names",
        po::value<bool>(&options.metadata.no_category_names)->zero_tokens(),
        "don't add category names to file system")
    ("no-category-metadata",
        po::value<bool>(&options.metadata.no_category_metadata)->zero_tokens(),
        "don't add category metadata to file system")
    ("no-hardlink-table",
        po::value<bool>(&options.metadata.no_hardlink_table)->zero_tokens(),
        "don't add hardlink count table to file system")
    ("pack-metadata,P",
        po::value<std::string>(&pack_metadata)->default_value("auto"),
        "pack certain metadata elements (auto, all, none, chunk_table, "
        "directories, shared_files, names, names_index, symlinks, "
        "symlinks_index, force, plain)")
    ;
  // clang-format on

  po::options_description opts;
  opts.add(basic_opts)
      .add(advanced_opts)
      .add(filter_opts)
      .add(segmenter_opts)
      .add(compressor_opts)
      .add(filesystem_opts)
      .add(metadata_opts);

  catreg.add_options(opts);

  po::variables_map vm;

  std::vector<std::string> command_line;
  command_line.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    command_line.emplace_back(sys_string_to_string(argv[i]));
  }

  try {
    auto parsed = po::parse_command_line(argc, argv, opts);

    po::store(parsed, vm);
    po::notify(vm);

    auto unrecognized =
        po::collect_unrecognized(parsed.options, po::include_positional);

    if (!unrecognized.empty()) {
      iol.err << "error: unrecognized argument(s) '"
              << sys_string_to_string(boost::join(unrecognized, " ")) << "'\n";
      return 1;
    }
  } catch (po::error const& e) {
    iol.err << "error: " << e.what() << "\n";
    return 1;
  }

#ifdef DWARFS_BUILTIN_MANPAGE
  if (vm.contains("man")) {
    tool::show_manpage(tool::manpage::get_mkdwarfs_manpage(), iol);
    return 0;
  }
#endif

  auto constexpr usage = "Usage: mkdwarfs [OPTIONS...]\n";
  auto extra_deps = [](library_dependencies& deps) {
    compressor_registry::instance().add_library_dependencies(deps);
    decompressor_registry::instance().add_library_dependencies(deps);
  };

  if (vm.contains("long-help")) {
    constexpr std::string_view block_data_hdr{"Block Data"};
    constexpr std::string_view schema_history_hdr{"Schema/History"};
    constexpr std::string_view metadata_hdr{"Metadata"};
    size_t l_dc{block_data_hdr.size()}, l_sc{schema_history_hdr.size()},
        l_mc{metadata_hdr.size()}, l_or{0};
    for (auto const& l : levels) {
      l_dc = std::max(l_dc, l.data_compression.size());
      l_sc = std::max(l_sc, l.schema_history_compression.size());
      l_mc = std::max(l_mc, l.metadata_compression.size());
      l_or = std::max(l_or, l.order.size());
    }

    std::string sep(30 + l_dc + l_sc + l_mc + l_or, '-');

    iol.out << tool::tool_header("mkdwarfs", extra_deps) << usage << opts
            << "\n"
            << "Compression level defaults:\n"
            << "  " << sep << "\n"
            << fmt::format("  Level  Block  {:{}s} {:s}     Inode\n",
                           "Compression Algorithm", 4 + l_dc + l_sc + l_mc,
                           "Window")
            << fmt::format("         Size   {:{}s}  {:{}s}  {:{}s} {:6s}\n",
                           block_data_hdr, l_dc, schema_history_hdr, l_sc,
                           metadata_hdr, l_mc, "Size/Step  Order")
            << "  " << sep << "\n";

    for (auto const& [i, l] : ranges::views::enumerate(levels)) {
      iol.out << fmt::format("  {:1d}      {:2d}     {:{}s}  {:{}s}  {:{}s}"
                             "  {:2d} / {:1d}    {:{}s}",
                             i, l.block_size_bits, l.data_compression, l_dc,
                             l.schema_history_compression, l_sc,
                             l.metadata_compression, l_mc, l.window_size,
                             l.window_step, l.order, l_or)
              << "\n";
    }

    iol.out << "  " << sep << "\n";

    iol.out << "\nCompression algorithms:\n";

    compressor_registry::instance().for_each_algorithm(
        [&iol](compression_type, compressor_info const& info) {
          iol.out << fmt::format("  {:9}{}\n", info.name(), info.description());
          for (auto const& opt : info.options()) {
            iol.out << fmt::format("               {}\n", opt);
          }
        });

    iol.out << "\nCategories:\n";

    for (auto const& name : catreg.categorizer_names()) {
      stream_logger lgr(iol.term, iol.err);
      auto categorizer = catreg.create(lgr, name, vm, iol.file);
      iol.out << "  [" << name << "]\n";
      for (auto cat : categorizer->categories()) {
        iol.out << "    " << cat << "\n";
      }
    }

    iol.out << "\n";

    return 0;
  }

  if (vm.contains("help") or
      !(vm.contains("input") or vm.contains("input-list")) or
      (!vm.contains("output") and !vm.contains("debug-filter"))) {
    iol.out << tool::tool_header("mkdwarfs", extra_deps) << usage << "\n"
            << basic_opts << "\n";
    return 0;
  }

  if (level >= levels.size()) {
    iol.err << "error: invalid compression level\n";
    return 1;
  }

  auto const& defaults = levels[level];

  categorizer_list.add_defaults(categorize_defaults_common());
  categorizer_list.add_defaults(categorize_defaults_level(level));

  if (!vm.contains("block-size-bits")) {
    sf_config.block_size_bits = defaults.block_size_bits;
  }

  if (!vm.contains("schema-compression")) {
    schema_compression = defaults.schema_history_compression;
  }

  if (!vm.contains("history-compression")) {
    history_compression = defaults.schema_history_compression;
  }

  if (!vm.contains("metadata-compression")) {
    metadata_compression = defaults.metadata_compression;
  }

  if (sf_config.block_size_bits < min_block_size_bits ||
      sf_config.block_size_bits > max_block_size_bits) {
    iol.err << "error: block size must be between " << min_block_size_bits
            << " and " << max_block_size_bits << "\n";
    return 1;
  }

  std::filesystem::path path(path_str);
  std::optional<std::vector<std::filesystem::path>> input_list;

  if (vm.contains("input-list")) {
    if (vm.contains("filter")) {
      iol.err << "error: cannot combine --input-list and --filter\n";
      return 1;
    }

    // implicitly turn on
    options.with_devices = true;
    options.with_specials = true;

    if (!vm.contains("input")) {
      path = iol.os->current_path();
    }

    std::filesystem::path input_list_path(input_list_str);
    std::unique_ptr<input_stream> ifs;
    std::istream* is;

    if (input_list_path == "-") {
      is = &iol.in;
    } else {
      std::error_code ec;
      ifs = iol.file->open_input(input_list_path, ec);

      if (ec) {
        iol.err << "cannot open input list file '" << input_list_path
                << "': " << ec.message() << "\n";
        return 1;
      }

      is = &ifs->is();
    }

    std::string line;
    input_list.emplace();

    while (std::getline(*is, line)) {
      std::filesystem::path p(line);
      if (p.has_root_directory()) {
        p = iol.os->canonical(p);
      }
      input_list->emplace_back(std::move(p));
    }
  }

  path = iol.os->canonical(path);

  bool recompress =
      vm.contains("recompress") || rebuild_metadata || change_block_size;
  utility::rewrite_options rw_opts;
  if (recompress) {
    std::unordered_map<std::string, unsigned> const modes{
        {"all", 3},
        {"metadata", 2},
        {"block", 1},
        {"none", 0},
    };

    if (recompress_opts.empty()) {
      if (change_block_size) {
        recompress_opts = "all";
      } else if (rebuild_metadata) {
        recompress_opts = "metadata";
      }
    }

    if (auto it = modes.find(recompress_opts); it != modes.end()) {
      rw_opts.recompress_block = it->second & 1;
      rw_opts.recompress_metadata = it->second & 2;
    } else {
      iol.err << "invalid recompress mode: " << recompress_opts << "\n";
      return 1;
    }

    if (!recompress_categories.empty()) {
      if (change_block_size) {
        iol.err
            << "cannot use --recompress-categories with --change-block-size\n";
        return 1;
      }

      std::string_view input = recompress_categories;
      if (input.front() == '!') {
        rw_opts.recompress_categories_exclude = true;
        input.remove_prefix(1);
      }
      rw_opts.recompress_categories =
          split_to<std::unordered_set<std::string>>(input, ',');
    }
  }

  if (file_hash_algo == "none") {
    options.file_hash_algorithm.reset();
  } else if (checksum::is_available(file_hash_algo)) {
    options.file_hash_algorithm = file_hash_algo;
  } else {
    iol.err << "error: unknown file hash function '" << file_hash_algo << "'\n";
    return 1;
  }

  if (vm.contains("max-similarity-size")) {
    auto size = parse_size_with_unit(max_similarity_size);
    if (size > 0) {
      options.inode.max_similarity_scan_size = size;
    }
  }

  if (!vm.contains("num-scanner-workers")) {
    num_scanner_workers = num_workers;
  }

  if (!vm.contains("num-segmenter-workers")) {
    num_segmenter_workers = num_workers;
  }

  options.num_segmenter_workers = num_segmenter_workers;

  if (vm.contains("debug-filter")) {
    if (auto it = debug_filter_modes.find(debug_filter);
        it != debug_filter_modes.end()) {
      options.debug_filter_function =
          [&iol, mode = it->second](bool exclude,
                                    writer::entry_interface const& ei) {
            debug_filter_output(iol.out, exclude, ei, mode);
          };
      no_progress = true;
    } else {
      iol.err << "error: invalid filter debug mode '" << debug_filter << "'\n";
      return 1;
    }
  }

  if (!progress_modes.contains(progress_mode)) {
    iol.err << "error: invalid progress mode '" << progress_mode << "'\n";
    return 1;
  }

  if (no_progress) {
    progress_mode = "none";
  }

  if (progress_mode != "none" && !iol.term->is_tty(iol.err)) {
    progress_mode = "simple";
  }

  writer::console_writer::options const cwopts{
      .progress = DWARFS_NOTHROW(progress_modes.at(progress_mode)),
      .display = recompress ? writer::console_writer::REWRITE
                            : writer::console_writer::NORMAL,
      .enable_sparse_files = !no_sparse_files,
  };

  writer::console_writer lgr(iol.term, iol.err, cwopts, logopts);

  if (get_self_memory_usage()) {
    lgr.set_memory_usage_function(
        [] { return get_self_memory_usage().value_or(0); });
  }

  std::unique_ptr<writer::rule_based_entry_filter> rule_filter;

  if (!filter.empty()) {
    rule_filter =
        std::make_unique<writer::rule_based_entry_filter>(lgr, iol.file);

    rule_filter->set_root_path(path);

    for (auto const& rule : filter) {
      auto srule = sys_string_to_string(rule);
      try {
        rule_filter->add_rule(srule);
      } catch (std::exception const& e) {
        iol.err << "error: could not parse filter rule '" << srule
                << "': " << e.what() << "\n";
        return 1;
      }
    }
  }

  if (vm.contains("chmod")) {
    if (chmod_str == "norm") {
      chmod_str = "ug-st,=Xr";
    }

    options.metadata.chmod_specifiers = chmod_str;
    options.metadata.umask = get_current_umask();
  }

  if (vm.contains("set-owner")) {
    options.metadata.uid = uid;
  }

  if (vm.contains("set-group")) {
    options.metadata.gid = gid;
  }

  if (vm.contains("set-time")) {
    if (timestamp == "now") {
      options.metadata.timestamp = std::time(nullptr);
    } else if (auto val = try_to<uint64_t>(timestamp)) {
      options.metadata.timestamp = val;
    } else {
      try {
        auto tp = parse_time_point(timestamp);
        options.metadata.timestamp =
            std::chrono::duration_cast<std::chrono::seconds>(
                tp.time_since_epoch())
                .count();
      } catch (std::exception const& e) {
        iol.err << "error: " << e.what() << "\n";
        return 1;
      }
    }
  }

  if (vm.contains("time-resolution")) {
    try {
      auto const res = parse_time_with_unit(time_resolution);
      if (res.count() == 0) {
        iol.err
            << "error: the argument to '--time-resolution' must be nonzero\n";
        return 1;
      }
      options.metadata.time_resolution = res;
    } catch (std::exception const& e) {
      iol.err << "error: the argument ('" << time_resolution
              << "') to '--time-resolution' is invalid (" << e.what() << ")\n";
      return 1;
    }
  }

  if (!pack_metadata.empty() and pack_metadata != "none") {
    if (pack_metadata == "auto") {
      options.metadata.force_pack_string_tables = false;
      options.metadata.pack_chunk_table = false;
      options.metadata.pack_directories = false;
      options.metadata.pack_shared_files_table = false;
      options.metadata.pack_names = true;
      options.metadata.pack_names_index = false;
      options.metadata.pack_symlinks = true;
      options.metadata.pack_symlinks_index = false;
    } else {
      auto pack_opts =
          split_to<std::vector<std::string_view>>(pack_metadata, ',');
      for (auto const& opt : pack_opts) {
        if (opt == "chunk_table") {
          options.metadata.pack_chunk_table = true;
        } else if (opt == "directories") {
          options.metadata.pack_directories = true;
        } else if (opt == "shared_files") {
          options.metadata.pack_shared_files_table = true;
        } else if (opt == "names") {
          options.metadata.pack_names = true;
        } else if (opt == "names_index") {
          options.metadata.pack_names_index = true;
        } else if (opt == "symlinks") {
          options.metadata.pack_symlinks = true;
        } else if (opt == "symlinks_index") {
          options.metadata.pack_symlinks_index = true;
        } else if (opt == "force") {
          options.metadata.force_pack_string_tables = true;
        } else if (opt == "plain") {
          options.metadata.plain_names_table = true;
          options.metadata.plain_symlinks_table = true;
        } else if (opt == "all") {
          options.metadata.pack_chunk_table = true;
          options.metadata.pack_directories = true;
          options.metadata.pack_shared_files_table = true;
          options.metadata.pack_names = true;
          options.metadata.pack_names_index = true;
          options.metadata.pack_symlinks = true;
          options.metadata.pack_symlinks_index = true;
        } else {
          iol.err << "error: the argument ('" << opt
                  << "') to '--pack-metadata' is invalid\n";
          return 1;
        }
      }
    }
  }

  auto interval = cwopts.progress == writer::console_writer::NONE ||
                          cwopts.progress == writer::console_writer::SIMPLE
                      ? 2000ms
                      : 200ms;

  std::unique_ptr<input_stream> header_ifs;

  if (!header_str.empty()) {
    std::filesystem::path header(header_str);
    std::error_code ec;
    header_ifs = iol.file->open_input_binary(header, ec);
    if (ec) {
      iol.err << "error: cannot open header file '" << header
              << "': " << ec.message() << "\n";
      return 1;
    }
  }

  LOG_PROXY(debug_logger_policy, lgr);

  if (auto const res = options.metadata.time_resolution) {
    if (auto const native = iol.os->native_file_time_resolution();
        *res < native) {
      LOG_WARN << "requested time resolution of " << time_with_unit(*res)
               << " is finer than the native file timestamp resolution of "
               << time_with_unit(native);
    }
  }

  try {
    writer::metadata_options::validate(options.metadata);
  } catch (std::exception const& e) {
    LOG_ERROR << "invalid metadata option: " << e.what();
    return 1;
  }

  writer::writer_progress::update_function_type updater;

  if (options.debug_filter_function) {
    updater = [](writer::writer_progress&, bool) {};
  } else {
    updater = [&](writer::writer_progress& p, bool last) {
      lgr.update(p, last);
    };
  }

  writer::writer_progress prog(std::move(updater), interval);

  // No more streaming to iol.err after this point as this would
  // cause a race with the progress thread.

  size_t mem_limit = 0;

  if (memory_limit == "auto") {
    mem_limit = compute_memory_limit(UINT64_C(1) << sf_config.block_size_bits,
                                     num_workers);
    LOG_VERBOSE << "using memory limit of " << size_with_unit(mem_limit);
  } else {
    mem_limit = parse_size_with_unit(memory_limit);
  }

  auto min_memory_req =
      num_workers * (UINT64_C(1) << sf_config.block_size_bits);

  // TODO:
  if (mem_limit < min_memory_req /* && compression != "null" */) {
    LOG_WARN << "low memory limit (" << size_with_unit(mem_limit) << "), need "
             << size_with_unit(min_memory_req) << " to efficiently compress "
             << size_with_unit(UINT64_C(1) << sf_config.block_size_bits)
             << " blocks with " << num_workers << " threads";
  }

  std::filesystem::path output(output_str);

  std::variant<std::monostate, std::unique_ptr<output_stream>,
               std::ostringstream>
      os;

  if (!options.debug_filter_function) {
    if (output != "-") {
      if (iol.file->exists(output) && !force_overwrite) {
        LOG_ERROR << "output file already exists, use --force to overwrite";
        return 1;
      }

      std::error_code ec;
      auto stream = iol.file->open_output_binary(output, ec);

      if (ec) {
        LOG_ERROR << "cannot open output file '" << output
                  << "': " << ec.message();
        return 1;
      }

      assert(stream);

      os.emplace<std::unique_ptr<output_stream>>(std::move(stream));
    } else {
      ensure_binary_mode(iol.out);
    }
  } else {
    os.emplace<std::ostringstream>();
  }

  options.enable_history = !no_history;
  rw_opts.enable_history = !no_history;

  if (options.enable_history) {
    options.history.with_timestamps = !no_history_timestamps;
    rw_opts.history.with_timestamps = !no_history_timestamps;

    if (!no_history_command_line) {
      options.command_line_arguments = command_line;
      rw_opts.command_line_arguments = command_line;
    }
  }

  if (!categorizer_list.empty()) {
    auto categorizers =
        split_to<std::vector<std::string>>(categorizer_list.value(), ',');

    options.inode.categorizer_mgr =
        std::make_shared<writer::categorizer_manager>(lgr, path);

    try {
      for (auto const& name : categorizers) {
        options.inode.categorizer_mgr->add(
            catreg.create(lgr, name, vm, iol.file));
      }
    } catch (std::exception const& e) {
      LOG_ERROR << "could not create categorizer: " << e.what();
      return 1;
    }
  }

  std::optional<reader::filesystem_v2> input_filesystem;
  std::shared_ptr<writer::category_resolver> cat_resolver;

  if (recompress) {
    input_filesystem.emplace(
        lgr, *iol.os, path,
        reader::filesystem_options{
            .image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});

    LOG_INFO << "checking input filesystem...";

    {
      auto tv = LOG_TIMED_VERBOSE;

      if (auto num_errors =
              input_filesystem->check(reader::filesystem_check_level::CHECKSUM);
          num_errors != 0) {
        LOG_ERROR << "input filesystem is corrupt: detected " << num_errors
                  << " error(s)";
        return 1;
      }

      tv << "checked input filesystem";
    }

    cat_resolver = std::make_shared<writer::filesystem_block_category_resolver>(
        input_filesystem->get_all_block_categories());

    for (auto const& cat : rw_opts.recompress_categories) {
      if (!cat_resolver->category_value(cat)) {
        LOG_ERROR << "no category '" << cat << "' in input filesystem";
        return 1;
      }
    }
  } else {
    cat_resolver = options.inode.categorizer_mgr;
  }

  std::unordered_set<std::string_view> accepted_categories;

  for (auto const& name : catreg.categorizer_names()) {
    stream_logger lgr(iol.term, iol.err);
    auto categorizer = catreg.create(lgr, name, vm, iol.file);
    for (auto cat : categorizer->categories()) {
      accepted_categories.insert(cat);
    }
  }

  writer::category_parser cp(cat_resolver, accepted_categories);

  try {
    {
      writer::contextual_option_parser cop(
          "--order", options.inode.fragment_order, cp, order_parser);
      cop.parse(defaults.order);
      cop.parse(order);
      categorizer_list.add_implicit_defaults(cop);
      LOG_VERBOSE << cop.as_string();
    }

    {
      writer::contextual_option_parser cop("--max-lookback-blocks",
                                           sf_config.max_active_blocks, cp,
                                           max_lookback_parser);
      sf_config.max_active_blocks.set_default(kDefaultMaxActiveBlocks);
      cop.parse(max_lookback_blocks);
      categorizer_list.add_implicit_defaults(cop);
      LOG_VERBOSE << cop.as_string();
    }

    {
      writer::contextual_option_parser cop("--window-size",
                                           sf_config.blockhash_window_size, cp,
                                           window_size_parser);
      sf_config.blockhash_window_size.set_default(defaults.window_size);
      cop.parse(window_size);
      categorizer_list.add_implicit_defaults(cop);
      LOG_VERBOSE << cop.as_string();
    }

    {
      writer::contextual_option_parser cop("--window-step",
                                           sf_config.window_increment_shift, cp,
                                           window_step_parser);
      sf_config.window_increment_shift.set_default(defaults.window_step);
      cop.parse(window_step);
      categorizer_list.add_implicit_defaults(cop);
      LOG_VERBOSE << cop.as_string();
    }

    {
      writer::contextual_option_parser cop("--bloom-filter-size",
                                           sf_config.bloom_filter_size, cp,
                                           bloom_filter_size_parser);
      sf_config.bloom_filter_size.set_default(kDefaultBloomFilterSize);
      cop.parse(bloom_filter_size);
      categorizer_list.add_implicit_defaults(cop);
      LOG_VERBOSE << cop.as_string();
    }
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    return 1;
  }

  sf_config.enable_sparse_files = !no_sparse_files;
  options.metadata.enable_sparse_files = !no_sparse_files;

  block_compressor schema_bc(schema_compression);
  block_compressor metadata_bc(metadata_compression);
  block_compressor history_bc(history_compression);

  thread_pool compress_pool(lgr, *iol.os, "compress", num_workers,
                            std::numeric_limits<size_t>::max(),
                            compress_niceness);

  writer::filesystem_writer_options fswopts;
  fswopts.max_queue_size = mem_limit;
  fswopts.worst_case_block_size = UINT64_C(1) << sf_config.block_size_bits;
  fswopts.remove_header = remove_header;
  fswopts.no_section_index = no_section_index;

  std::optional<writer::filesystem_writer> fsw;

  try {
    std::ostream& fsw_os =
        os |
        match{[&](std::monostate) -> std::ostream& { return iol.out; },
              [&](std::unique_ptr<output_stream>& os) -> std::ostream& {
                return os->os();
              },
              [&](std::ostringstream& oss) -> std::ostream& { return oss; }};

    fsw.emplace(fsw_os, lgr, compress_pool, prog, fswopts,
                header_ifs ? &header_ifs->is() : nullptr);

    fsw->add_section_compressor(section_type::METADATA_V2_SCHEMA, schema_bc);
    fsw->add_section_compressor(section_type::METADATA_V2, metadata_bc);
    fsw->add_section_compressor(section_type::HISTORY, history_bc);

    writer::categorized_option<block_compressor> compression_opt;
    writer::contextual_option_parser cop("--compression", compression_opt, cp,
                                         compressor_parser);
    compression_opt.set_default(
        block_compressor(std::string(defaults.data_compression)));
    cop.parse(compression);
    categorizer_list.add_implicit_defaults(cop);
    LOG_VERBOSE << cop.as_string();

    {
      auto bc = compression_opt.get();

      if (!bc.metadata_requirements().empty()) {
        throw std::runtime_error(
            fmt::format("compression '{}' cannot be used without a category: "
                        "metadata requirements not met",
                        bc.describe()));
      }

      fsw->add_default_compressor(std::move(bc));
    }

    if (recompress) {
      compression_opt.visit_contextual(
          [&fsw](auto cat, block_compressor const& bc) {
            fsw->add_category_compressor(cat, bc);
          });
    } else {
      compression_opt.visit_contextual([catmgr = options.inode.categorizer_mgr,
                                        &fsw](auto cat,
                                              block_compressor const& bc) {
        try {
          catmgr->set_metadata_requirements(cat, bc.metadata_requirements());
          fsw->add_category_compressor(cat, bc);
        } catch (std::exception const& e) {
          throw std::runtime_error(
              fmt::format("compression '{}' cannot be used for category '{}': "
                          "metadata requirements not met ({})",
                          bc.describe(), catmgr->category_name(cat), e.what()));
        }
      });
    }
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    return 1;
  }

  auto ti = LOG_TIMED_INFO;

  try {
    if (recompress) {
      if (rebuild_metadata || change_block_size) {
        rw_opts.rebuild_metadata = options.metadata;
      }
      if (change_block_size) {
        rw_opts.change_block_size = UINT64_C(1) << sf_config.block_size_bits;
      }
      utility::rewrite_filesystem(lgr, *input_filesystem, *fsw, *cat_resolver,
                                  rw_opts, extra_deps);
    } else {
      writer::segmenter_factory sf(lgr, prog, options.inode.categorizer_mgr,
                                   sf_config);
      writer::entry_factory ef;

      thread_pool scanner_pool(lgr, *iol.os, "scanner", num_scanner_workers);

      writer::scanner s(lgr, scanner_pool, sf, ef, *iol.os, options);

      if (rule_filter) {
        s.add_filter(std::move(rule_filter));
      }

      s.scan(*fsw, path, prog, input_list, iol.file, extra_deps);

      options.inode.categorizer_mgr.reset();
    }
  } catch (dwarfs::error const& e) {
    LOG_ERROR << exception_str(e);
    return 1;
  } catch (std::exception const& e) {
    LOG_ERROR << exception_str(e);
    return 1;
  }

  if (!options.debug_filter_function) {
    std::error_code ec;
    auto cpu_time = compress_pool.get_cpu_time(ec);
    if (ec) {
      LOG_WARN << "could not measure CPU time: " << ec.message();
    } else {
      LOG_INFO << "compression CPU time: " << time_with_unit(cpu_time);
    }
  }

  {
    auto ec = os | match{[](std::monostate) -> int { return 0; },
                         [&](std::unique_ptr<output_stream>& os) -> int {
                           std::error_code ec;
                           os->close(ec);
                           if (ec) {
                             LOG_ERROR << "failed to close output file '"
                                       << output << "': " << ec.message();
                             return 1;
                           }
                           os.reset();
                           return 0;
                         },
                         [](std::ostringstream& oss [[maybe_unused]]) -> int {
                           assert(oss.str().empty());
                           return 0;
                         }};

    if (ec != 0) {
      return ec;
    }
  }

  auto errors = prog.errors();

  if (!options.debug_filter_function) {
    std::ostringstream err;

    if (errors) {
      err << "with " << errors << " error";
      if (errors > 1) {
        err << "s";
      }
    } else {
      err << "without errors";
    }

    ti << "filesystem " << (recompress ? "rewritten " : "created ")
       << err.str();
  }

  return errors > 0 ? 2 : 0;
}

} // namespace dwarfs::tool

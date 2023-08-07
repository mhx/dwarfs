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
#include <array>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
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
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/gen/String.h>
#include <folly/portability/SysStat.h>
#include <folly/system/HardwareConcurrency.h>

#include <fmt/format.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/builtin_script.h"
#include "dwarfs/chmod_transformer.h"
#include "dwarfs/console_writer.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/options_interface.h"
#include "dwarfs/os_access_generic.h"
#include "dwarfs/program_options_helpers.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "dwarfs/terminal.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

namespace po = boost::program_options;

namespace dwarfs {

namespace {

enum class debug_filter_mode {
  OFF,
  INCLUDED,
  INCLUDED_FILES,
  EXCLUDED,
  EXCLUDED_FILES,
  FILES,
  ALL
};

const std::map<std::string, file_order_mode> order_choices{
    {"none", file_order_mode::NONE},
    {"path", file_order_mode::PATH},
    {"similarity", file_order_mode::SIMILARITY},
    {"nilsimsa", file_order_mode::NILSIMSA},
};

const std::map<std::string, console_writer::progress_mode> progress_modes{
    {"none", console_writer::NONE},
    {"simple", console_writer::SIMPLE},
    {"ascii", console_writer::ASCII},
#ifndef _WIN32
    {"unicode", console_writer::UNICODE},
#endif
};

const std::string default_progress_mode =
#ifdef _WIN32
    "ascii"
#else
    "unicode"
#endif
    ;

const std::map<std::string, debug_filter_mode> debug_filter_modes{
    {"included", debug_filter_mode::INCLUDED},
    {"included-files", debug_filter_mode::INCLUDED_FILES},
    {"excluded", debug_filter_mode::EXCLUDED},
    {"excluded-files", debug_filter_mode::EXCLUDED_FILES},
    {"files", debug_filter_mode::FILES},
    {"all", debug_filter_mode::ALL},
};

const std::map<std::string, uint32_t> time_resolutions{
    {"sec", 1},
    {"min", 60},
    {"hour", 3600},
    {"day", 86400},
};

constexpr size_t min_block_size_bits{10};
constexpr size_t max_block_size_bits{30};

void debug_filter_output(std::ostream& os, bool exclude, entry const* pe,
                         debug_filter_mode mode) {
  if (exclude ? mode == debug_filter_mode::INCLUDED or
                    mode == debug_filter_mode::INCLUDED_FILES
              : mode == debug_filter_mode::EXCLUDED or
                    mode == debug_filter_mode::EXCLUDED_FILES) {
    return;
  }

  bool const files_only = mode == debug_filter_mode::FILES or
                          mode == debug_filter_mode::INCLUDED_FILES or
                          mode == debug_filter_mode::EXCLUDED_FILES;

  if (files_only and pe->type() == entry::E_DIR) {
    return;
  }

  char const* prefix = "";

  if (mode == debug_filter_mode::FILES or mode == debug_filter_mode::ALL) {
    prefix = exclude ? "- " : "+ ";
  }

  os << prefix << pe->unix_dpath() << "\n";
}

int parse_order_option(std::string const& ordname, std::string const& opt,
                       int& value, std::string_view name,
                       std::optional<int> min = std::nullopt,
                       std::optional<int> max = std::nullopt) {
  if (!opt.empty()) {
    if (auto val = folly::tryTo<int>(opt)) {
      auto tmp = *val;
      if (min && max && (tmp < *min || tmp > *max)) {
        std::cerr << "error: " << name << " (" << opt
                  << ") out of range for order '" << ordname << "' (" << *min
                  << ".." << *max << ")\n";
        return 1;
      }
      if (min && tmp < *min) {
        std::cerr << "error: " << name << " (" << opt
                  << ") cannot be less than " << *min << " for order '"
                  << ordname << "'\n";
      }
      if (max && tmp > *max) {
        std::cerr << "error: " << name << " (" << opt
                  << ") cannot be greater than " << *max << " for order '"
                  << ordname << "'\n";
      }
      value = tmp;
    } else {
      std::cerr << "error: " << name << " (" << opt
                << ") is not numeric for order '" << ordname << "'\n";
      return 1;
    }
  }
  return 0;
}

struct level_defaults {
  unsigned block_size_bits;
  std::string_view data_compression;
  std::string_view schema_compression;
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
#define ALG_SCHEMA "zstd:level=12"
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

constexpr unsigned default_level = 7;

} // namespace

int mkdwarfs_main(int argc, sys_char** argv) {
  using namespace folly::gen;

  const size_t num_cpu = std::max(folly::hardware_concurrency(), 1u);

  block_manager::config cfg;
  sys_string path_str, output_str;
  std::string memory_limit, script_arg, compression, header, schema_compression,
      metadata_compression, log_level_str, timestamp, time_resolution, order,
      progress_mode, recompress_opts, pack_metadata, file_hash_algo,
      debug_filter, max_similarity_size, input_list_str, chmod_str;
  std::vector<sys_string> filter;
  size_t num_workers, num_scanner_workers;
  bool no_progress = false, remove_header = false, no_section_index = false,
       force_overwrite = false;
  unsigned level;
  int compress_niceness;
  uint16_t uid, gid;

  scanner_options options;

  auto order_desc =
      "inode order (" + (from(order_choices) | get<0>() | unsplit(", ")) + ")";

  auto progress_desc = "progress mode (" +
                       (from(progress_modes) | get<0>() | unsplit(", ")) + ")";

  auto debug_filter_desc =
      "show effect of filter rules without producing an image (" +
      (from(debug_filter_modes) | get<0>() | unsplit(", ")) + ")";

  auto resolution_desc = "time resolution in seconds or (" +
                         (from(time_resolutions) | get<0>() | unsplit(", ")) +
                         ")";

  auto hash_list = checksum::available_algorithms();

  auto file_hash_desc = "choice of file hashing function (none, " +
                        (from(hash_list) | unsplit(", ")) + ")";

  // clang-format off
  po::options_description basic_opts("Options");
  basic_opts.add_options()
    ("input,i",
        po_sys_value<sys_string>(&path_str),
        "path to root directory or source filesystem")
    ("input-list",
        po::value<std::string>(&input_list_str),
        "file containing list of paths relative to root directory")
    ("output,o",
        po_sys_value<sys_string>(&output_str),
        "filesystem output name")
    ("force,f",
        po::value<bool>(&force_overwrite)->zero_tokens(),
        "force overwrite of existing output image")
    ("compress-level,l",
        po::value<unsigned>(&level)->default_value(default_level),
        "compression level (0=fast, 9=best, please see man page for details)")
    ("log-level",
        po::value<std::string>(&log_level_str)->default_value("info"),
        "log level (error, warn, info, debug, trace)")
    ("help,h",
        "output help message and exit")
    ("long-help,H",
        "output full help message and exit")
    ;

  po::options_description advanced_opts("Advanced options");
  advanced_opts.add_options()
    ("block-size-bits,S",
        po::value<unsigned>(&cfg.block_size_bits),
        "block size bits (size = 2^arg bits)")
    ("num-workers,N",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of writer (compression) worker threads")
    ("compress-niceness",
        po::value<int>(&compress_niceness)->default_value(5),
        "compression worker threads niceness")
    ("num-scanner-workers",
        po::value<size_t>(&num_scanner_workers),
        "number of scanner (hashing) worker threads")
    ("memory-limit,L",
        po::value<std::string>(&memory_limit)->default_value("1g"),
        "block manager memory limit")
    ("recompress",
        po::value<std::string>(&recompress_opts)->implicit_value("all"),
        "recompress an existing filesystem (none, block, metadata, all)")
    ("order",
        po::value<std::string>(&order),
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
    ("header",
        po::value<std::string>(&header),
        "prepend output filesystem with contents of this file")
    ("remove-header",
        po::value<bool>(&remove_header)->zero_tokens(),
        "remove any header present before filesystem data"
        " (use with --recompress)")
    ("no-section-index",
        po::value<bool>(&no_section_index)->zero_tokens(),
        "don't add section index to file system")
    ;

  po::options_description segmenter_opts("Segmenter options");
  segmenter_opts.add_options()
    ("max-lookback-blocks,B",
        po::value<size_t>(&cfg.max_active_blocks)->default_value(1),
        "how many blocks to scan for segments")
    ("window-size,W",
        po::value<unsigned>(&cfg.blockhash_window_size),
        "window sizes for block hashing")
    ("window-step,w",
        po::value<unsigned>(&cfg.window_increment_shift),
        "window step (as right shift of size)")
    ("bloom-filter-size",
        po::value<unsigned>(&cfg.bloom_filter_size)->default_value(4),
        "bloom filter size (2^N*values bits)")
    ;

  po::options_description compressor_opts("Compressor options");
  compressor_opts.add_options()
    ("compression,C",
        po::value<std::string>(&compression),
        "block compression algorithm")
    ("schema-compression",
        po::value<std::string>(&schema_compression),
        "metadata schema compression algorithm")
    ("metadata-compression",
        po::value<std::string>(&metadata_compression),
        "metadata compression algorithm")
    ;

  po::options_description filter_opts("Filter options");
  filter_opts.add_options()
    ("filter,F",
        po_sys_value<std::vector<sys_string>>(&filter)->multitoken(),
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
        po::value<bool>(&options.no_create_timestamp)->zero_tokens(),
        "don't add create timestamp to file system")
    ("set-time",
        po::value<std::string>(&timestamp),
        "set timestamp for whole file system (unixtime or 'now')")
    ("keep-all-times",
        po::value<bool>(&options.keep_all_times)->zero_tokens(),
        "save atime and ctime in addition to mtime")
    ("time-resolution",
        po::value<std::string>(&time_resolution)->default_value("sec"),
        resolution_desc.c_str())
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

  po::variables_map vm;

  auto& sys_err_out = SYS_CERR;

  try {
    auto parsed = po::parse_command_line(argc, argv, opts);

    po::store(parsed, vm);
    po::notify(vm);

    auto unrecognized =
        po::collect_unrecognized(parsed.options, po::include_positional);

    if (!unrecognized.empty()) {
      sys_err_out << "error: unrecognized argument(s) '"
                  << boost::join(unrecognized, " ") << "'\n";
      return 1;
    }
  } catch (po::error const& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  auto const usage = "Usage: mkdwarfs [OPTIONS...]\n";

  if (vm.count("long-help")) {
    size_t l_dc = 0, l_sc = 0, l_mc = 0, l_or = 0;
    for (auto const& l : levels) {
      l_dc = std::max(l_dc, l.data_compression.size());
      l_sc = std::max(l_sc, l.schema_compression.size());
      l_mc = std::max(l_mc, l.metadata_compression.size());
      l_or = std::max(l_or, l.order.size());
    }

    std::string sep(30 + l_dc + l_sc + l_mc + l_or, '-');

    std::cout << tool_header("mkdwarfs") << usage << opts << "\n"
              << "Compression level defaults:\n"
              << "  " << sep << "\n"
              << fmt::format("  Level  Block  {:{}s} {:s}     Inode\n",
                             "Compression Algorithm", 4 + l_dc + l_sc + l_mc,
                             "Window")
              << fmt::format("         Size   {:{}s}  {:{}s}  {:{}s} {:6s}\n",
                             "Block Data", l_dc, "Schema", l_sc, "Metadata",
                             l_mc, "Size/Step  Order")
              << "  " << sep << "\n";

    int level = 0;
    for (auto const& l : levels) {
      std::cout << fmt::format("  {:1d}      {:2d}     {:{}s}  {:{}s}  {:{}s}"
                               "  {:2d} / {:1d}    {:{}s}",
                               level, l.block_size_bits, l.data_compression,
                               l_dc, l.schema_compression, l_sc,
                               l.metadata_compression, l_mc, l.window_size,
                               l.window_step, l.order, l_or)
                << "\n";
      ++level;
    }

    std::cout << "  " << sep << "\n";

    std::cout << "\nCompression algorithms:\n";

    compression_registry::instance().for_each_algorithm(
        [](compression_type, compression_info const& info) {
          std::cout << fmt::format("  {:9}{}\n", info.name(),
                                   info.description());
          for (auto const& opt : info.options()) {
            std::cout << fmt::format("               {}\n", opt);
          }
        });

    std::cout << "\n";

    return 0;
  }

  if (vm.count("help") or !(vm.count("input") or vm.count("input-list")) or
      (!vm.count("output") and !vm.count("debug-filter"))) {
    std::cout << tool_header("mkdwarfs") << usage << "\n" << basic_opts << "\n";
    return 0;
  }

  if (level >= levels.size()) {
    std::cerr << "error: invalid compression level\n";
    return 1;
  }

  auto const& defaults = levels[level];

  if (!vm.count("block-size-bits")) {
    cfg.block_size_bits = defaults.block_size_bits;
  }

  if (!vm.count("compression")) {
    compression = defaults.data_compression;
  }

  if (!vm.count("schema-compression")) {
    schema_compression = defaults.schema_compression;
  }

  if (!vm.count("metadata-compression")) {
    metadata_compression = defaults.metadata_compression;
  }

  if (!vm.count("window-size")) {
    cfg.blockhash_window_size = defaults.window_size;
  }

  if (!vm.count("window-step")) {
    cfg.window_increment_shift = defaults.window_step;
  }

  if (!vm.count("order")) {
    order = defaults.order;
  }

  if (cfg.block_size_bits < min_block_size_bits ||
      cfg.block_size_bits > max_block_size_bits) {
    std::cerr << "error: block size must be between " << min_block_size_bits
              << " and " << max_block_size_bits << "\n";
    return 1;
  }

  std::filesystem::path path(path_str);
  std::optional<std::vector<std::filesystem::path>> input_list;

  if (vm.count("input-list")) {
    if (vm.count("filter")) {
      std::cerr << "error: cannot use --input-list and --filter\n";
      return 1;
    }

    // implicitly turn on
    options.with_devices = true;
    options.with_specials = true;

    if (!vm.count("input")) {
      path = std::filesystem::current_path();
    }

    std::unique_ptr<std::ifstream> ifs;
    std::istream* is;

    if (input_list_str == "-") {
      is = &std::cin;
    } else {
      ifs = std::make_unique<std::ifstream>(input_list_str);

      if (!ifs->is_open()) {
        throw std::runtime_error(
            fmt::format("error opening file: {}", input_list_str));
      }

      is = ifs.get();
    }

    std::string line;
    input_list.emplace();

    while (std::getline(*is, line)) {
      input_list->emplace_back(line);
    }
  }

  path = canonical_path(path);

  bool recompress = vm.count("recompress");
  rewrite_options rw_opts;
  if (recompress) {
    std::unordered_map<std::string, unsigned> const modes{
        {"all", 3},
        {"metadata", 2},
        {"block", 1},
        {"none", 0},
    };
    if (auto it = modes.find(recompress_opts); it != modes.end()) {
      rw_opts.recompress_block = it->second & 1;
      rw_opts.recompress_metadata = it->second & 2;
    } else {
      std::cerr << "invalid recompress mode: " << recompress_opts << "\n";
      return 1;
    }
  }

  std::vector<std::string> order_opts;
  boost::split(order_opts, order, boost::is_any_of(":"));

  if (auto it = order_choices.find(order_opts.front());
      it != order_choices.end()) {
    options.file_order.mode = it->second;

    if (order_opts.size() > 1) {
      if (options.file_order.mode != file_order_mode::NILSIMSA) {
        std::cerr << "error: inode order mode '" << order_opts.front()
                  << "' does not support options\n";
        return 1;
      }

      if (order_opts.size() > 4) {
        std::cerr << "error: too many options for inode order mode '"
                  << order_opts[0] << "'\n";
        return 1;
      }

      auto ordname = order_opts[0];

      if (parse_order_option(ordname, order_opts[1],
                             options.file_order.nilsimsa_limit, "limit", 0,
                             255)) {
        return 1;
      }

      if (order_opts.size() > 2) {
        if (parse_order_option(ordname, order_opts[2],
                               options.file_order.nilsimsa_depth, "depth", 0)) {
          return 1;
        }
      }

      if (order_opts.size() > 3) {
        if (parse_order_option(ordname, order_opts[3],
                               options.file_order.nilsimsa_min_depth,
                               "min depth", 0)) {
          return 1;
        }
      }
    }
  } else {
    std::cerr << "error: invalid inode order mode: " << order << "\n";
    return 1;
  }

  if (file_hash_algo == "none") {
    options.file_hash_algorithm.reset();
  } else if (checksum::is_available(file_hash_algo)) {
    options.file_hash_algorithm = file_hash_algo;
  } else {
    std::cerr << "error: unknown file hash function '" << file_hash_algo
              << "'\n";
    return 1;
  }

  if (vm.count("max-similarity-size")) {
    auto size = parse_size_with_unit(max_similarity_size);
    if (size > 0) {
      options.inode.max_similarity_scan_size = size;
    }
  }

  size_t mem_limit = parse_size_with_unit(memory_limit);

  if (!vm.count("num-scanner-workers")) {
    num_scanner_workers = num_workers;
  }

  worker_group wg_compress("compress", num_workers,
                           std::numeric_limits<size_t>::max(),
                           compress_niceness);
  worker_group wg_scanner("scanner", num_scanner_workers);

  if (vm.count("debug-filter")) {
    if (auto it = debug_filter_modes.find(debug_filter);
        it != debug_filter_modes.end()) {
      options.debug_filter_function = [mode = it->second](bool exclude,
                                                          entry const* pe) {
        debug_filter_output(std::cout, exclude, pe, mode);
      };
      no_progress = true;
    } else {
      std::cerr << "error: invalid filter debug mode '" << debug_filter
                << "'\n";
      return 1;
    }
  }

  if (no_progress) {
    progress_mode = "none";
  }
  if (progress_mode != "none" && !stream_is_fancy_terminal(std::cerr)) {
    progress_mode = "simple";
  }
  if (!progress_modes.count(progress_mode)) {
    std::cerr << "error: invalid progress mode '" << progress_mode << "'\n";
    return 1;
  }

  auto pg_mode = DWARFS_NOTHROW(progress_modes.at(progress_mode));
  auto log_level = logger::parse_level(log_level_str);

  console_writer lgr(std::cerr, pg_mode, get_term_width, log_level,
                     recompress ? console_writer::REWRITE
                                : console_writer::NORMAL,
                     log_level >= logger::DEBUG);

  std::shared_ptr<script> script;

  if (!filter.empty() or vm.count("chmod")) {
    auto bs = std::make_shared<builtin_script>(lgr);

    if (!filter.empty()) {
      bs->set_root_path(path);

      for (auto const& rule : filter) {
        auto srule = sys_string_to_string(rule);
        try {
          bs->add_filter_rule(srule);
        } catch (std::exception const& e) {
          std::cerr << "error: could not parse filter rule '" << srule
                    << "': " << e.what() << "\n";
          return 1;
        }
      }
    }

    if (vm.count("chmod")) {
      if (chmod_str == "norm") {
        chmod_str = "ug-st,=Xr";
      }

      std::vector<std::string_view> chmod_exprs;
      boost::split(chmod_exprs, chmod_str, boost::is_any_of(","));

      // I'm pretty certain these warnings by Flawfinder are false positives.
      // After all, we're just doing a no-op by re-setting the original value
      // in order to read it.
      auto mask = ::umask(0077); /* Flawfinder: ignore */
      ::umask(mask);             /* Flawfinder: ignore */

      for (auto expr : chmod_exprs) {
        bs->add_transformer(create_chmod_transformer(expr, mask));
      }
    }

    script = bs;
  }

  if (vm.count("set-owner")) {
    options.uid = uid;
  }

  if (vm.count("set-group")) {
    options.gid = gid;
  }

  if (vm.count("set-time")) {
    if (timestamp == "now") {
      options.timestamp = std::time(nullptr);
    } else if (auto val = folly::tryTo<uint64_t>(timestamp)) {
      options.timestamp = *val;
    } else {
      std::cerr << "error: argument for option '--set-time' must be numeric or "
                   "`now`\n";
      return 1;
    }
  }

  if (auto it = time_resolutions.find(time_resolution);
      it != time_resolutions.end()) {
    options.time_resolution_sec = it->second;
  } else if (auto val = folly::tryTo<uint32_t>(time_resolution)) {
    options.time_resolution_sec = *val;
    if (options.time_resolution_sec == 0) {
      std::cerr
          << "error: the argument to '--time-resolution' must be nonzero\n";
      return 1;
    }
  } else {
    std::cerr << "error: the argument ('" << time_resolution
              << "') to '--time-resolution' is invalid\n";
    return 1;
  }

  if (!pack_metadata.empty() and pack_metadata != "none") {
    if (pack_metadata == "auto") {
      options.force_pack_string_tables = false;
      options.pack_chunk_table = false;
      options.pack_directories = false;
      options.pack_shared_files_table = false;
      options.pack_names = true;
      options.pack_names_index = false;
      options.pack_symlinks = true;
      options.pack_symlinks_index = false;
    } else {
      std::vector<std::string> pack_opts;
      boost::split(pack_opts, pack_metadata, boost::is_any_of(","));
      for (auto const& opt : pack_opts) {
        if (opt == "chunk_table") {
          options.pack_chunk_table = true;
        } else if (opt == "directories") {
          options.pack_directories = true;
        } else if (opt == "shared_files") {
          options.pack_shared_files_table = true;
        } else if (opt == "names") {
          options.pack_names = true;
        } else if (opt == "names_index") {
          options.pack_names_index = true;
        } else if (opt == "symlinks") {
          options.pack_symlinks = true;
        } else if (opt == "symlinks_index") {
          options.pack_symlinks_index = true;
        } else if (opt == "force") {
          options.force_pack_string_tables = true;
        } else if (opt == "plain") {
          options.plain_names_table = true;
          options.plain_symlinks_table = true;
        } else if (opt == "all") {
          options.pack_chunk_table = true;
          options.pack_directories = true;
          options.pack_shared_files_table = true;
          options.pack_names = true;
          options.pack_names_index = true;
          options.pack_symlinks = true;
          options.pack_symlinks_index = true;
        } else {
          std::cerr << "error: the argument ('" << opt
                    << "') to '--pack-metadata' is invalid\n";
          return 1;
        }
      }
    }
  }

  unsigned interval_ms =
      pg_mode == console_writer::NONE || pg_mode == console_writer::SIMPLE
          ? 2000
          : 200;

  filesystem_writer_options fswopts;
  fswopts.max_queue_size = mem_limit;
  fswopts.remove_header = remove_header;
  fswopts.no_section_index = no_section_index;

  std::unique_ptr<std::ifstream> header_ifs;

  if (!header.empty()) {
    header_ifs =
        std::make_unique<std::ifstream>(header.c_str(), std::ios::binary);
    if (header_ifs->bad() || !header_ifs->is_open()) {
      std::cerr << "error: cannot open header file '" << header
                << "': " << strerror(errno) << "\n";
      return 1;
    }
  }

  LOG_PROXY(debug_logger_policy, lgr);

  folly::Function<void(const progress&, bool)> updater;

  if (options.debug_filter_function) {
    updater = [](const progress&, bool) {};
  } else {
    updater = [&](const progress& p, bool last) { lgr.update(p, last); };
  }

  progress prog(std::move(updater), interval_ms);

  block_compressor bc(compression);
  block_compressor schema_bc(schema_compression);
  block_compressor metadata_bc(metadata_compression);

  auto min_memory_req = num_workers * (UINT64_C(1) << cfg.block_size_bits);

  if (mem_limit < min_memory_req && compression != "null") {
    LOG_WARN << "low memory limit (" << size_with_unit(mem_limit) << "), need "
             << size_with_unit(min_memory_req) << " to efficiently compress "
             << size_with_unit(UINT64_C(1) << cfg.block_size_bits)
             << " blocks with " << num_workers << " threads";
  }

  std::filesystem::path output(output_str);

  std::unique_ptr<std::ostream> os;

  if (!options.debug_filter_function) {
    if (std::filesystem::exists(output) && !force_overwrite) {
      std::cerr
          << "error: output file already exists, use --force to overwrite\n";
      return 1;
    }

    auto ofs = std::make_unique<std::ofstream>(output, std::ios::binary |
                                                           std::ios::trunc);

    if (ofs->bad() || !ofs->is_open()) {
      std::cerr << "error: cannot open output file '" << output
                << "': " << ::strerror(errno) << "\n";
      return 1;
    }

    os = std::move(ofs);
  } else {
    os = std::make_unique<std::ostringstream>();
  }

  filesystem_writer fsw(*os, lgr, wg_compress, prog, bc, schema_bc, metadata_bc,
                        fswopts, header_ifs.get());

  auto ti = LOG_TIMED_INFO;

  try {
    if (recompress) {
      filesystem_v2::rewrite(lgr, prog, std::make_shared<dwarfs::mmap>(path),
                             fsw, rw_opts);
      wg_compress.wait();
    } else {
      options.inode.with_similarity =
          options.file_order.mode == file_order_mode::SIMILARITY;
      options.inode.with_nilsimsa =
          options.file_order.mode == file_order_mode::NILSIMSA;

      scanner s(lgr, wg_scanner, cfg, entry_factory::create(),
                std::make_shared<os_access_generic>(), std::move(script),
                options);

      if (input_list) {
        s.scan(fsw, path, prog, *input_list);
      } else {
        s.scan(fsw, path, prog);
      }
    }
  } catch (runtime_error const& e) {
    LOG_ERROR << e.what();
    return 1;
  } catch (system_error const& e) {
    LOG_ERROR << e.what();
    return 1;
  }

  if (!options.debug_filter_function) {
    LOG_INFO << "compression CPU time: "
             << time_with_unit(wg_compress.get_cpu_time());
  }

  if (auto ofs = dynamic_cast<std::ofstream*>(os.get())) {
    ofs->close();

    if (ofs->bad()) {
      LOG_ERROR << "failed to close output file '" << output
                << "': " << strerror(errno);
      return 1;
    }
  } else if (auto oss [[maybe_unused]] =
                 dynamic_cast<std::ostringstream*>(os.get())) {
    assert(oss->str().empty());
  } else {
    assert(false);
  }

  os.reset();

  if (!options.debug_filter_function) {
    std::ostringstream err;

    if (prog.errors) {
      err << "with " << prog.errors << " error";
      if (prog.errors > 1) {
        err << "s";
      }
    } else {
      err << "without errors";
    }

    ti << "filesystem " << (recompress ? "rewritten " : "created ")
       << err.str();
  }

  return prog.errors > 0;
}

} // namespace dwarfs

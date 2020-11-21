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
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/gen/String.h>

#if DWARFS_HAVE_LIBZSTD
#include <zstd.h>
#endif

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/console_writer.h"
#include "dwarfs/entry.h"
#include "dwarfs/filesystem.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/lua_script.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access_posix.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/util.h"

namespace po = boost::program_options;

using namespace dwarfs;

namespace {

constexpr const char* script_name = "dwarfs.lua";

const std::map<std::string, file_order_mode> order_choices{
    {"none", file_order_mode::NONE},
    {"path", file_order_mode::PATH},
#ifdef DWARFS_HAVE_LUA
    {"script", file_order_mode::SCRIPT},
#endif
    {"similarity", file_order_mode::SIMILARITY}};
} // namespace

namespace dwarfs {

void validate(boost::any& v, const std::vector<std::string>& values,
              file_order_mode*, int) {
  using namespace boost::program_options;

  validators::check_first_occurrence(v);

  auto it = order_choices.find(validators::get_single_string(values));
  if (it == order_choices.end()) {
    throw validation_error(validation_error::invalid_option_value);
  }

  v = boost::any(it->second);
}
} // namespace dwarfs

namespace {

#ifdef DWARFS_HAVE_LUA
std::string find_default_script() {
  using namespace boost::filesystem;

  path program(get_program_path());
  path dir(program.parent_path());

  std::vector<path> candidates;
  candidates.emplace_back(script_name);

  if (!dir.empty()) {
    candidates.emplace_back(dir / script_name);
    candidates.emplace_back(dir / ".." / "share" / "dwarfs" / script_name);
  }

  for (const auto& cand : candidates) {
    if (exists(cand)) {
      return canonical(cand).string();
    }
  }

  return std::string();
}
#endif

size_t get_term_width() {
  struct ::winsize w;
  ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  return w.ws_col;
}

struct level_defaults {
  unsigned block_size_bits;
  char const* compression;
  char const* window_sizes;
};

#if defined(DWARFS_HAVE_LIBLZ4)
#define ALG_LEVEL1 "lz4"
#define ALG_LEVEL2 "lz4hc:level=9"
#define ALG_LEVEL3 "lz4hc:level=9"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_LEVEL1 "zstd:level=1"
#define ALG_LEVEL2 "zstd:level=4"
#define ALG_LEVEL3 "zstd:level=7"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_LEVEL1 "lzma:level=1"
#define ALG_LEVEL2 "lzma:level=2"
#define ALG_LEVEL3 "lzma:level=3"
#else
#define ALG_LEVEL1 "null"
#define ALG_LEVEL2 "null"
#define ALG_LEVEL3 "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_LEVEL4 "zstd:level=11"
#define ALG_LEVEL5 "zstd:level=16"
#define ALG_LEVEL6 "zstd:level=20"
#define ALG_LEVEL7 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_LEVEL4 "lzma:level=4"
#define ALG_LEVEL5 "lzma:level=5"
#define ALG_LEVEL6 "lzma:level=6"
#define ALG_LEVEL7 "zstd:level=7"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_LEVEL4 "lz4hc:level=9"
#define ALG_LEVEL5 "lz4hc:level=9"
#define ALG_LEVEL6 "lz4hc:level=9"
#define ALG_LEVEL7 "lz4hc:level=9"
#else
#define ALG_LEVEL4 "null"
#define ALG_LEVEL5 "null"
#define ALG_LEVEL6 "null"
#define ALG_LEVEL7 "null"
#endif

#if defined(DWARFS_HAVE_LIBLZMA)
#define ALG_LEVEL8 "lzma:level=8:dict_size=25"
#define ALG_LEVEL9 "lzma:level=9:extreme"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_LEVEL8 "zstd:level=22"
#define ALG_LEVEL9 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_LEVEL8 "lz4hc:level=9"
#define ALG_LEVEL9 "lz4hc:level=9"
#else
#define ALG_LEVEL8 "null"
#define ALG_LEVEL9 "null"
#endif

constexpr std::array<level_defaults, 10> levels{{
    /* 0 */ {20, "null", "-"},
    /* 1 */ {20, ALG_LEVEL1, "-"},
    /* 2 */ {20, ALG_LEVEL2, "-"},
    /* 3 */ {20, ALG_LEVEL3, "13"},
    /* 4 */ {21, ALG_LEVEL4, "11"},
    /* 5 */ {22, ALG_LEVEL5, "11"},
    /* 6 */ {23, ALG_LEVEL6, "15,11"},
    /* 7 */ {24, ALG_LEVEL7, "17,15,13,11"},
    /* 8 */ {24, ALG_LEVEL8, "17,15,13,11"},
    /* 9 */ {24, ALG_LEVEL9, "17,15,13,11"},
}};

constexpr unsigned default_level = 7;

int mkdwarfs(int argc, char** argv) {
  using namespace folly::gen;

  const size_t num_cpu = std::max(std::thread::hardware_concurrency(), 1u);

  block_manager::config cfg;
  std::string path, output, window_sizes, memory_limit, script_path,
      compression, log_level;
  size_t num_workers;
  bool no_time = false, no_owner = false, recompress = false,
       no_progress = false;
  unsigned level;

  scanner_options options;

  auto order_desc =
      "file order (" + (from(order_choices) | get<0>() | unsplit(", ")) + ")";

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&path),
        "path to root directory or source filesystem")
    ("output,o",
        po::value<std::string>(&output),
        "filesystem output name")
    ("compress-level,l",
        po::value<unsigned>(&level)->default_value(default_level),
        "compression level (0=fast, 9=best)")
    ("block-size-bits,S",
        po::value<unsigned>(&cfg.block_size_bits),
        "block size bits (size = 2^bits)")
    ("num-workers,N",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of worker threads")
    ("memory-limit,L",
        po::value<std::string>(&memory_limit)->default_value("256m"),
        "block manager memory limit")
    ("compression,C",
        po::value<std::string>(&compression),
        "block compression algorithm")
    ("recompress",
        po::value<bool>(&recompress)->zero_tokens(),
        "recompress an existing filesystem")
    ("no-owner",
        po::value<bool>(&no_owner)->zero_tokens(),
        "don't save user/group (implies --no-time)")
    ("no-time",
        po::value<bool>(&no_time)->zero_tokens(),
        "don't save timestamps")
    ("order",
        po::value<file_order_mode>(&options.file_order)
            ->default_value(file_order_mode::SIMILARITY, "similarity"),
        order_desc.c_str())
#ifdef DWARFS_HAVE_LUA
    ("script",
        po::value<std::string>(&script_path)
            ->default_value(find_default_script()),
        "Lua script for file acceptance/ordering")
#endif
    ("blockhash-window-sizes",
        po::value<std::string>(&window_sizes),
        "window sizes for block hashing")
    ("window-increment-shift",
        po::value<unsigned>(&cfg.window_increment_shift)
            ->default_value(1),
        "window increment (as right shift of size)")
    ("log-level",
        po::value<std::string>(&log_level)->default_value("info"),
        "log level (error, warn, info, debug, trace)")
    ("no-progress",
        po::value<bool>(&no_progress)->zero_tokens(),
        "don't show progress")
    ("help,h",
        "output help message and exit");
  // clang-format on

  po::variables_map vm;

  po::store(po::parse_command_line(argc, argv, opts), vm);
  po::notify(vm);

  if (vm.count("help") or !vm.count("input") or !vm.count("output")) {
    std::cout << "mkdwarfs (" << DWARFS_VERSION << ")\n" << opts << std::endl;
    std::cout
        << "Compression level defaults:\n"
           "  "
           "------------------------------------------------------------------"
           "\n"
           "  Level   Block Size   Algorithm                        Window "
           "Sizes\n"
           "  "
           "------------------------------------------------------------------"
        << std::endl;

    int level = 0;
    for (auto const& l : levels) {
      std::cout << folly::format("  {:1d}       {:2d}           {:32s} {:10s}",
                                 level, l.block_size_bits, l.compression,
                                 l.window_sizes)
                << std::endl;
      ++level;
    }

    std::cout
        << "  "
           "------------------------------------------------------------------"
        << std::endl;

    std::cout << "\nCompression algorithms:\n"
                 "  none     no compression at all\n"
#ifdef DWARFS_HAVE_LIBLZ4
                 "  lz4      LZ4 compression\n"
                 "               level=[0..9]\n"
                 "  lz4hc    LZ4 HC compression\n"
                 "               level=[0..9]\n"
#endif
#ifdef DWARFS_HAVE_LIBZSTD
                 "  zstd     ZSTD compression\n"
                 "               level=["
              << ZSTD_minCLevel() << ".." << ZSTD_maxCLevel()
              << "]\n"
#endif
#ifdef DWARFS_HAVE_LIBLZMA
                 "  lzma     LZMA compression\n"
                 "               level=[0..9]\n"
                 "               dict_size=[12..30]\n"
                 "               extreme\n"
                 "               binary={x86,powerpc,ia64,arm,armthumb,sparc}\n"
#endif
              << std::endl;

    return 0;
  }

  if (level >= levels.size()) {
    throw std::runtime_error("invalid compression level");
  }

  auto const& defaults = levels[level];

  if (!vm.count("block-size-bits")) {
    cfg.block_size_bits = defaults.block_size_bits;
  }

  if (!vm.count("compression")) {
    compression = defaults.compression;
  }

  if (!vm.count("blockhash-window-sizes")) {
    window_sizes = defaults.window_sizes;
  }

  size_t mem_limit = parse_size_with_unit(memory_limit);

  std::vector<std::string> wsv;

  if (window_sizes != "-") {
    boost::split(wsv, window_sizes, boost::is_any_of(","));

    std::transform(wsv.begin(), wsv.end(),
                   std::back_inserter(cfg.blockhash_window_size),
                   [](const std::string& x) {
                     return static_cast<size_t>(1) << folly::to<unsigned>(x);
                   });
  }

  worker_group wg("writer", num_workers);

  console_writer lgr(std::cerr, !no_progress && ::isatty(::fileno(stderr)),
                     get_term_width(), logger::parse_level(log_level));

  std::shared_ptr<script> script;

#ifdef DWARFS_HAVE_LUA
  if (!script_path.empty()) {
    script = std::make_shared<lua_script>(lgr, script_path);
  }
#endif

  if (options.file_order == file_order_mode::SCRIPT && !script) {
    throw std::runtime_error(
        "--order=script can only be used with a valid --script option");
  }

  log_proxy<debug_logger_policy> log(lgr);

  progress prog([&](const progress& p, bool last) { lgr.update(p, last); });

  block_compressor bc(compression);
  std::ofstream ofs(output);
  filesystem_writer fsw(ofs, lgr, wg, prog, bc, mem_limit);

  if (recompress) {
    auto ti = log.timed_info();
    filesystem::rewrite(lgr, prog, std::make_shared<dwarfs::mmap>(path), fsw);
    wg.wait();
    ti << "filesystem rewritten";
  } else {
    scanner s(lgr, wg, cfg,
              entry_factory::create(no_owner, no_owner || no_time,
                                    options.file_order ==
                                        file_order_mode::SIMILARITY),
              std::make_shared<os_access_posix>(), script, options);

    {
      auto ti = log.timed_info();

      s.scan(fsw, path, prog);

      std::ostringstream err;

      if (prog.errors) {
        err << "with " << prog.errors << " error";
        if (prog.errors > 1) {
          err << "s";
        }
      } else {
        err << "without errors";
      }

      ti << "filesystem created " << err.str();
    }
  }

  return prog.errors > 0;
}
} // namespace

int main(int argc, char** argv) {
  try {
    return mkdwarfs(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << folly::exceptionStr(e) << std::endl;
    return 1;
  }
}

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
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/experimental/symbolizer/SignalHandler.h>
#include <folly/gen/String.h>

#include <fmt/format.h>

#ifdef DWARFS_HAVE_LIBZSTD
#include <zstd.h>
#endif

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/console_writer.h"
#include "dwarfs/entry.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/options_interface.h"
#include "dwarfs/os_access_posix.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "dwarfs/util.h"

#ifdef DWARFS_HAVE_PYTHON
#include "dwarfs/python_script.h"
#endif

namespace po = boost::program_options;

using namespace dwarfs;

namespace {

#ifdef DWARFS_HAVE_LIBZSTD
#if ZSTD_VERSION_MAJOR > 1 ||                                                  \
    (ZSTD_VERSION_MAJOR == 1 && ZSTD_VERSION_MINOR >= 4)
#define ZSTD_MIN_LEVEL ZSTD_minCLevel()
#else
#define ZSTD_MIN_LEVEL 1
#endif
#endif

const std::map<std::string, file_order_mode> order_choices{
    {"none", file_order_mode::NONE},
    {"path", file_order_mode::PATH},
#ifdef DWARFS_HAVE_PYTHON
    {"script", file_order_mode::SCRIPT},
#endif
    {"similarity", file_order_mode::SIMILARITY},
    {"nilsimsa", file_order_mode::NILSIMSA}};

const std::map<std::string, uint32_t> time_resolutions{
    {"sec", 1},
    {"min", 60},
    {"hour", 3600},
    {"day", 86400},
};

} // namespace

namespace dwarfs {

class script_options : public options_interface {
 public:
  script_options(logger& lgr, po::variables_map& vm, scanner_options& opts,
                 bool& force_similarity)
      : log_(lgr)
      , vm_(vm)
      , opts_(opts)
      , force_similarity_(force_similarity) {}

  void set_order(file_order_mode order_mode, set_mode mode = DEFAULT) override {
    set(opts_.file_order.mode, order_mode, "order", mode);
  }

  void
  set_remove_empty_dirs(bool remove_empty, set_mode mode = DEFAULT) override {
    set(opts_.remove_empty_dirs, remove_empty, "remove-empty-dirs", mode);
  }

  void enable_similarity() override {
    log_.debug() << "script is forcing similarity hash computation";
    force_similarity_ = true;
  }

 private:
  template <typename T>
  void set(T& target, T const& value, std::string const& name, set_mode mode) {
    switch (mode) {
    case options_interface::DEFAULT:
      if (!vm_.count(name) || vm_[name].defaulted()) {
        log_.info() << "script is setting " << name << "=" << value;
        target = value;
      }
      break;

    case options_interface::OVERRIDE:
      if (vm_.count(name) && !vm_[name].defaulted()) {
        log_.warn() << "script is overriding " << name << "=" << value;
      } else {
        log_.info() << "script is setting " << name << "=" << value;
      }
      target = value;
      break;
    }
  }

  log_proxy<debug_logger_policy> log_;
  po::variables_map& vm_;
  scanner_options& opts_;
  bool& force_similarity_;
};

} // namespace dwarfs

namespace {

size_t get_term_width() {
  struct ::winsize w;
  ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  return w.ws_col;
}

struct level_defaults {
  unsigned block_size_bits;
  char const* data_compression;
  char const* schema_compression;
  char const* metadata_compression;
  char const* window_sizes;
  char const* order;
};

#if defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_LEVEL1 "lz4"
#define ALG_DATA_LEVEL2 "lz4hc:level=9"
#define ALG_DATA_LEVEL3 "lz4hc:level=9"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_LEVEL1 "zstd:level=1"
#define ALG_DATA_LEVEL2 "zstd:level=4"
#define ALG_DATA_LEVEL3 "zstd:level=7"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_LEVEL1 "lzma:level=1"
#define ALG_DATA_LEVEL2 "lzma:level=2"
#define ALG_DATA_LEVEL3 "lzma:level=3"
#else
#define ALG_DATA_LEVEL1 "null"
#define ALG_DATA_LEVEL2 "null"
#define ALG_DATA_LEVEL3 "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_LEVEL4 "zstd:level=11"
#define ALG_DATA_LEVEL5 "zstd:level=16"
#define ALG_DATA_LEVEL6 "zstd:level=20"
#define ALG_DATA_LEVEL7 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_LEVEL4 "lzma:level=4"
#define ALG_DATA_LEVEL5 "lzma:level=5"
#define ALG_DATA_LEVEL6 "lzma:level=6"
#define ALG_DATA_LEVEL7 "zstd:level=7"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_LEVEL4 "lz4hc:level=9"
#define ALG_DATA_LEVEL5 "lz4hc:level=9"
#define ALG_DATA_LEVEL6 "lz4hc:level=9"
#define ALG_DATA_LEVEL7 "lz4hc:level=9"
#else
#define ALG_DATA_LEVEL4 "null"
#define ALG_DATA_LEVEL5 "null"
#define ALG_DATA_LEVEL6 "null"
#define ALG_DATA_LEVEL7 "null"
#endif

#if defined(DWARFS_HAVE_LIBLZMA)
#define ALG_DATA_LEVEL8 "lzma:level=8:dict_size=25"
#define ALG_DATA_LEVEL9 "lzma:level=9:extreme"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_DATA_LEVEL8 "zstd:level=22"
#define ALG_DATA_LEVEL9 "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_DATA_LEVEL8 "lz4hc:level=9"
#define ALG_DATA_LEVEL9 "lz4hc:level=9"
#else
#define ALG_DATA_LEVEL8 "null"
#define ALG_DATA_LEVEL9 "null"
#endif

#if defined(DWARFS_HAVE_LIBZSTD)
#define ALG_SCHEMA "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZMA)
#define ALG_SCHEMA "lzma:level=9"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_SCHEMA "lz4hc:level=9"
#else
#define ALG_SCHEMA "null"
#endif

#if defined(DWARFS_HAVE_LIBLZMA)
#define ALG_METADATA "lzma:level=9:extreme"
#elif defined(DWARFS_HAVE_LIBZSTD)
#define ALG_METADATA "zstd:level=22"
#elif defined(DWARFS_HAVE_LIBLZ4)
#define ALG_METADATA "lz4hc:level=9"
#else
#define ALG_METADATA "null"
#endif

constexpr std::array<level_defaults, 10> levels{{
    // clang-format off
    /* 0 */ {20, "null",          "null"    , "null",       "-",           "none"},
    /* 1 */ {20, ALG_DATA_LEVEL1, ALG_SCHEMA, "null",       "-",           "path"},
    /* 2 */ {20, ALG_DATA_LEVEL2, ALG_SCHEMA, "null",       "-",           "path"},
    /* 3 */ {20, ALG_DATA_LEVEL3, ALG_SCHEMA, "null",       "13",          "similarity"},
    /* 4 */ {21, ALG_DATA_LEVEL4, ALG_SCHEMA, "null",       "11",          "similarity"},
    /* 5 */ {22, ALG_DATA_LEVEL5, ALG_SCHEMA, "null",       "11",          "similarity"},
    /* 6 */ {23, ALG_DATA_LEVEL6, ALG_SCHEMA, "null",       "15,11",       "nilsimsa:250:10000"},
    /* 7 */ {24, ALG_DATA_LEVEL7, ALG_SCHEMA, "null",       "17,15,13,11", "nilsimsa"},
    /* 8 */ {24, ALG_DATA_LEVEL8, ALG_SCHEMA, ALG_METADATA, "17,15,13,11", "nilsimsa"},
    /* 9 */ {24, ALG_DATA_LEVEL9, ALG_SCHEMA, ALG_METADATA, "17,15,13,11", "nilsimsa"},
    // clang-format on
}};

constexpr unsigned default_level = 7;

int mkdwarfs(int argc, char** argv) {
  using namespace folly::gen;

  const size_t num_cpu = std::max(std::thread::hardware_concurrency(), 1u);

  block_manager::config cfg;
  std::string path, output, window_sizes, memory_limit, script_arg, compression,
      schema_compression, metadata_compression, log_level, timestamp,
      time_resolution, order;
  size_t num_workers, max_scanner_workers;
  bool recompress = false, no_progress = false;
  unsigned level;
  uint16_t uid, gid;

  scanner_options options;

  auto order_desc =
      "inode order (" + (from(order_choices) | get<0>() | unsplit(", ")) + ")";

  auto resolution_desc = "time resolution in seconds or (" +
                         (from(time_resolutions) | get<0>() | unsplit(", ")) +
                         ")";

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
        "number of writer worker threads")
    ("max-scanner-workers,M",
        po::value<size_t>(&max_scanner_workers)->default_value(num_cpu),
        "number of scanner worker threads")
    ("memory-limit,L",
        po::value<std::string>(&memory_limit)->default_value("1g"),
        "block manager memory limit")
    ("compression,C",
        po::value<std::string>(&compression),
        "block compression algorithm")
    ("schema-compression",
        po::value<std::string>(&schema_compression),
        "metadata schema compression algorithm")
    ("metadata-compression",
        po::value<std::string>(&metadata_compression),
        "metadata compression algorithm")
    ("recompress",
        po::value<bool>(&recompress)->zero_tokens(),
        "recompress an existing filesystem")
    ("set-owner",
        po::value<uint16_t>(&uid),
        "set owner (uid) for whole file system")
    ("set-group",
        po::value<uint16_t>(&gid),
        "set group (gid) for whole file system")
    ("set-time",
        po::value<std::string>(&timestamp),
        "set timestamp for whole file system (unixtime or 'now')")
    ("keep-all-times",
        po::value<bool>(&options.keep_all_times)->zero_tokens(),
        "save atime and ctime in addition to mtime")
    ("time-resolution",
        po::value<std::string>(&time_resolution)->default_value("sec"),
        resolution_desc.c_str())
    ("order",
        po::value<std::string>(&order),
        order_desc.c_str())
#ifdef DWARFS_HAVE_PYTHON
    ("script",
        po::value<std::string>(&script_arg),
        "Python script for customization")
#endif
    ("blockhash-window-sizes",
        po::value<std::string>(&window_sizes),
        "window sizes for block hashing")
    ("window-increment-shift",
        po::value<unsigned>(&cfg.window_increment_shift)
            ->default_value(1),
        "window increment (as right shift of size)")
    ("remove-empty-dirs",
        po::value<bool>(&options.remove_empty_dirs)->zero_tokens(),
        "remove empty directories in file system")
    ("with-devices",
        po::value<bool>(&options.with_devices)->zero_tokens(),
        "include block and character devices")
    ("with-specials",
        po::value<bool>(&options.with_specials)->zero_tokens(),
        "include named fifo and sockets")
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
  auto parsed = po::parse_command_line(argc, argv, opts);

  po::store(parsed, vm);
  po::notify(vm);

  auto unrecognized =
      po::collect_unrecognized(parsed.options, po::include_positional);

  if (!unrecognized.empty()) {
    throw std::runtime_error("unrecognized argument(s): " +
                             boost::join(unrecognized, " "));
  }

  if (vm.count("help") or !vm.count("input") or !vm.count("output")) {
    size_t l_dc = 0, l_sc = 0, l_mc = 0, l_ws = 0, l_or = 0;
    for (auto const& l : levels) {
      l_dc = std::max(l_dc, ::strlen(l.data_compression));
      l_sc = std::max(l_sc, ::strlen(l.schema_compression));
      l_mc = std::max(l_mc, ::strlen(l.metadata_compression));
      l_ws = std::max(l_ws, ::strlen(l.window_sizes));
      l_or = std::max(l_or, ::strlen(l.order));
    }

    std::string sep(22 + l_dc + l_sc + l_mc + l_ws + l_or, '-');

    std::cout << "mkdwarfs (" << DWARFS_VERSION << ")\n" << opts << std::endl;
    std::cout << "Compression level defaults:\n"
              << "  " << sep << "\n"
              << fmt::format("  Level  Block  {:{}s}  {:{}s}  Inode Order\n",
                             "Compression Algorithm", 4 + l_dc + l_sc + l_mc,
                             "Window", l_ws)
              << fmt::format("         Size   {:{}s}  {:{}s}  {:{}s}  {:{}s}\n",
                             "Block Data", l_dc, "Schema", l_sc, "Metadata",
                             l_mc, "Sizes", l_ws)
              << "  " << sep << std::endl;

    int level = 0;
    for (auto const& l : levels) {
      std::cout << fmt::format("  {:1d}      {:2d}     {:{}s}  {:{}s}  {:{}s}  "
                               "{:{}s}  {:{}s}",
                               level, l.block_size_bits, l.data_compression,
                               l_dc, l.schema_compression, l_sc,
                               l.metadata_compression, l_mc, l.window_sizes,
                               l_ws, l.order, l_or)
                << std::endl;
      ++level;
    }

    std::cout << "  " << sep << std::endl;

    std::cout << "\nCompression algorithms:\n"
                 "  null     no compression at all\n"
#ifdef DWARFS_HAVE_LIBLZ4
                 "  lz4      LZ4 compression\n"
                 "               level=[0..9]\n"
                 "  lz4hc    LZ4 HC compression\n"
                 "               level=[0..9]\n"
#endif
#ifdef DWARFS_HAVE_LIBZSTD
                 "  zstd     ZSTD compression\n"
                 "               level=["
              << ZSTD_MIN_LEVEL << ".." << ZSTD_maxCLevel()
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
    compression = defaults.data_compression;
  }

  if (!vm.count("schema-compression")) {
    schema_compression = defaults.schema_compression;
  }

  if (!vm.count("metadata-compression")) {
    metadata_compression = defaults.metadata_compression;
  }

  if (!vm.count("blockhash-window-sizes")) {
    window_sizes = defaults.window_sizes;
  }

  if (!vm.count("order")) {
    order = defaults.order;
  }

  std::vector<std::string> order_opts;
  boost::split(order_opts, order, boost::is_any_of(":"));
  if (auto it = order_choices.find(order_opts.front());
      it != order_choices.end()) {
    options.file_order.mode = it->second;
    if (order_opts.size() > 1) {
      if (options.file_order.mode != file_order_mode::NILSIMSA) {
        throw std::runtime_error(
            fmt::format("inode order mode '{}' does not support options",
                        order_opts.front()));
      }
      if (order_opts.size() > 3) {
        throw std::runtime_error(fmt::format(
            "too many options for inode order mode '{}'", order_opts.front()));
      }
      options.file_order.nilsimsa_limit = folly::to<int>(order_opts[1]);
      if (options.file_order.nilsimsa_limit < 0 ||
          options.file_order.nilsimsa_limit > 255) {
        throw std::runtime_error(
            fmt::format("limit ({}) out of range for '{}' (0..255)",
                        options.file_order.nilsimsa_limit, order_opts.front()));
      }
      if (order_opts.size() > 2) {
        options.file_order.nilsimsa_depth = folly::to<int>(order_opts[2]);
        if (options.file_order.nilsimsa_depth < 0) {
          throw std::runtime_error(fmt::format(
              "depth ({}) cannot be negative for '{}'", order_opts.front()));
        }
      }
    }
  } else {
    throw std::runtime_error("invalid inode order mode: " + order);
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

  worker_group wg_writer("writer", num_workers);
  worker_group wg_scanner(worker_group::load_adaptive, "scanner",
                          max_scanner_workers);

  console_writer lgr(std::cerr, !no_progress && ::isatty(::fileno(stderr)),
                     get_term_width(), logger::parse_level(log_level),
                     recompress ? console_writer::REWRITE
                                : console_writer::NORMAL);

  std::shared_ptr<script> script;

#ifdef DWARFS_HAVE_PYTHON
  if (!script_arg.empty()) {
    std::string file, ctor;
    if (auto pos = script_arg.find(':'); pos != std::string::npos) {
      file = script_arg.substr(0, pos);
      ctor = script_arg.substr(pos + 1);
      if (ctor.find('(') == std::string::npos) {
        ctor += "()";
      }
    } else {
      file = script_arg;
      ctor = "mkdwarfs()";
    }
    std::string code;
    if (folly::readFile(file.c_str(), code)) {
      script = std::make_shared<python_script>(lgr, code, ctor);
    } else {
      throw std::runtime_error("could not load script: " + file);
    }
  }
#endif

  bool force_similarity = false;

  if (script && script->has_configure()) {
    script_options script_opts(lgr, vm, options, force_similarity);
    script->configure(script_opts);
  }

  if (options.file_order.mode == file_order_mode::SCRIPT && !script) {
    throw std::runtime_error(
        "--order=script can only be used with a valid --script option");
  }

  if (vm.count("set-owner")) {
    options.uid = uid;
  }

  if (vm.count("set-group")) {
    options.gid = gid;
  }

  if (vm.count("set-time")) {
    options.timestamp = timestamp == "now" ? std::time(nullptr)
                                           : folly::to<uint64_t>(timestamp);
  }

  if (auto it = time_resolutions.find(time_resolution);
      it != time_resolutions.end()) {
    options.time_resolution_sec = it->second;
  } else {
    options.time_resolution_sec = folly::to<uint32_t>(time_resolution);
    if (options.time_resolution_sec == 0) {
      throw std::runtime_error("timestamp resolution cannot be 0");
    }
  }

  log_proxy<debug_logger_policy> log(lgr);

  progress prog([&](const progress& p, bool last) { lgr.update(p, last); });

  block_compressor bc(compression);
  block_compressor schema_bc(schema_compression);
  block_compressor metadata_bc(metadata_compression);
  std::ofstream ofs(output);
  filesystem_writer fsw(ofs, lgr, wg_writer, prog, bc, schema_bc, metadata_bc,
                        mem_limit);

  if (recompress) {
    auto ti = log.timed_info();
    filesystem_v2::rewrite(lgr, prog, std::make_shared<dwarfs::mmap>(path),
                           fsw);
    wg_writer.wait();
    ti << "filesystem rewritten";
  } else {
    options.inode.with_similarity =
        force_similarity ||
        options.file_order.mode == file_order_mode::SIMILARITY;
    options.inode.with_nilsimsa =
        options.file_order.mode == file_order_mode::NILSIMSA;

    scanner s(lgr, wg_scanner, cfg, entry_factory::create(),
              std::make_shared<os_access_posix>(), std::move(script), options);

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
    folly::symbolizer::installFatalSignalHandler();
    return mkdwarfs(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << folly::exceptionStr(e) << std::endl;
    return 1;
  }
}

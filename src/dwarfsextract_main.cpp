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

#include <exception>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include <archive.h>

#include <folly/String.h>

#include "dwarfs/filesystem_extractor.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/iolayer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/performance_monitor.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

namespace po = boost::program_options;

namespace dwarfs {

int dwarfsextract_main(int argc, sys_char** argv, iolayer const& iol) {
  std::string filesystem, output, format, cache_size_str, image_offset;
  logger_options logopts;
#if DWARFS_PERFMON_ENABLED
  std::string perfmon_str;
#endif
  size_t num_workers;
  bool continue_on_error{false}, disable_integrity_check{false},
      stdout_progress{false};

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&filesystem),
        "input filesystem file")
    ("output,o",
        po::value<std::string>(&output),
        "output file or directory")
    ("image-offset,O",
        po::value<std::string>(&image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
    ("format,f",
        po::value<std::string>(&format),
        "output format")
    ("continue-on-error",
        po::value<bool>(&continue_on_error)->zero_tokens(),
        "continue if errors are encountered")
    ("disable-integrity-check",
        po::value<bool>(&disable_integrity_check)->zero_tokens(),
        "disable file system image block integrity check (dangerous)")
    ("stdout-progress",
        po::value<bool>(&stdout_progress)->zero_tokens(),
        "write percentage progress to stdout")
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(4),
        "number of worker threads")
    ("cache-size,s",
        po::value<std::string>(&cache_size_str)->default_value("512m"),
        "block cache size")
#if DWARFS_PERFMON_ENABLED
    ("perfmon",
        po::value<std::string>(&perfmon_str),
        "enable performance monitor")
#endif
    ;
  // clang-format on

  add_common_options(opts, logopts);

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, opts), vm);
    po::notify(vm);
  } catch (po::error const& e) {
    iol.err << "error: " << e.what() << "\n";
    return 1;
  }

#ifdef DWARFS_BUILTIN_MANPAGE
  if (vm.count("man")) {
    show_manpage(manpage::get_dwarfsextract_manpage(), iol);
    return 0;
  }
#endif

  auto constexpr usage = "Usage: dwarfsextract [OPTIONS...]\n";

  if (vm.count("help") or !vm.count("input")) {
    iol.out << tool_header("dwarfsextract") << "using "
            << ::archive_version_string() << "\n\n"
            << usage << "\n"
            << opts << "\n";
    return 0;
  }

  int rv = 0;

  try {
    stream_logger lgr(iol.term, iol.err, logopts);
    filesystem_options fsopts;

    fsopts.image_offset = parse_image_offset(image_offset);
    fsopts.block_cache.max_bytes = parse_size_with_unit(cache_size_str);
    fsopts.block_cache.num_workers = num_workers;
    fsopts.block_cache.disable_block_integrity_check = disable_integrity_check;
    fsopts.metadata.enable_nlink = true;

    std::unordered_set<std::string> perfmon_enabled;
#if DWARFS_PERFMON_ENABLED
    if (!perfmon_str.empty()) {
      folly::splitTo<std::string>(
          ',', perfmon_str,
          std::inserter(perfmon_enabled, perfmon_enabled.begin()));
    }
#endif
    std::shared_ptr<performance_monitor> perfmon =
        performance_monitor::create(perfmon_enabled);

    filesystem_v2 fs(lgr, *iol.os, iol.os->map_file(filesystem), fsopts,
                     perfmon);
    filesystem_extractor fsx(lgr, *iol.os);

    if (format.empty()) {
      fsx.open_disk(output);
    } else {
      std::ostream* stream{nullptr};

      if (output.empty() or output == "-") {
        if (stdout_progress) {
          DWARFS_THROW(runtime_error,
                       "cannot use --stdout-progress with --output=-");
        }

        if (&iol.out == &std::cout) {
          output.clear();
        } else {
          stream = &iol.out;
        }
      }

      if (stream) {
        fsx.open_stream(*stream, format);
      } else {
        fsx.open_archive(output, format);
      }
    }

    filesystem_extractor_options fsx_opts;

    fsx_opts.max_queued_bytes = fsopts.block_cache.max_bytes;
    fsx_opts.continue_on_error = continue_on_error;
    int prog{-1};
    if (stdout_progress) {
      fsx_opts.progress = [&prog, &iol](std::string_view, uint64_t extracted,
                                        uint64_t total) {
        int p = 100 * extracted / total;
        if (p > prog) {
          prog = p;
          iol.out << "\r" << prog << "%";
          iol.out.flush();
        }
        if (extracted == total) {
          iol.out << "\n";
        }
      };
    }

    rv = fsx.extract(fs, fsx_opts) ? 0 : 2;

    fsx.close();

    if (perfmon) {
      perfmon->summarize(iol.err);
    }
  } catch (std::exception const& e) {
    iol.err << folly::exceptionStr(e) << "\n";
    return 1;
  }

  return rv;
}

int dwarfsextract_main(int argc, sys_char** argv) {
  return dwarfsextract_main(argc, argv, iolayer::system_default());
}

int dwarfsextract_main(std::span<std::string> args, iolayer const& iol) {
  return call_sys_main_iolayer(args, iol, dwarfsextract_main);
}

int dwarfsextract_main(std::span<std::string_view> args, iolayer const& iol) {
  return call_sys_main_iolayer(args, iol, dwarfsextract_main);
}

} // namespace dwarfs

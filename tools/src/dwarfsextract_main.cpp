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

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include <boost/program_options.hpp>

#include <dwarfs/config.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/glob_matcher.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/string.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/program_options_helpers.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/filesystem_extractor.h>
#include <dwarfs/utility/filesystem_extractor_archive_format.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

namespace po = boost::program_options;

namespace dwarfs::tool {

namespace {

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
#ifdef _WIN32
constexpr std::wstring_view kDash{L"-"};
#else
constexpr std::string_view kDash{"-"};
#endif
#endif

} // namespace

int dwarfsextract_main(int argc, sys_char** argv, iolayer const& iol) {
  sys_string fs_image, output, trace_file;
  std::string cache_size_str, image_offset;
  logger_options logopts;
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  utility::filesystem_extractor_archive_format format;
  std::string format_filters;
#endif
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
        po_sys_value<sys_string>(&fs_image),
        "input filesystem file")
    ("output,o",
        po_sys_value<sys_string>(&output),
        "output file or directory")
    ("pattern",
        po::value<std::vector<std::string>>(),
        "only extract files matching these patterns")
    ("image-offset,O",
        po::value<std::string>(&image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    ("format,f",
        po::value<std::string>(&format.name),
        "output format")
    ("format-filters",
        po::value<std::string>(&format_filters),
        "comma-separated libarchive format filters")
    ("format-options",
        po::value<std::string>(&format.options),
        "options for the specific libarchive format/filters")
#endif
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
    ("perfmon-trace",
        po_sys_value<sys_string>(&trace_file),
        "write performance monitor trace file")
#endif
    ;
  // clang-format on

  tool::add_common_options(opts, logopts);

  po::positional_options_description pos;
  pos.add("pattern", -1);

  po::variables_map vm;

  try {
    po::store(po::basic_command_line_parser<sys_char>(argc, argv)
                  .options(opts)
                  .positional(pos)
                  .run(),
              vm);
    po::notify(vm);
  } catch (po::error const& e) {
    iol.err << "error: " << e.what() << "\n";
    return 1;
  }

#ifdef DWARFS_BUILTIN_MANPAGE
  if (vm.contains("man")) {
    tool::show_manpage(tool::manpage::get_dwarfsextract_manpage(), iol);
    return 0;
  }
#endif

  auto constexpr usage = "Usage: dwarfsextract [OPTIONS...]\n";

  if (vm.contains("help") or !vm.contains("input")) {
    auto extra_deps = [](library_dependencies& deps) {
      utility::filesystem_extractor::add_library_dependencies(deps);
      decompressor_registry::instance().add_library_dependencies(deps);
    };

    iol.out << tool::tool_header("dwarfsextract", extra_deps) << usage << "\n"
            << opts << "\n";
    return 0;
  }

  std::unique_ptr<glob_matcher> matcher;

  if (vm.contains("pattern")) {
    matcher = std::make_unique<glob_matcher>(
        vm["pattern"].as<std::vector<std::string>>());
  }

  int rv = 0;

  try {
    stream_logger lgr(iol.term, iol.err, logopts);
    reader::filesystem_options fsopts;

    fsopts.image_offset = reader::parse_image_offset(image_offset);
    fsopts.block_cache.max_bytes = parse_size_with_unit(cache_size_str);
    fsopts.block_cache.num_workers = num_workers;
    fsopts.block_cache.disable_block_integrity_check = disable_integrity_check;

    std::shared_ptr<performance_monitor> perfmon;

#if DWARFS_PERFMON_ENABLED
    std::unordered_set<std::string> perfmon_enabled;
    std::optional<std::filesystem::path> perfmon_trace_file;

    if (!perfmon_str.empty()) {
      split_to(perfmon_str, ',', perfmon_enabled);
    }

    if (!trace_file.empty()) {
      perfmon_trace_file = iol.os->canonical(trace_file);
    }

    perfmon = performance_monitor::create(perfmon_enabled, iol.file,
                                          perfmon_trace_file);
#endif

    reader::filesystem_v2_lite fs(lgr, *iol.os, fs_image, fsopts, perfmon);
    utility::filesystem_extractor fsx(lgr, *iol.os, iol.file);

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    if (format.name.empty()) {
#endif
      fsx.open_disk(iol.os->canonical(output));
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    } else {
      std::ostream* stream{nullptr};

      if (output.empty() or output == kDash) {
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

      split_to(format_filters, ',', format.filters);

      if (stream) {
        fsx.open_stream(*stream, format);
      } else {
        fsx.open_archive(iol.os->canonical(output), format);
      }
    }
#endif

    utility::filesystem_extractor_options fsx_opts;

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

    rv = fsx.extract(fs, matcher.get(), fsx_opts) ? 0 : 2;

    fsx.close();

    if (perfmon) {
      perfmon->summarize(iol.err);
    }
  } catch (std::exception const& e) {
    iol.err << exception_str(e) << "\n";
    return 1;
  }

  return rv;
}

} // namespace dwarfs::tool

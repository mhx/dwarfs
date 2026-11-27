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

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/command_line.h>
#include <dwarfs/config.h>
#include <dwarfs/decompressor_factory.h>
#include <dwarfs/glob_matcher.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/string.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/filesystem_extractor.h>
#include <dwarfs/utility/filesystem_extractor_archive_format.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

using namespace std::chrono_literals;

namespace dwarfs::tool {

namespace {

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
#ifdef _WIN32
constexpr std::wstring_view kDash{L"-"};
#else
constexpr std::string_view kDash{"-"};
#endif
#endif

class progress_thread {
 public:
  using fn_type = std::function<void(bool last)>;

  progress_thread(std::chrono::nanoseconds interval, fn_type const& fn)
      : thread_([this, interval, fn] { this->run(interval, fn); }) {}

  ~progress_thread() { stop(); }

  void stop() {
    try {
      {
        std::lock_guard lock(running_mx_);
        if (!running_) {
          return;
        }
        running_ = false;
      }
      cond_.notify_all();
      thread_.join();
    } catch (...) {
      DWARFS_PANIC("exception thrown in writer_progress destructor: " +
                   exception_str(std::current_exception()));
    }
  }

 private:
  void run(std::chrono::nanoseconds interval, fn_type const& fn) {
    std::unique_lock lock(running_mx_);
    do {
      fn(false);
      cond_.wait_for(lock, interval);
    } while (running_);
    fn(true);
  }

  mutable std::mutex running_mx_;
  bool running_{true};
  std::condition_variable cond_;
  std::thread thread_;
};

} // namespace

int dwarfsextract_main(int argc, sys_char** argv, iolayer const& iol) {
  std::optional<sys_string> fs_image;
  sys_string output;
  std::vector<std::string> patterns;
  std::string cache_size_str, image_offset;
  logger_options logopts;
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  utility::filesystem_extractor_archive_format format;
  std::string format_filters;
#endif
#if DWARFS_PERFMON_ENABLED
  std::string perfmon_str;
  sys_string trace_file;
#endif
  size_t num_workers, num_disk_writers;
  bool continue_on_error{false}, disable_integrity_check{false},
      stdout_progress{false}, skip_devices{false}, skip_specials{false},
      no_hardlinks{false};

  cli::options opts;
  common_cli_options common;
  opts.add("input,i", fs_image, "input filesystem file");
  opts.add("output,o", output, "output file or directory");
  opts.add_repeated("pattern", patterns,
                    "only extract files matching these patterns");
  opts.positional("pattern");
  opts.flag("skip-devices", skip_devices, "do not extract device files");
  opts.flag("skip-specials", skip_specials,
            "do not extract special files (sockets, fifos, etc.)");
  opts.flag("no-hardlinks", no_hardlinks,
            "do not create hardlinks, extract each file separately");
  opts.add("image-offset,O", image_offset, "filesystem image offset in bytes",
           "auto");
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  opts.add("format,f", format.name, "output format");
  opts.add("format-filters", format_filters,
           "comma-separated libarchive format filters");
  opts.add("format-options", format.options,
           "options for the specific libarchive format/filters");
#endif
  opts.flag("continue-on-error", continue_on_error,
            "continue if errors are encountered");
  opts.flag("disable-integrity-check", disable_integrity_check,
            "disable file system image block integrity check (dangerous)");
  opts.flag("stdout-progress", stdout_progress,
            "write percentage progress to stdout");
  opts.add("num-workers,n", num_workers, "number of worker threads", 4);
  opts.add("num-disk-writers", num_disk_writers, "number of disk data writers",
           0);
  opts.add("cache-size,s", cache_size_str, "block cache size", "512m");
#if DWARFS_PERFMON_ENABLED
  opts.add("perfmon", perfmon_str, "enable performance monitor");
  opts.add("perfmon-trace", trace_file, "write performance monitor trace file");
#endif
  tool::add_common_options(opts, logopts, common);

  try {
    opts.parse(argc, argv);
  } catch (cli::error const& e) {
    iol.err << "error: " << e.what() << "\n";
    return 1;
  }

#ifdef DWARFS_BUILTIN_MANPAGE
  if (common.man) {
    tool::show_manpage(tool::manpage::get_dwarfsextract_manpage(), iol);
    return 0;
  }
#endif

  constexpr auto usage = "Usage: dwarfsextract [OPTIONS...]\n";

  if (common.help or !fs_image) {
    auto extra_deps = [](library_dependencies& deps) {
      utility::filesystem_extractor::add_library_dependencies(deps);
      add_decompressor_library_dependencies(deps);
    };

    iol.out << tool::tool_header("dwarfsextract", extra_deps) << usage << "\n";
    opts.help(iol.out);
    iol.out << "\n";
    return 0;
  }

  std::unique_ptr<glob_matcher> matcher;

  if (!patterns.empty()) {
    matcher = std::make_unique<glob_matcher>(patterns);
  }

  int rv = 0;

  try {
    stream_logger lgr(iol.term, iol.err, *iol.os, logopts);
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

    reader::filesystem_v2_lite fs(lgr, *iol.os, *fs_image, fsopts, perfmon);
    utility::filesystem_extractor fsx(lgr, *iol.os, iol.file);

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    if (format.name.empty()) {
#endif
      fsx.open_disk(iol.os->canonical(output), num_disk_writers);
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    } else {
      LOG_PROXY(debug_logger_policy, lgr);

      if (num_disk_writers > 0) {
        LOG_INFO
            << "--num-disk-writers option is only used when extracting to disk";
      }

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
    fsx_opts.enable_progress = stdout_progress;
    fsx_opts.skip_devices = skip_devices;
    fsx_opts.skip_specials = skip_specials;
    fsx_opts.disable_hardlinks = no_hardlinks;

    std::optional<progress_thread> prog;

    if (stdout_progress) {
      prog.emplace(40ms, [&fsx, &iol](bool last) {
        if (!last) {
          auto const info = fsx.get_progress();
          int pct = 0;
          if (info.total_bytes) {
            if (info.total_bytes.value() > 0) {
              pct = static_cast<int>(100 * info.extracted_bytes /
                                     info.total_bytes.value());
            } else {
              pct = 100;
            }
          }
          iol.out << "\r" << pct << "%";
        } else {
          iol.out << "\r100%\n";
        }
        iol.out.flush();
      });
    }

    rv = fsx.extract(fs, matcher.get(), fsx_opts) ? 0 : 2;

    fsx.close();

    prog.reset();

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

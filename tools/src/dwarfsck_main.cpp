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
#include <cstring>
#include <iostream>
#include <mutex>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/checksum.h>
#include <dwarfs/config.h>
#include <dwarfs/conv.h>
#include <dwarfs/counting_semaphore.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/program_options_helpers.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/util.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

namespace dwarfs::tool {

namespace po = boost::program_options;

namespace {

void do_list_files(reader::filesystem_v2& fs, iolayer const& iol,
                   bool verbose) {
  auto max_width = [](auto const& vec) {
    auto max = std::max_element(vec.begin(), vec.end());
    return std::to_string(*max).size();
  };

  auto const uid_width = max_width(fs.get_all_uids());
  auto const gid_width = max_width(fs.get_all_gids());

  size_t inode_size_width{0};

  if (verbose) {
    file_stat::off_type max_inode_size{0};
    fs.walk([&](auto const& de) {
      auto st = fs.getattr(de.inode());
      max_inode_size = std::max(max_inode_size, st.size());
    });
    inode_size_width = fmt::format("{:L}", max_inode_size).size();
  }

  fs.walk([&](auto const& de) {
    auto name = de.unix_path();
    utf8_sanitize(name);

    if (verbose) {
      auto iv = de.inode();

      if (iv.is_symlink()) {
        auto target = fs.readlink(iv);
        utf8_sanitize(target);
        name += " -> " + target;
      }

      auto st = fs.getattr(iv);

      iol.out << fmt::format("{3} {4:{0}}/{5:{1}} {6:{2}L} {7:%F %H:%M} {8}\n",
                             uid_width, gid_width, inode_size_width,
                             iv.mode_string(), iv.getuid(), iv.getgid(),
                             st.size(), safe_localtime(st.mtime()), name);
    } else if (!name.empty()) {
      iol.out << name << "\n";
    }
  });
}

void do_checksum(logger& lgr, reader::filesystem_v2& fs, iolayer const& iol,
                 std::string const& algo, size_t num_workers,
                 size_t max_queued_bytes) {
  LOG_PROXY(debug_logger_policy, lgr);

  std::mutex mx;
  counting_semaphore sem;
  sem.post(static_cast<int64_t>(max_queued_bytes));

  thread_pool pool{lgr, *iol.os, "checksum", num_workers};

  size_t const max_queued_per_worker = max_queued_bytes / num_workers;

  fs.walk_data_order([&](auto const& de) {
    auto iv = de.inode();

    if (iv.is_regular_file()) {
      reader::detail::file_reader fr(fs, iv);

      pool.add_job(
          [&, de,
           ranges = fr.read_sequential(sem, max_queued_per_worker)]() mutable {
            try {
              checksum cs(algo);

              for (auto const& r : ranges) {
                cs.update(r.data(), r.size());
              }

              auto output =
                  fmt::format("{}  {}\n", cs.hexdigest(), de.unix_path());

              {
                std::lock_guard lock(mx);
                iol.out << output;
              }
            } catch (std::exception const& e) {
              LOG_ERROR << "error processing inode for " << de.unix_path()
                        << ": " << e.what();
            }
          });
    }
  });

  pool.wait();
}

} // namespace

int dwarfsck_main(int argc, sys_char** argv, iolayer const& iol) {
  size_t const num_cpu = std::max(hardware_concurrency(), 1U);

  auto algo_list = checksum::available_algorithms();
  auto checksum_desc = fmt::format("print checksums for all files ({})",
                                   fmt::join(algo_list, ", "));
  auto detail_desc = fmt::format(
      "detail level (0-{}, or feature list: {})",
      reader::fsinfo_features::max_level(),
      fmt::join(reader::fsinfo_features::all().to_string_views(), ", "));
  auto const detail_default{reader::fsinfo_features::for_level(2).to_string()};

  sys_string input, export_metadata;
  std::string cache_size_str, image_offset, checksum_algo;
  logger_options logopts;
  size_t num_workers;
  std::string detail;
  bool quiet{false};
  bool verbose{false};
  bool output_json{false};
  bool check_integrity{false};
  bool no_check{false};
  bool print_header{false};
  bool list_files{false};

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po_sys_value<sys_string>(&input),
        "input filesystem")
    ("detail,d",
        po::value<std::string>(&detail)->default_value(detail_default),
        detail_desc.c_str())
    ("quiet,q",
        po::value<bool>(&quiet)->zero_tokens(),
        "don't print anything unless an error occurs")
    ("verbose,v",
        po::value<bool>(&verbose)->zero_tokens(),
        "produce verbose output")
    ("image-offset,O",
        po::value<std::string>(&image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
    ("print-header,H",
        po::value<bool>(&print_header)->zero_tokens(),
        "print filesystem header to stdout and exit")
    ("list,l",
        po::value<bool>(&list_files)->zero_tokens(),
        "list all files and exit")
    ("checksum",
        po::value<std::string>(&checksum_algo),
        checksum_desc.c_str())
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of reader worker threads")
    ("cache-size,s",
        po::value<std::string>(&cache_size_str)->default_value("512m"),
        "block cache size")
    ("check-integrity",
        po::value<bool>(&check_integrity)->zero_tokens(),
        "check integrity of each block")
    ("no-check",
        po::value<bool>(&no_check)->zero_tokens(),
        "don't even verify block checksums")
    ("json,j",
        po::value<bool>(&output_json)->zero_tokens(),
        "print information in JSON format")
    ("export-metadata",
        po_sys_value<sys_string>(&export_metadata),
        "export raw metadata as JSON to file")
    ;
  // clang-format on

  tool::add_common_options(opts, logopts);

  po::positional_options_description pos;
  pos.add("input", -1);

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
    tool::show_manpage(tool::manpage::get_dwarfsck_manpage(), iol);
    return 0;
  }
#endif

  auto constexpr usage = "Usage: dwarfsck [OPTIONS...]\n";

  if (vm.contains("help") or !vm.contains("input")) {
    auto extra_deps = [](library_dependencies& deps) {
      decompressor_registry::instance().add_library_dependencies(deps);
    };
    iol.out << tool::tool_header("dwarfsck", extra_deps) << usage << "\n"
            << opts << "\n";
    return 0;
  }

  try {
    stream_logger lgr(iol.term, iol.err, logopts);
    LOG_PROXY(debug_logger_policy, lgr);

    if (no_check && check_integrity) {
      LOG_WARN << "--no-check and --check-integrity are mutually exclusive";
      return 1;
    }

    if (vm.contains("checksum") && !checksum::is_available(checksum_algo)) {
      LOG_WARN << "checksum algorithm not available: " << checksum_algo;
      return 1;
    }

    if (print_header &&
        (output_json || !export_metadata.empty() || check_integrity ||
         list_files || !checksum_algo.empty())) {
      LOG_WARN << "--print-header is mutually exclusive with --json, "
                  "--export-metadata, --check-integrity, --list and --checksum";
      return 1;
    }

    reader::filesystem_options fsopts;

    // This is needed to report a correct original file system size.
    fsopts.metadata.enable_nlink = true;
    fsopts.metadata.check_consistency = check_integrity;
    fsopts.image_offset = reader::parse_image_offset(image_offset);
    fsopts.block_cache.max_bytes = parse_size_with_unit(cache_size_str);
    fsopts.block_cache.num_workers = num_workers;

    auto input_path = iol.os->canonical(input);

    auto mm = iol.os->open_file(input_path);

    if (print_header) {
      if (auto hdr =
              reader::filesystem_v2::header(lgr, mm, fsopts.image_offset)) {
        ensure_binary_mode(iol.out);
        for (auto const& ext : *hdr) {
          for (auto const& seg : ext.segments()) {
            auto const data = seg.span<char>();
            iol.out.write(data.data(), data.size());
          }
        }
        if (iol.out.bad() || iol.out.fail()) {
          LOG_ERROR << "error writing header";
          return 1;
        }
      } else {
        LOG_WARN << "filesystem does not contain a header";
        return 2;
      }
    } else {
      reader::filesystem_v2 fs(lgr, *iol.os, mm, fsopts);

      if (!export_metadata.empty()) {
        std::error_code ec;
        auto of = iol.file->open_output(iol.os->canonical(export_metadata), ec);
        if (ec) {
          LOG_ERROR << "failed to open metadata output file: " << ec.message();
          return 1;
        }
        auto json = fs.serialize_metadata_as_json(false);
        of->os().write(json.data(), json.size());
        of->close(ec);
        if (ec) {
          LOG_ERROR << "failed to close metadata output file: " << ec.message();
          return 1;
        }
      } else {
        auto level = check_integrity ? reader::filesystem_check_level::FULL
                                     : reader::filesystem_check_level::CHECKSUM;
        auto errors = no_check ? 0 : fs.check(level, num_workers);

        if (!quiet && !list_files && checksum_algo.empty()) {
          reader::fsinfo_options opts;

          opts.block_access = no_check
                                  ? reader::block_access_level::no_verify
                                  : reader::block_access_level::unrestricted;

          auto numeric_detail = try_to<int>(detail);
          opts.features =
              numeric_detail.has_value()
                  ? reader::fsinfo_features::for_level(*numeric_detail)
                  : reader::fsinfo_features::parse(detail);

          if (output_json) {
            iol.out << fs.info_as_json(opts) << "\n";
          } else {
            fs.dump(iol.out, opts);
          }
        }

        if (list_files) {
          do_list_files(fs, iol, verbose);
        }

        if (!checksum_algo.empty()) {
          do_checksum(lgr, fs, iol, checksum_algo, num_workers,
                      fsopts.block_cache.max_bytes);
        }

        if (errors > 0) {
          return 1;
        }
      }
    }
  } catch (std::exception const& e) {
    iol.err << exception_str(e) << "\n";
    return 1;
  }

  return 0;
}

} // namespace dwarfs::tool

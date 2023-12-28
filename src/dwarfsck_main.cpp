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

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/json.h>
#include <folly/portability/Unistd.h>
#include <folly/system/HardwareConcurrency.h>

#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/iolayer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/tool.h"
#include "dwarfs_tool_main.h"

namespace dwarfs {

namespace po = boost::program_options;

int dwarfsck_main(int argc, sys_char** argv, iolayer const& iol) {
  const size_t num_cpu = std::max(folly::hardware_concurrency(), 1u);

  std::string log_level, input, export_metadata, image_offset;
  size_t num_workers;
  int detail;
  bool quiet{false};
  bool json{false};
  bool check_integrity{false};
  bool no_check{false};
  bool print_header{false};

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&input),
        "input filesystem")
    ("detail,d",
        po::value<int>(&detail)->default_value(2),
        "detail level")
    ("quiet,q",
        po::value<bool>(&quiet)->zero_tokens(),
        "don't print anything unless an error occurs")
    ("image-offset,O",
        po::value<std::string>(&image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
    ("print-header,H",
        po::value<bool>(&print_header)->zero_tokens(),
        "print filesystem header to stdout and exit")
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of reader worker threads")
    ("check-integrity",
        po::value<bool>(&check_integrity)->zero_tokens(),
        "check integrity of each block")
    ("no-check",
        po::value<bool>(&no_check)->zero_tokens(),
        "don't even verify block checksums")
    ("json,j",
        po::value<bool>(&json)->zero_tokens(),
        "print information in JSON format")
    ("export-metadata",
        po::value<std::string>(&export_metadata),
        "export raw metadata as JSON to file")
      ;
  add_common_options(opts, log_level);
  // clang-format on

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

  auto constexpr usage = "Usage: dwarfsck [OPTIONS...]\n";

  if (vm.count("help") or !vm.count("input")) {
    iol.out << tool_header("dwarfsck") << usage << "\n" << opts << "\n";
    return 0;
  }

  try {
    auto level = logger::parse_level(log_level);
    stream_logger lgr(iol.term, iol.err, level, level >= logger::DEBUG);
    LOG_PROXY(debug_logger_policy, lgr);

    if (no_check && check_integrity) {
      LOG_WARN << "--no-check and --check-integrity are mutually exclusive";
      return 1;
    }

    if (print_header && (json || !export_metadata.empty() || check_integrity)) {
      LOG_WARN << "--print-header is mutually exclusive with --json, "
                  "--export-metadata and --check-integrity";
      return 1;
    }

    filesystem_options fsopts;

    fsopts.metadata.enable_nlink = true;
    fsopts.metadata.check_consistency = check_integrity;

    try {
      fsopts.image_offset = image_offset == "auto"
                                ? filesystem_options::IMAGE_OFFSET_AUTO
                                : folly::to<file_off_t>(image_offset);
    } catch (...) {
      DWARFS_THROW(runtime_error, "failed to parse offset: " + image_offset);
    }

    auto mm = std::make_shared<mmap>(input);

    if (print_header) {
      if (auto hdr = filesystem_v2::header(mm, fsopts.image_offset)) {
#ifdef _WIN32
        ::_setmode(STDOUT_FILENO, _O_BINARY);
#endif
        if (::write(STDOUT_FILENO, hdr->data(), hdr->size()) < 0) {
          LOG_ERROR << "error writing header: " << ::strerror(errno);
          return 1;
        }
      } else {
        LOG_ERROR << "filesystem does not contain a header";
        return 1;
      }
    } else {
      filesystem_v2 fs(lgr, mm, fsopts);

      if (!export_metadata.empty()) {
        auto of = folly::File(export_metadata, O_RDWR | O_CREAT | O_TRUNC);
        auto json = fs.serialize_metadata_as_json(false);
        if (folly::writeFull(of.fd(), json.data(), json.size()) < 0) {
          LOG_ERROR << "failed to export metadata";
        }
        of.close();
      } else {
        auto level = check_integrity ? filesystem_check_level::FULL
                                     : filesystem_check_level::CHECKSUM;
        auto errors = no_check ? 0 : fs.check(level, num_workers);

        if (!quiet) {
          if (json) {
            iol.out << folly::toPrettyJson(fs.info_as_dynamic(detail)) << "\n";
          } else {
            fs.dump(iol.out, detail);
          }
        }

        if (errors > 0) {
          return 1;
        }
      }
    }
  } catch (system_error const& e) {
    iol.err << folly::exceptionStr(e) << "\n";
    return 1;
  } catch (runtime_error const& e) {
    iol.err << folly::exceptionStr(e) << "\n";
    return 1;
  } catch (std::system_error const& e) {
    iol.err << folly::exceptionStr(e) << "\n";
    return 1;
  }

  return 0;
}

int dwarfsck_main(int argc, sys_char** argv) {
  return dwarfsck_main(argc, argv, iolayer::system_default());
}

int dwarfsck_main(std::span<std::string> args, iolayer const& iol) {
  return call_sys_main_iolayer(args, iol, dwarfsck_main);
}

int dwarfsck_main(std::span<std::string_view> args, iolayer const& iol) {
  return call_sys_main_iolayer(args, iol, dwarfsck_main);
}

} // namespace dwarfs

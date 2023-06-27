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
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/tool.h"
#include "dwarfs_tool_main.h"

namespace dwarfs {

namespace po = boost::program_options;

int dwarfsck_main(int argc, sys_char** argv) {
  const size_t num_cpu = std::max(folly::hardware_concurrency(), 1u);

  std::string log_level, input, export_metadata, image_offset;
  size_t num_workers;
  int detail;
  bool json = false;
  bool check_integrity = false;
  bool print_header = false;

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&input),
        "input filesystem")
    ("detail,d",
        po::value<int>(&detail)->default_value(2),
        "detail level")
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
    ("json",
        po::value<bool>(&json)->zero_tokens(),
        "print metadata in JSON format")
    ("export-metadata",
        po::value<std::string>(&export_metadata),
        "export raw metadata as JSON to file")
    ("log-level",
        po::value<std::string>(&log_level)->default_value("info"),
        "log level (error, warn, info, debug, trace)")
    ("help,h",
        "output help message and exit");
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
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  if (vm.count("help") or !vm.count("input")) {
    std::cout << tool_header("dwarfsck") << opts << "\n";
    return 0;
  }

  try {
    auto level = logger::parse_level(log_level);
    stream_logger lgr(std::cerr, level, level >= logger::DEBUG);
    LOG_PROXY(debug_logger_policy, lgr);

    filesystem_options fsopts;

    fsopts.metadata.check_consistency = true;

    try {
      fsopts.image_offset = image_offset == "auto"
                                ? filesystem_options::IMAGE_OFFSET_AUTO
                                : folly::to<file_off_t>(image_offset);
    } catch (...) {
      DWARFS_THROW(runtime_error, "failed to parse offset: " + image_offset);
    }

    auto mm = std::make_shared<mmap>(input);

    if (!export_metadata.empty()) {
      auto of = folly::File(export_metadata, O_RDWR | O_CREAT | O_TRUNC);
      filesystem_v2 fs(lgr, mm, fsopts);
      auto json = fs.serialize_metadata_as_json(false);
      if (folly::writeFull(of.fd(), json.data(), json.size()) < 0) {
        LOG_ERROR << "failed to export metadata";
      }
      of.close();
    } else if (json) {
      filesystem_v2 fs(lgr, mm, fsopts);
      std::cout << folly::toPrettyJson(fs.metadata_as_dynamic()) << "\n";
    } else if (print_header) {
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
      if (filesystem_v2::identify(lgr, mm, std::cout, detail, num_workers,
                                  check_integrity, fsopts.image_offset) != 0) {
        return 1;
      }
    }
  } catch (system_error const& e) {
    std::cerr << folly::exceptionStr(e) << "\n";
    return 1;
  } catch (runtime_error const& e) {
    std::cerr << folly::exceptionStr(e) << "\n";
    return 1;
  } catch (std::system_error const& e) {
    std::cerr << folly::exceptionStr(e) << "\n";
    return 1;
  }

  return 0;
}

} // namespace dwarfs

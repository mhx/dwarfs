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

#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>

#include <folly/String.h>
#include <folly/json.h>
#include <folly/portability/Unistd.h>
#include <folly/system/HardwareConcurrency.h>

#include "dwarfs/error.h"
#include "dwarfs/file_access.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/iolayer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

namespace dwarfs {

namespace po = boost::program_options;

int dwarfsck_main(int argc, sys_char** argv, iolayer const& iol) {
  const size_t num_cpu = std::max(folly::hardware_concurrency(), 1u);

  std::string input, export_metadata, image_offset;
  logger_options logopts;
  size_t num_workers;
  int detail;
  bool quiet{false};
  bool output_json{false};
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
        po::value<bool>(&output_json)->zero_tokens(),
        "print information in JSON format")
    ("export-metadata",
        po::value<std::string>(&export_metadata),
        "export raw metadata as JSON to file")
    ;
  // clang-format on

  add_common_options(opts, logopts);

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
  if (vm.count("man")) {
    show_manpage(manpage::get_dwarfsck_manpage(), iol);
    return 0;
  }
#endif

  auto constexpr usage = "Usage: dwarfsck [OPTIONS...]\n";

  if (vm.count("help") or !vm.count("input")) {
    iol.out << tool_header("dwarfsck") << usage << "\n" << opts << "\n";
    return 0;
  }

  try {
    stream_logger lgr(iol.term, iol.err, logopts);
    LOG_PROXY(debug_logger_policy, lgr);

    if (no_check && check_integrity) {
      LOG_WARN << "--no-check and --check-integrity are mutually exclusive";
      return 1;
    }

    if (print_header &&
        (output_json || !export_metadata.empty() || check_integrity)) {
      LOG_WARN << "--print-header is mutually exclusive with --json, "
                  "--export-metadata and --check-integrity";
      return 1;
    }

    filesystem_options fsopts;

    fsopts.metadata.enable_nlink = true;
    fsopts.metadata.check_consistency = check_integrity;
    fsopts.image_offset = parse_image_offset(image_offset);

    std::shared_ptr<mmif> mm = iol.os->map_file(input);

    if (print_header) {
      if (auto hdr = filesystem_v2::header(mm, fsopts.image_offset)) {
#ifdef _WIN32
        if (&iol.out == &std::cout) {
          ::_setmode(::_fileno(stdout), _O_BINARY);
        }
#endif
        iol.out.write(reinterpret_cast<char const*>(hdr->data()), hdr->size());
        if (iol.out.bad() || iol.out.fail()) {
          LOG_ERROR << "error writing header";
          return 1;
        }
      } else {
        LOG_WARN << "filesystem does not contain a header";
        return 2;
      }
    } else {
      filesystem_v2 fs(lgr, *iol.os, mm, fsopts);

      if (!export_metadata.empty()) {
        std::error_code ec;
        auto of = iol.file->open_output(export_metadata, ec);
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
        auto level = check_integrity ? filesystem_check_level::FULL
                                     : filesystem_check_level::CHECKSUM;
        auto errors = no_check ? 0 : fs.check(level, num_workers);

        if (!quiet) {
          if (output_json) {
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
  } catch (std::exception const& e) {
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

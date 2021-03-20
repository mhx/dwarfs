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

#include <iostream>
#include <vector>

#include <boost/program_options.hpp>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/json.h>

#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/version.h"

namespace dwarfs {

namespace po = boost::program_options;

int dwarfsck(int argc, char** argv) {
  const size_t num_cpu = std::max(std::thread::hardware_concurrency(), 1u);

  std::string log_level, input, export_metadata;
  size_t num_workers;
  int detail;
  bool json = false;

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&input),
        "input filesystem")
    ("detail,d",
        po::value<int>(&detail)->default_value(1),
        "detail level")
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(num_cpu),
        "number of reader worker threads")
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
    po::store(
        po::command_line_parser(argc, argv).options(opts).positional(pos).run(),
        vm);
    po::notify(vm);
  } catch (po::error const& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }

  if (vm.count("help") or !vm.count("input")) {
    std::cout << "dwarfsck (" << PRJ_GIT_ID << ")\n\n" << opts << std::endl;
    return 0;
  }

  stream_logger lgr(std::cerr, logger::parse_level(log_level));
  LOG_PROXY(debug_logger_policy, lgr);

  try {
    auto mm = std::make_shared<mmap>(input);

    if (!export_metadata.empty()) {
      auto of = folly::File(export_metadata, O_RDWR | O_CREAT);
      filesystem_v2 fs(lgr, mm);
      auto json = fs.serialize_metadata_as_json(true);
      if (folly::writeFull(of.fd(), json.data(), json.size()) < 0) {
        LOG_ERROR << "failed to export metadata";
      }
      of.close();
    } else if (json) {
      filesystem_v2 fs(lgr, mm);
      std::cout << folly::toPrettyJson(fs.metadata_as_dynamic()) << std::endl;
    } else {
      filesystem_v2::identify(lgr, mm, std::cout, detail, num_workers);
    }
  } catch (system_error const& e) {
    LOG_ERROR << folly::exceptionStr(e);
    return 1;
  } catch (runtime_error const& e) {
    LOG_ERROR << folly::exceptionStr(e);
    return 1;
  } catch (std::system_error const& e) {
    LOG_ERROR << folly::exceptionStr(e);
    return 1;
  }

  return 0;
}

} // namespace dwarfs

int main(int argc, char** argv) {
  return dwarfs::safe_main([&] { return dwarfs::dwarfsck(argc, argv); });
}

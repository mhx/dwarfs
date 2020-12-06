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

#include <folly/json.h>
#include <folly/String.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"

namespace dwarfs {

namespace po = boost::program_options;

int dwarfsck(int argc, char** argv) {
  std::string log_level, input;
  int detail;
  bool json;

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&input),
        "input filesystem")
    ("detail,d",
        po::value<int>(&detail)->default_value(1),
        "detail level")
    ("json",
        po::value<bool>(&json)->zero_tokens(),
        "print metadata in JSON format")
    ("log-level",
        po::value<std::string>(&log_level)->default_value("info"),
        "log level (error, warn, info, debug, trace)")
    ("help,h",
        "output help message and exit");
  // clang-format on

  po::positional_options_description pos;
  pos.add("input", -1);

  po::variables_map vm;

  po::store(
      po::command_line_parser(argc, argv).options(opts).positional(pos).run(),
      vm);
  po::notify(vm);

  if (vm.count("help") or !vm.count("input")) {
    std::cout << "dwarfsck (" << DWARFS_VERSION << ")\n" << opts << std::endl;
    return 0;
  }

  dwarfs::stream_logger lgr(std::cerr, logger::parse_level(log_level));

  auto mm = std::make_shared<dwarfs::mmap>(input);

  if (json) {
    dwarfs::filesystem_v2 fs(lgr, mm);
    std::cout << folly::toPrettyJson(fs.metadata_as_dynamic()) << std::endl;
  } else {
    dwarfs::filesystem_v2::identify(lgr, mm, std::cout, detail);
  }

  return 0;
}

} // namespace dwarfs

int main(int argc, char** argv) {
  try {
    return dwarfs::dwarfsck(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << folly::exceptionStr(e) << std::endl;
    return 1;
  }
}

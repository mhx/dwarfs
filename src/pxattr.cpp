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

#include <boost/program_options.hpp>

#include "dwarfs/program_options_helpers.h"
#include "dwarfs/types.h"
#include "dwarfs/xattr.h"

namespace dwarfs {

namespace po = boost::program_options;
namespace fs = std::filesystem;

int pxattr_main(int argc, sys_char** argv) {
  std::string name, value;
  sys_string pathstr;

  // clang-format off
  po::options_description desc("Command line options");
  desc.add_options()
    ("get,g", po::value<std::string>(&name), "get extended attribute value")
    ("set,s", po::value<std::string>(&name), "set extended attribute value")
    ("remove,r", po::value<std::string>(&name), "remove extended attribute")
    ("list,l", "list extended attributes")
    ("path", po_sys_value<sys_string>(&pathstr), "path to the file or directory")
    ("value,V", po::value<std::string>(&value), "new attribute value (with -s)")
    ("help,h", "show this help message")
    ;
  // clang-format on

  po::positional_options_description pos;
  pos.add("path", 1);

  po::variables_map vm;
  po::store(po::basic_command_line_parser<sys_char>(argc, argv)
                .options(desc)
                .positional(pos)
                .run(),
            vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  if (!vm.count("path")) {
    std::cerr << "no path specified" << std::endl;
    return 1;
  }

  if (vm.count("get") + vm.count("set") + vm.count("remove") +
          vm.count("list") !=
      1) {
    std::cerr << "exactly one of --get, --set, --remove or --list must be "
                 "specified\n";
    return 1;
  }

  if (vm.count("set") != vm.count("value")) {
    std::cerr << "missing value for --set\n";
    return 1;
  }

  fs::path path(pathstr);

  if (vm.count("get")) {
    std::error_code ec;
    std::string val = getxattr(path, name, ec);
    if (ec) {
      std::cerr << "getxattr failed: " << ec.message() << "\n";
      return 1;
    }
    std::cout << val << "\n";
  } else if (vm.count("set")) {
    std::error_code ec;
    setxattr(path, name, value, ec);
    if (ec) {
      std::cerr << "setxattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (vm.count("remove")) {
    std::error_code ec;
    removexattr(path, name, ec);
    if (ec) {
      std::cerr << "removexattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (vm.count("list")) {
    std::error_code ec;
    std::vector<std::string> attrs = listxattr(path, ec);
    if (ec) {
      std::cerr << "listxattr failed: " << ec.message() << "\n";
      return 1;
    }
    for (const auto& attr : attrs) {
      std::cout << attr << "\n";
    }
  }

  std::cout << "successfully completed\n";

  return 0;
}

} // namespace dwarfs

int SYS_MAIN(int argc, dwarfs::sys_char** argv) {
  return dwarfs::pxattr_main(argc, argv);
}

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

#include <iostream>

#include <boost/program_options.hpp>

#include <dwarfs/tool/program_options_helpers.h>
#include <dwarfs/tool/sys_char.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/xattr.h>

namespace dwarfs::tool {

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

  if (vm.contains("help")) {
    constexpr auto usage = "Usage: pxattr [OPTIONS...]\n";
    std::cout << tool::tool_header_nodeps("pxattr") << usage << "\n"
              << desc << "\n";
    return 0;
  }

  if (!vm.contains("path")) {
    std::cerr << "no path specified\n";
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

  if (vm.contains("get")) {
    std::error_code ec;
    std::string val = getxattr(path, name, ec);
    if (ec) {
      std::cerr << "getxattr failed: " << ec.message() << "\n";
      return 1;
    }
    std::cout << val << "\n";
  } else if (vm.contains("set")) {
    std::error_code ec;
    setxattr(path, name, value, ec);
    if (ec) {
      std::cerr << "setxattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (vm.contains("remove")) {
    std::error_code ec;
    removexattr(path, name, ec);
    if (ec) {
      std::cerr << "removexattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (vm.contains("list")) {
    std::error_code ec;
    std::vector<std::string> attrs = listxattr(path, ec);
    if (ec) {
      std::cerr << "listxattr failed: " << ec.message() << "\n";
      return 1;
    }
    for (auto const& attr : attrs) {
      std::cout << attr << "\n";
    }
  }

  return 0;
}

} // namespace dwarfs::tool

int SYS_MAIN(int argc, dwarfs::tool::sys_char** argv) {
  return dwarfs::tool::pxattr_main(argc, argv);
}

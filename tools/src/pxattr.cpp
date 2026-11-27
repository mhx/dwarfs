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
#include <optional>

#include <dwarfs/command_line.h>
#include <dwarfs/tool/sys_char.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/xattr.h>

namespace dwarfs::tool {

namespace fs = std::filesystem;

int pxattr_main(int argc, sys_char** argv) {
  std::optional<std::string> get, set, remove, value;
  std::optional<sys_string> pathstr;

  cli::options opts;
  bool list{false}, help{false};
  opts.add("get,g", get, "get extended attribute value");
  opts.add("set,s", set, "set extended attribute value");
  opts.add("remove,r", remove, "remove extended attribute");
  opts.flag("list,l", list, "list extended attributes");
  opts.add("path", pathstr, "path to the file or directory");
  opts.add("value,V", value, "new attribute value (with -s)");
  opts.flag("help,h", help, "show this help message");
  opts.positional("path");

  try {
    opts.parse(argc, argv);
  } catch (cli::error const& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  if (help) {
    constexpr auto usage = "Usage: pxattr [OPTIONS...]\n";
    std::cout << tool::tool_header_nodeps("pxattr") << usage << "\n";
    opts.help(std::cout);
    std::cout << "\n";
    return 0;
  }

  if (!pathstr) {
    std::cerr << "no path specified\n";
    return 1;
  }

  if (get.has_value() + set.has_value() + remove.has_value() + list != 1) {
    std::cerr << "exactly one of --get, --set, --remove or --list must be "
                 "specified\n";
    return 1;
  }

  if (set.has_value() != value.has_value()) {
    std::cerr << "missing value for --set\n";
    return 1;
  }

  fs::path path(*pathstr);

  if (get) {
    std::error_code ec;
    std::string val = getxattr(path, *get, ec);
    if (ec) {
      std::cerr << "getxattr failed: " << ec.message() << "\n";
      return 1;
    }
    std::cout << val << "\n";
  } else if (set) {
    std::error_code ec;
    setxattr(path, *set, *value, ec);
    if (ec) {
      std::cerr << "setxattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (remove) {
    std::error_code ec;
    removexattr(path, *remove, ec);
    if (ec) {
      std::cerr << "removexattr failed: " << ec.message() << "\n";
      return 1;
    }
  } else if (list) {
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

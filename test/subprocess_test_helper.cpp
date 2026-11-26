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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#endif

using namespace std::chrono_literals;

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif

  if (argc < 2) {
    return 99;
  }

  std::string_view mode = argv[1];

  if (mode == "args") {
    for (int i = 2; i < argc; ++i) {
      std::string_view arg = argv[i];
      std::cout << arg.size() << ':' << arg << '\n';
    }
    return 0;
  }

  if (mode == "capture") {
    if (std::cin.get() != EOF) {
      return 98;
    }
    for (int i = 0; i < 256; ++i) {
      std::cout << std::string(4096, 'o');
      std::cerr << std::string(4096, 'e');
    }
    std::cout.write("\0\xff", 2);
    return 23;
  }

  if (mode == "sleep") {
    std::this_thread::sleep_for(30s);
    return 0;
  }

#ifndef _WIN32
  if (mode == "signal") {
    std::raise(SIGINT);
    return 97;
  }

  if (mode == "inherited-output") {
    auto pid = ::fork();
    if (pid < 0) {
      return 96;
    }
    if (pid == 0) {
      // Keep the redirected streams open after our parent exits.
      std::this_thread::sleep_for(3s);
      ::_exit(0);
    }
    std::cout << "parent\n";
    return 0;
  }
#endif

  return 95;
}

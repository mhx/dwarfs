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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

FILE* open_out(char const* path) {
  FILE* f = std::fopen(path, "wb");

  if (!f) {
    std::fprintf(stderr, "helper: cannot open %s\n", path);
    std::exit(3);
  }

  return f;
}

void write_record(FILE* f, std::string const& s) {
  std::fwrite(s.data(), 1, s.size(), f);
  std::fputc('\0', f);
}

} // namespace

/**
 * Controllable fake pager used by pager_test.cpp.
 *
 * Modes:
 *   cat  <outfile>            copy all of stdin to <outfile>, exit 0
 *   head <outfile> <n>        read at most n bytes, write them, exit 0
 *                             (closes stdin early -> parent sees EPIPE)
 *   argv <outfile> [args...]  write args to <outfile>, NUL separated
 *   env  <outfile> [names...] write NAME=VALUE to <outfile>, NUL separated;
 *                             unset variables are written as NAME with no '='
 *   fail <outfile> <code>     exit immediately with <code>, don't read stdin
 *
 * SPDX-License-Identifier: MIT
 */

int main(int argc, char** argv) {
#ifdef _WIN32
  ::_setmode(::_fileno(stdin), _O_BINARY);
#endif

  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <mode> <outfile> [...]\n",
                 argc > 0 ? argv[0] : "pager_test_helper");
    return 2;
  }

  std::string const mode{argv[1]};
  char const* out = argv[2];

  if (mode == "fail") {
    return argc > 3 ? std::atoi(argv[3]) : 1;
  }

  if (mode == "argv") {
    FILE* f = open_out(out);
    for (int i = 3; i < argc; ++i) {
      write_record(f, argv[i]);
    }
    std::fclose(f);
    return 0;
  }

  if (mode == "env") {
    FILE* f = open_out(out);
    for (int i = 3; i < argc; ++i) {
      if (char const* value = std::getenv(argv[i])) {
        write_record(f, std::string{argv[i]} + "=" + value);
      } else {
        write_record(f, argv[i]);
      }
    }
    std::fclose(f);
    return 0;
  }

  size_t limit = static_cast<size_t>(-1);

  if (mode == "head") {
    if (argc < 4) {
      return 2;
    }
    limit = static_cast<size_t>(std::strtoull(argv[3], nullptr, 10));
  } else if (mode != "cat") {
    std::fprintf(stderr, "helper: unknown mode %s\n", mode.c_str());
    return 2;
  }

  FILE* f = open_out(out);
  char buf[64 * 1024];
  size_t total = 0;

  while (total < limit) {
    size_t want = std::min(sizeof(buf), limit - total);
    size_t n = std::fread(buf, 1, want, stdin);

    if (n == 0) {
      break;
    }

    std::fwrite(buf, 1, n, f);
    total += n;
  }

  std::fclose(f);

  // For "head": exit without draining stdin. The parent's next write should
  // fail with EPIPE (POSIX) / ERROR_BROKEN_PIPE (Windows).
  return 0;
}

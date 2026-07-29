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

#include <dwarfs/tool/internal/pager_command_line.h>

#ifdef _WIN32

#include <dwarfs/portability/windows.h>

// CommandLineToArgvW; must come after windows.h
#include <shellapi.h>

#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool::internal {

void append_quoted(std::wstring& cmd, std::wstring const& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    cmd += arg;
    return;
  }

  cmd += L'"';

  for (auto it = arg.begin();;) {
    size_t backslashes = 0;

    while (it != arg.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }

    if (it == arg.end()) {
      // Escape all backslashes, but leave the terminating quote unescaped.
      cmd.append(2 * backslashes, L'\\');
      break;
    }

    if (*it == L'"') {
      // Escape all backslashes and the following quote.
      cmd.append(2 * backslashes + 1, L'\\');
      cmd += L'"';
    } else {
      // Backslashes are not special here.
      cmd.append(backslashes, L'\\');
      cmd += *it;
    }

    ++it;
  }

  cmd += L'"';
}

std::optional<std::vector<std::string>>
split_command_line(std::string_view sv) {
  auto const wide = string_to_sys_string(std::string{sv});

  int argc{};
  auto* argv = ::CommandLineToArgvW(wide.c_str(), &argc);

  if (!argv) {
    return std::nullopt;
  }

  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));

  for (int i = 0; i < argc; ++i) {
    args.emplace_back(sys_string_to_string(std::wstring{argv[i]}));
  }

  ::LocalFree(argv);

  return args;
}

} // namespace dwarfs::tool::internal

#endif

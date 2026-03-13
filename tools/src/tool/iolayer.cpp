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

#include <clocale>
#include <iostream>
#include <string_view>

#include <dwarfs/file_access.h>
#include <dwarfs/file_access_generic.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/terminal_ansi.h>
#include <dwarfs/tool/iolayer.h>

namespace dwarfs::tool {

namespace {

bool get_is_utf8_locale() {
  auto const* locale = std::setlocale(LC_CTYPE, nullptr);
  if (!locale) {
    return false;
  }
  auto const loc = std::string_view(locale);
  auto const dot = loc.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  auto const enc = loc.substr(dot + 1);
  return enc == "UTF-8" || enc == "utf8" || enc == "utf-8" || enc == "UTF8";
}

} // namespace

iolayer const& iolayer::system_default() {
  static iolayer const iol{
      .os = std::make_shared<os_access_generic>(),
      .term = std::make_shared<terminal_ansi>(),
      .file = create_file_access_generic(),
      .in = std::cin,
      .out = std::cout,
      .err = std::cerr,
      .is_utf8_locale = get_is_utf8_locale(),
  };
  return iol;
}

} // namespace dwarfs::tool

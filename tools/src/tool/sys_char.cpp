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

#include <algorithm>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool {

std::string sys_string_to_string(sys_string const& in) {
#ifdef _WIN32
  std::u16string tmp(in.size(), 0);
  std::transform(in.begin(), in.end(), tmp.begin(),
                 [](sys_char c) { return static_cast<char16_t>(c); });
  return utf8::utf16to8(tmp);
#else
  return in;
#endif
}

sys_string string_to_sys_string(std::string const& in) {
#ifdef _WIN32
  auto tmp = utf8::utf8to16(in);
  sys_string rv(tmp.size(), 0);
  std::transform(tmp.begin(), tmp.end(), rv.begin(),
                 [](char16_t c) { return static_cast<sys_char>(c); });
  return rv;
#else
  return in;
#endif
}

} // namespace dwarfs::tool

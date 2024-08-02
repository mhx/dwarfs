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

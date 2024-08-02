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

#pragma once

#include <string>

namespace dwarfs::tool {

#ifdef _WIN32
#define SYS_MAIN wmain
using sys_char = wchar_t;
using sys_string = std::wstring;
#else
#define SYS_MAIN main
using sys_char = char;
using sys_string = std::string;
#endif

std::string sys_string_to_string(sys_string const& in);
sys_string string_to_sys_string(std::string const& in);

} // namespace dwarfs::tool

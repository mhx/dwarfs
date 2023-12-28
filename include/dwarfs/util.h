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

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "dwarfs/types.h"

namespace dwarfs {

struct iolayer;

std::string time_with_unit(double sec);
std::string size_with_unit(size_t size);
size_t parse_size_with_unit(std::string const& str);
std::chrono::milliseconds parse_time_with_unit(std::string const& str);

inline std::u8string string_to_u8string(std::string const& in) {
  return std::u8string(reinterpret_cast<char8_t const*>(in.data()), in.size());
}

inline std::string u8string_to_string(std::u8string const& in) {
  return std::string(reinterpret_cast<char const*>(in.data()), in.size());
}

std::string sys_string_to_string(sys_string const& in);
sys_string string_to_sys_string(std::string const& in);

int call_sys_main_iolayer(std::span<std::string_view> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&));

int call_sys_main_iolayer(std::span<std::string> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&));

size_t utf8_display_width(char const* p, size_t len);
size_t utf8_display_width(std::string const& str);
void utf8_truncate(std::string& str, size_t len);

void shorten_path_string(std::string& path, char separator, size_t max_len);

std::filesystem::path canonical_path(std::filesystem::path p);

bool getenv_is_enabled(char const* var);

} // namespace dwarfs

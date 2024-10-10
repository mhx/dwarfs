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
#include <exception>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace dwarfs {

std::string time_with_unit(double sec);
std::string time_with_unit(std::chrono::nanoseconds ns);
std::string size_with_unit(size_t size);
size_t parse_size_with_unit(std::string const& str);
std::chrono::milliseconds parse_time_with_unit(std::string const& str);
std::chrono::system_clock::time_point parse_time_point(std::string const& str);

inline std::u8string string_to_u8string(std::string const& in) {
  return std::u8string(reinterpret_cast<char8_t const*>(in.data()), in.size());
}

inline std::string u8string_to_string(std::u8string const& in) {
  return std::string(reinterpret_cast<char const*>(in.data()), in.size());
}

size_t utf8_display_width(char const* p, size_t len);
size_t utf8_display_width(std::string const& str);
void utf8_truncate(std::string& str, size_t len);
void utf8_sanitize(std::string& str);

void shorten_path_string(std::string& path, char separator, size_t max_len);

std::filesystem::path canonical_path(std::filesystem::path p);
std::string path_to_utf8_string_sanitized(std::filesystem::path const& p);

bool getenv_is_enabled(char const* var);

void setup_default_locale();

std::string_view basename(std::string_view path);

void ensure_binary_mode(std::ostream& os);

std::string exception_str(std::exception const& e);
std::string exception_str(std::exception_ptr const& e);

unsigned int hardware_concurrency() noexcept;

int get_current_umask();

void install_signal_handlers();

} // namespace dwarfs

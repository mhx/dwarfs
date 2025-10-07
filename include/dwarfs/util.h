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

#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <dwarfs/types.h>

namespace dwarfs {

std::string time_with_unit(double sec);
std::string time_with_unit(std::chrono::nanoseconds ns);
std::string size_with_unit(file_size_t size);
std::string ratio_to_string(double num, double den, int precision = 3);
file_size_t parse_size_with_unit(std::string const& str);
std::chrono::milliseconds parse_time_with_unit(std::string const& str);
std::chrono::system_clock::time_point parse_time_point(std::string const& str);

std::unordered_map<std::string_view, std::string_view>
parse_option_string(std::string_view str);

inline std::u8string string_to_u8string(std::string const& in) {
  return {reinterpret_cast<char8_t const*>(in.data()), in.size()};
}

inline std::string u8string_to_string(std::u8string const& in) {
  return {reinterpret_cast<char const*>(in.data()), in.size()};
}

size_t utf8_display_width(char const* p, size_t len);
size_t utf8_display_width(std::string const& str);
void utf8_truncate(std::string& str, size_t len);
void utf8_sanitize(std::string& str);

std::string error_cp_to_utf8(std::string_view error);

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

std::tm safe_localtime(std::time_t t);

std::optional<size_t> get_self_memory_usage();

} // namespace dwarfs

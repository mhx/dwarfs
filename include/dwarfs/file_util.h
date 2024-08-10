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

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace dwarfs {

std::string read_file(std::filesystem::path const& path, std::error_code& ec);
std::string read_file(std::filesystem::path const& path);

void write_file(std::filesystem::path const& path, std::string const& content,
                std::error_code& ec);
void write_file(std::filesystem::path const& path, std::string const& content);

class temporary_directory {
 public:
  temporary_directory();
  explicit temporary_directory(std::string_view prefix);
  ~temporary_directory();

  temporary_directory(temporary_directory&&) = default;
  temporary_directory& operator=(temporary_directory&&) = default;

  std::filesystem::path const& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

} // namespace dwarfs

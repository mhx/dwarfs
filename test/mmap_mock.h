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

#include <filesystem>
#include <optional>
#include <string>

#include <dwarfs/file_view.h>

namespace dwarfs::test {

// TODO: see which of those we actually need

struct mock_file_view_options {
  std::optional<bool> support_raw_bytes;
};

file_view
make_mock_file_view(std::string data, mock_file_view_options const& opts = {});

file_view make_mock_file_view(std::string data,
                              std::vector<detail::file_extent_info> extents,
                              mock_file_view_options const& opts = {});

file_view
make_mock_file_view(std::string data, std::filesystem::path const& path,
                    mock_file_view_options const& opts = {});

file_view
make_mock_file_view(std::string data, std::filesystem::path const& path,
                    std::vector<detail::file_extent_info> extents,
                    mock_file_view_options const& opts = {});

file_view make_mock_file_view(std::string const& data, file_size_t size,
                              mock_file_view_options const& opts = {});

file_view make_mock_file_view(std::string const& data, file_size_t size,
                              std::filesystem::path const& path,
                              mock_file_view_options const& opts = {});

} // namespace dwarfs::test

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

#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <vector>

#include <dwarfs/file_range.h>

namespace dwarfs::test {

enum class diff_kind {
  only_in_left,
  only_in_right,
  type_mismatch,
  symlink_target_diff,
  file_size_diff,
  file_content_diff,
  error
};

struct entry_diff {
  std::filesystem::path relpath{};
  diff_kind kind{diff_kind::error};

  // Useful context
  std::filesystem::file_type left_type{std::filesystem::file_type::none};
  std::filesystem::file_type right_type{std::filesystem::file_type::none};

  // For size diffs
  std::optional<uintmax_t> left_size{};
  std::optional<uintmax_t> right_size{};

  // For symlink diffs
  std::optional<std::filesystem::path> left_link_target{};
  std::optional<std::filesystem::path> right_link_target{};

  // For content diffs
  std::vector<file_range> ranges{};

  // For errors
  std::optional<std::string> error_message{};
};

struct directory_diff {
  std::vector<entry_diff> differences;
  std::vector<std::filesystem::path> matching_directories;
  std::vector<std::filesystem::path> matching_symlinks;
  std::vector<std::filesystem::path> matching_regular_files;
  file_size_t total_matching_regular_file_size{0};

  bool identical() const noexcept { return differences.empty(); }
};

directory_diff compare_directories(std::filesystem::path const& left_root,
                                   std::filesystem::path const& right_root);

std::ostream& operator<<(std::ostream& os, directory_diff const& d);

} // namespace dwarfs::test

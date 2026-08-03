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

#include <initializer_list>
#include <optional>
#include <string_view>

#include <dwarfs/file_stat.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/types.h>

namespace dwarfs::test {

/**
 * Expected attributes of a single file system entry.
 *
 * Only the fields that are actually set are checked; everything else is
 * ignored. This mirrors what the hand-written check blocks used to do, where
 * each block only asserted the attributes it cared about.
 */
struct expected_attrs {
  std::optional<posix_file_type::value> type{};
  std::optional<file_stat::off_type> size{};
  std::optional<file_stat::off_type> allocated_size{};
  std::optional<file_stat::off_type> blocks{};
  std::optional<file_stat::time_type> atime{};
  std::optional<file_stat::time_type> mtime{};
  std::optional<file_stat::time_type> ctime{};
  std::optional<file_stat::timespec_type> atimespec{};
  std::optional<file_stat::timespec_type> mtimespec{};
  std::optional<file_stat::timespec_type> ctimespec{};
  std::optional<file_stat::uid_type> uid{};
  std::optional<file_stat::gid_type> gid{};
  std::optional<file_stat::mode_type> permissions{};
  std::optional<file_stat::nlink_type> nlink{};
};

struct expected_entry {
  std::string_view path;
  expected_attrs attrs;
};

/**
 * Check the attributes of the entry at `path`.
 *
 * A missing entry raises a fatal failure, so call sites that need to abort the
 * whole test (rather than just this check) should wrap the call in
 * `ASSERT_NO_FATAL_FAILURE()`.
 */
void expect_attrs(reader::filesystem_v2 const& fs, std::string_view path,
                  expected_attrs const& expected);

/**
 * Check the attributes of multiple entries. Stops at the first entry that
 * cannot be found.
 */
void expect_attrs(reader::filesystem_v2 const& fs,
                  std::initializer_list<expected_entry> entries);

/**
 * Check that two paths refer to the same inode, i.e. that they are hardlinks
 * of each other.
 */
void expect_same_inode(reader::filesystem_v2 const& fs, std::string_view a,
                       std::string_view b);

} // namespace dwarfs::test

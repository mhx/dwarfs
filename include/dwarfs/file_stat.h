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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>

#include <dwarfs/error.h>
#include <dwarfs/file_type.h>

namespace dwarfs {

struct file_stat {
  using valid_fields_type = uint32_t;
  using perms_type = std::underlying_type_t<std::filesystem::perms>;
  using mode_type = uint32_t;
  using dev_type = uint64_t;
  using ino_type = uint64_t;
  using nlink_type = uint64_t;
  using uid_type = uint32_t;
  using gid_type = uint32_t;
  using off_type = int64_t;
  using blksize_type = int64_t;
  using blkcnt_type = int64_t;
  using time_type = int64_t;

  void ensure_valid(valid_fields_type fields) const;

  std::filesystem::file_status status() const {
    ensure_valid(mode_valid);
    return file_mode_to_status(mode);
  };

  posix_file_type::value type() const {
    ensure_valid(mode_valid);
    return static_cast<posix_file_type::value>(mode & posix_file_type::mask);
  };

  perms_type permissions() const {
    ensure_valid(mode_valid);
    return mode & 07777;
  };

  void set_permissions(perms_type perms) { mode = type() | (perms & 07777); }

  bool is_directory() const { return type() == posix_file_type::directory; }

  bool is_regular_file() const { return type() == posix_file_type::regular; }

  bool is_symlink() const { return type() == posix_file_type::symlink; }

  bool is_device() const {
    auto t = type();
    return t == posix_file_type::block || t == posix_file_type::character;
  }

  static std::string perm_string(mode_type mode);
  static std::string mode_string(mode_type mode);

  static constexpr valid_fields_type dev_valid = 1 << 0;
  static constexpr valid_fields_type ino_valid = 1 << 1;
  static constexpr valid_fields_type nlink_valid = 1 << 2;
  static constexpr valid_fields_type mode_valid = 1 << 3;
  static constexpr valid_fields_type uid_valid = 1 << 4;
  static constexpr valid_fields_type gid_valid = 1 << 5;
  static constexpr valid_fields_type rdev_valid = 1 << 6;
  static constexpr valid_fields_type size_valid = 1 << 7;
  static constexpr valid_fields_type blksize_valid = 1 << 8;
  static constexpr valid_fields_type blocks_valid = 1 << 9;
  static constexpr valid_fields_type atime_valid = 1 << 10;
  static constexpr valid_fields_type mtime_valid = 1 << 11;
  static constexpr valid_fields_type ctime_valid = 1 << 12;
  static constexpr valid_fields_type all_valid = (1 << 13) - 1;

  uint32_t valid_fields{0};
  dev_type dev;
  ino_type ino;
  nlink_type nlink;
  mode_type mode;
  uid_type uid;
  gid_type gid;
  dev_type rdev;
  off_type size;
  blksize_type blksize;
  blkcnt_type blocks;
  time_type atime;
  time_type mtime;
  time_type ctime;
  std::exception_ptr exception;
};

file_stat make_file_stat(std::filesystem::path const& path);

template <bool with_block_info = true, typename T>
void copy_file_stat(T* out, file_stat const& in) {
  out->st_dev = in.dev;
  out->st_ino = in.ino;
  out->st_nlink = in.nlink;
  out->st_mode = in.mode;
  out->st_uid = in.uid;
  out->st_gid = in.gid;
  out->st_rdev = in.rdev;
  out->st_size = in.size;
  if constexpr (with_block_info) {
    out->st_blksize = in.blksize;
    out->st_blocks = in.blocks;
  }
  out->st_atime = in.atime;
  out->st_mtime = in.mtime;
  out->st_ctime = in.ctime;
}

} // namespace dwarfs

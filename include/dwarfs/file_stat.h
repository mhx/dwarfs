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

class file_stat {
 public:
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

  file_stat();
  explicit file_stat(std::filesystem::path const& path);

  void ensure_valid(valid_fields_type fields) const;

  std::filesystem::file_status status() const;
  posix_file_type::value type() const;

  perms_type permissions() const;
  void set_permissions(perms_type perms);

  dev_type dev() const;
  dev_type dev_unchecked() const { return dev_; }
  void set_dev(dev_type dev);

  ino_type ino() const;
  ino_type ino_unchecked() const { return ino_; }
  void set_ino(ino_type ino);

  nlink_type nlink() const;
  nlink_type nlink_unchecked() const { return nlink_; }
  void set_nlink(nlink_type nlink);

  mode_type mode() const;
  mode_type mode_unchecked() const { return mode_; }
  void set_mode(mode_type mode);

  uid_type uid() const;
  uid_type uid_unchecked() const { return uid_; }
  void set_uid(uid_type uid);

  gid_type gid() const;
  gid_type gid_unchecked() const { return gid_; }
  void set_gid(gid_type gid);

  dev_type rdev() const;
  dev_type rdev_unchecked() const { return rdev_; }
  void set_rdev(dev_type rdev);

  off_type size() const;
  off_type size_unchecked() const { return size_; }
  void set_size(off_type size);

  blksize_type blksize() const;
  blksize_type blksize_unchecked() const { return blksize_; }
  void set_blksize(blksize_type blksize);

  blkcnt_type blocks() const;
  blkcnt_type blocks_unchecked() const { return blocks_; }
  void set_blocks(blkcnt_type blocks);

  time_type atime() const;
  time_type atime_unchecked() const { return atime_; }
  void set_atime(time_type atime);

  time_type mtime() const;
  time_type mtime_unchecked() const { return mtime_; }
  void set_mtime(time_type mtime);

  time_type ctime() const;
  time_type ctime_unchecked() const { return ctime_; }
  void set_ctime(time_type ctime);

  bool is_directory() const;
  bool is_regular_file() const;
  bool is_symlink() const;
  bool is_device() const;

  static std::string perm_string(mode_type mode);
  static std::string mode_string(mode_type mode);

  std::string perm_string() const { return perm_string(mode()); }
  std::string mode_string() const { return mode_string(mode()); }

  template <typename T>
  void copy_to(T* out) const {
    copy_to_impl<true>(out);
  }

  template <typename T>
  void copy_to_without_block_info(T* out) const {
    copy_to_impl<false>(out);
  }

 private:
  template <bool with_block_info = true, typename T>
  void copy_to_impl(T* out) const {
    constexpr valid_fields_type required_fields{
        with_block_info ? all_valid
                        : all_valid & ~(blksize_valid | blocks_valid)};
    ensure_valid(required_fields);
    out->st_dev = dev_;
    out->st_ino = ino_;
    out->st_nlink = nlink_;
    out->st_mode = mode_;
    out->st_uid = uid_;
    out->st_gid = gid_;
    out->st_rdev = rdev_;
    out->st_size = size_;
    if constexpr (with_block_info) {
      out->st_blksize = blksize_;
      out->st_blocks = blocks_;
    }
    out->st_atime = atime_;
    out->st_mtime = mtime_;
    out->st_ctime = ctime_;
  }

  uint32_t valid_fields_{0};
  dev_type dev_{};
  ino_type ino_{};
  nlink_type nlink_{};
  mode_type mode_{};
  uid_type uid_{};
  gid_type gid_{};
  dev_type rdev_{};
  off_type size_{};
  blksize_type blksize_{};
  blkcnt_type blocks_{};
  time_type atime_{};
  time_type mtime_{};
  time_type ctime_{};
  std::exception_ptr exception_;
};

} // namespace dwarfs

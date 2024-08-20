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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <boost/range/irange.hpp>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_type.h>

namespace dwarfs::reader {

namespace internal {

template <typename T>
class metadata_;

class inode_view_impl;
class dir_entry_view_impl;
class global_metadata;

} // namespace internal

// TODO: move this elsewhere
enum class readlink_mode {
  raw,
  preferred,
  posix,
};

class inode_view {
 public:
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;

  inode_view() = default;
  explicit inode_view(std::shared_ptr<internal::inode_view_impl const> iv)
      : iv_{std::move(iv)} {}

  mode_type mode() const;
  std::string mode_string() const;
  std::string perm_string() const;
  posix_file_type::value type() const;
  bool is_regular_file() const;
  bool is_directory() const;
  bool is_symlink() const;
  uid_type getuid() const;
  gid_type getgid() const;
  uint32_t inode_num() const;

  internal::inode_view_impl const& raw() const { return *iv_; }

 private:
  std::shared_ptr<internal::inode_view_impl const> iv_;
};

class dir_entry_view {
 public:
  dir_entry_view() = default;
  dir_entry_view(std::shared_ptr<internal::dir_entry_view_impl const> impl)
      : impl_{std::move(impl)} {}

  std::string name() const;
  inode_view inode() const;

  bool is_root() const;
  std::optional<dir_entry_view> parent() const;

  std::string path() const;
  std::string unix_path() const;
  std::filesystem::path fs_path() const;
  std::wstring wpath() const;

  internal::dir_entry_view_impl const& raw() const { return *impl_; }

 private:
  std::shared_ptr<internal::dir_entry_view_impl const> impl_;
};

class directory_view {
  template <typename T>
  friend class internal::metadata_;

  friend class dir_entry_view;

 public:
  uint32_t inode() const { return inode_; }
  uint32_t parent_inode() const;

  uint32_t first_entry() const { return first_entry(inode_); }
  uint32_t parent_entry() const { return parent_entry(inode_); }
  uint32_t entry_count() const;
  boost::integer_range<uint32_t> entry_range() const;

 private:
  directory_view(uint32_t inode, internal::global_metadata const& g)
      : inode_{inode}
      , g_{&g} {}

  uint32_t first_entry(uint32_t ino) const;
  uint32_t parent_entry(uint32_t ino) const;

  uint32_t inode_;
  internal::global_metadata const* g_;
};

} // namespace dwarfs::reader

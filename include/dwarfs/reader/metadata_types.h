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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <boost/range/irange.hpp>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_type.h>
#include <dwarfs/reader/seek_whence.h>

namespace dwarfs::reader {

namespace internal {

class metadata_v2_data;

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

class directory_iterator {
 public:
  using value_type = dir_entry_view;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type const*;
  using reference = value_type const&;

  directory_iterator() = default;
  directory_iterator(uint32_t inode, internal::global_metadata const& g);

  reference operator*() const { return current_; }
  pointer operator->() const { return &current_; }

  directory_iterator& operator++();
  directory_iterator operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  bool operator==(directory_iterator const& other) const;
  bool operator!=(directory_iterator const& other) const {
    return !(*this == other);
  }

 private:
  directory_iterator(uint32_t inode, uint32_t first, uint32_t last,
                     internal::global_metadata const& g);

  dir_entry_view current_;
  uint32_t last_index_{0};
  internal::global_metadata const* g_{nullptr};
};

static_assert(std::input_iterator<directory_iterator>);

class directory_view {
  friend class internal::metadata_v2_data;
  friend class dir_entry_view;

 public:
  uint32_t inode() const { return inode_; }

  uint32_t first_entry() const { return first_entry(inode_); }
  uint32_t parent_entry() const;
  uint32_t self_entry() const;
  uint32_t entry_count() const;
  boost::integer_range<uint32_t> entry_range() const;

  directory_iterator begin() const { return directory_iterator{inode_, *g_}; }
  directory_iterator end() const { return directory_iterator{}; }

  dir_entry_view self_entry_view() const;

 private:
  directory_view(uint32_t inode, internal::global_metadata const& g)
      : inode_{inode}
      , g_{&g} {}

  uint32_t first_entry(uint32_t ino) const;

  uint32_t inode_{0};
  internal::global_metadata const* g_{nullptr};
};

} // namespace dwarfs::reader

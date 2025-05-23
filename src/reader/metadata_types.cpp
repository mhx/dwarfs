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

#include <algorithm>
#include <cassert>
#include <numeric>
#include <queue>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/reader/metadata_types.h>
#include <dwarfs/util.h>

#include <dwarfs/reader/internal/metadata_types.h>

namespace dwarfs::reader {

inode_view::mode_type inode_view::mode() const { return iv_->mode(); }

std::string inode_view::mode_string() const { return iv_->mode_string(); }

std::string inode_view::perm_string() const { return iv_->perm_string(); }

posix_file_type::value inode_view::type() const { return iv_->type(); }

bool inode_view::is_regular_file() const {
  return iv_->type() == posix_file_type::regular;
}

bool inode_view::is_directory() const {
  return iv_->type() == posix_file_type::directory;
}

bool inode_view::is_symlink() const {
  return iv_->type() == posix_file_type::symlink;
}

inode_view::uid_type inode_view::getuid() const { return iv_->getuid(); }

inode_view::gid_type inode_view::getgid() const { return iv_->getgid(); }

uint32_t inode_view::inode_num() const { return iv_->inode_num(); }

std::string dir_entry_view::name() const { return impl_->name(); }

inode_view dir_entry_view::inode() const {
  return inode_view{impl_->inode_shared()};
}

bool dir_entry_view::is_root() const { return impl_->is_root(); }

std::optional<dir_entry_view> dir_entry_view::parent() const {
  if (auto p = impl_->parent()) {
    return dir_entry_view{std::move(p)};
  }
  return std::nullopt;
}

std::string dir_entry_view::path() const { return impl_->path(); }

std::string dir_entry_view::unix_path() const { return impl_->unix_path(); }

std::filesystem::path dir_entry_view::fs_path() const {
  return impl_->fs_path();
}

std::wstring dir_entry_view::wpath() const { return impl_->wpath(); }

directory_iterator::directory_iterator(uint32_t inode, uint32_t first,
                                       uint32_t last,
                                       internal::global_metadata const& g)
    : current_{first != last
                   ? internal::dir_entry_view_impl::from_dir_entry_index_shared(
                         first, g.self_dir_entry(inode), g)
                   : nullptr}
    , last_index_{last}
    , g_{first != last ? &g : nullptr} {}

directory_iterator::directory_iterator(uint32_t inode,
                                       internal::global_metadata const& g)
    : directory_iterator(inode, g.first_dir_entry(inode),
                         g.first_dir_entry(inode + 1), g) {}

directory_iterator& directory_iterator::operator++() {
  auto const& raw = current_.raw();
  auto next_index = raw.self_index() + 1;

  if (next_index < last_index_) {
    current_ = dir_entry_view{
        internal::dir_entry_view_impl::from_dir_entry_index_shared(
            next_index, raw.parent_index(), *g_)};
  } else {
    current_ = dir_entry_view{};
    g_ = nullptr;
  }

  return *this;
}

bool directory_iterator::operator==(directory_iterator const& other) const {
  return (!g_ && !other.g_) ||
         (g_ == other.g_ &&
          current_.raw().self_index() == other.current_.raw().self_index());
}

uint32_t directory_view::first_entry(uint32_t ino) const {
  return g_->first_dir_entry(ino);
}

uint32_t directory_view::parent_entry(uint32_t ino) const {
  return g_->parent_dir_entry(ino);
}

uint32_t directory_view::entry_count() const {
  return first_entry(inode_ + 1) - first_entry();
}

boost::integer_range<uint32_t> directory_view::entry_range() const {
  return boost::irange(first_entry(), first_entry(inode_ + 1));
}

uint32_t directory_view::parent_inode() const {
  if (inode_ == 0) {
    return 0;
  }

  auto ent = parent_entry(inode_);

  if (auto e = g_->meta().dir_entries()) {
    ent = (*e)[ent].inode_num();
  }

  return ent;
}

dir_entry_view directory_view::self_entry_view() const {
  return dir_entry_view{
      internal::dir_entry_view_impl::from_dir_entry_index_shared(
          g_->self_dir_entry(inode_), *g_)};
}

} // namespace dwarfs::reader

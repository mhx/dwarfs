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

#include "dwarfs/metadata_types.h"
#include "dwarfs/error.h"
#include "dwarfs/overloaded.h"

#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"

namespace dwarfs {

uint16_t inode_view::mode() const { return meta_->modes()[mode_index()]; }

uint16_t inode_view::getuid() const { return meta_->uids()[owner_index()]; }

uint16_t inode_view::getgid() const { return meta_->gids()[group_index()]; }

// TODO: pretty certain some of this stuff can be simplified

std::string_view dir_entry_view::name() const {
  return std::visit(overloaded{
                        [this](DirEntryView const& dev) {
                          return meta_->names()[dev.name_index()];
                        },
                        [this](InodeView const& iv) {
                          return meta_->names()[iv.name_index_v2_2()];
                        },
                    },
                    v_);
}

inode_view dir_entry_view::inode() const {
  return std::visit(overloaded{
                        [this](DirEntryView const& dev) {
                          return inode_view(meta_->inodes()[dev.inode_num()],
                                            dev.inode_num(), meta_);
                        },
                        [this](InodeView const& iv) {
                          return inode_view(iv, iv.inode_v2_2(), meta_);
                        },
                    },
                    v_);
}

bool dir_entry_view::is_root() const {
  return std::visit(
      overloaded{
          [](DirEntryView const& dev) { return dev.inode_num() == 0; },
          [](InodeView const& iv) { return iv.inode_v2_2() == 0; },
      },
      v_);
}

/**
 * We need a parent index if the dir_entry_view is for a file. For
 * directories, the parent can be determined via the directory's
 * inode, but for files, this isn't possible.
 */

dir_entry_view
dir_entry_view::from_dir_entry_index(uint32_t self_index, uint32_t parent_index,
                                     Meta const* meta) {
  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(self_index < de->size(), "self_index out of range");
    DWARFS_CHECK(parent_index < de->size(), "parent_index out of range");

    auto dev = (*de)[self_index];

    return dir_entry_view(dev, self_index, parent_index, meta);
  }

  DWARFS_CHECK(self_index < meta->inodes().size(), "self_index out of range");
  DWARFS_CHECK(parent_index < meta->inodes().size(), "self_index out of range");

  auto iv = meta->inodes()[self_index];

  return dir_entry_view(iv, self_index, parent_index, meta);
}

dir_entry_view
dir_entry_view::from_dir_entry_index(uint32_t self_index, Meta const* meta) {
  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(self_index < de->size(), "self_index out of range");
    auto dev = (*de)[self_index];
    DWARFS_CHECK(dev.inode_num() < meta->directories().size(),
                 "self_index inode out of range");
    return dir_entry_view(dev, self_index,
                          meta->directories()[dev.inode_num()].parent_entry(),
                          meta);
  }

  DWARFS_CHECK(self_index < meta->inodes().size(), "self_index out of range");
  auto iv = meta->inodes()[self_index];

  DWARFS_CHECK(iv.inode_v2_2() < meta->directories().size(),
               "parent_index out of range");
  return dir_entry_view(
      iv, self_index,
      meta->entry_table_v2_2()[meta->directories()[iv.inode_v2_2()]
                                   .parent_entry()],
      meta);
}

std::optional<dir_entry_view> dir_entry_view::parent() const {
  if (is_root()) {
    return std::nullopt;
  }

  return from_dir_entry_index(parent_index_, meta_);
}

std::string_view dir_entry_view::name(uint32_t index, Meta const* meta) {
  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(index < de->size(), "index out of range");
    auto dev = (*de)[index];
    return meta->names()[dev.name_index()];
  }

  DWARFS_CHECK(index < meta->inodes().size(), "index out of range");
  auto iv = meta->inodes()[index];
  return meta->names()[iv.name_index_v2_2()];
}

inode_view dir_entry_view::inode(uint32_t index, Meta const* meta) {
  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(index < de->size(), "index out of range");
    auto dev = (*de)[index];
    return inode_view(meta->inodes()[dev.inode_num()], dev.inode_num(), meta);
  }

  DWARFS_CHECK(index < meta->inodes().size(), "index out of range");
  auto iv = meta->inodes()[index];
  return inode_view(iv, iv.inode_v2_2(), meta);
}

std::string dir_entry_view::path() const {
  std::string p;
  append_path_to(p);
  return p;
}

void dir_entry_view::append_path_to(std::string& s) const {
  if (auto p = parent()) {
    if (!p->is_root()) {
      p->append_path_to(s);
      s += '/';
    }
  }
  if (!is_root()) {
    s += name();
  }
}

uint32_t directory_view::first_entry(uint32_t ino) const {
  return directories_ ? directories_[ino].first_entry
                      : meta_->directories()[ino].first_entry();
}

uint32_t directory_view::parent_entry(uint32_t ino) const {
  return directories_ ? directories_[ino].parent_entry
                      : meta_->directories()[ino].parent_entry();
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

  if (auto e = meta_->dir_entries()) {
    ent = (*e)[ent].inode_num();
  }

  return ent;
}

} // namespace dwarfs

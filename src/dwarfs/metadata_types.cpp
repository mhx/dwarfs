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

#include <queue>

#include "dwarfs/error.h"
#include "dwarfs/metadata_types.h"
#include "dwarfs/overloaded.h"

#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"

namespace dwarfs {

namespace {

std::vector<thrift::metadata::directory>
unpack_directories(global_metadata::Meta const* meta) {
  std::vector<thrift::metadata::directory> directories;

  if (auto opts = meta->options(); opts and opts->packed_directories()) {
    auto dirent = *meta->dir_entries();
    auto metadir = meta->directories();

    {
      directories.resize(metadir.size());

      // delta-decode first entries first
      directories[0].first_entry = metadir[0].first_entry();

      for (size_t i = 1; i < directories.size(); ++i) {
        directories[i].first_entry =
            directories[i - 1].first_entry + metadir[i].first_entry();
      }

      // then traverse to recover parent entries
      std::queue<uint32_t> queue;
      queue.push(0);

      while (!queue.empty()) {
        auto parent = queue.front();
        queue.pop();

        auto p_ino = dirent[parent].inode_num();

        auto beg = directories[p_ino].first_entry;
        auto end = directories[p_ino + 1].first_entry;

        for (auto e = beg; e < end; ++e) {
          if (auto e_ino = dirent[e].inode_num();
              e_ino < (directories.size() - 1)) {
            directories[e_ino].parent_entry = parent;
            queue.push(e);
          }
        }
      }
    }
  }

  return directories;
}

} // namespace

global_metadata::global_metadata(Meta const* meta)
    : meta_{meta}
    , directories_storage_{unpack_directories(meta_)}
    , directories_{directories_storage_.empty() ? nullptr
                                                : directories_storage_.data()}
    , names_{meta_->compact_names() ? string_table(*meta_->compact_names())
                                    : string_table(meta_->names())} {}

uint32_t global_metadata::first_dir_entry(uint32_t ino) const {
  return directories_ ? directories_[ino].first_entry
                      : meta_->directories()[ino].first_entry();
}

uint32_t global_metadata::parent_dir_entry(uint32_t ino) const {
  return directories_ ? directories_[ino].parent_entry
                      : meta_->directories()[ino].parent_entry();
}

uint16_t inode_view::mode() const { return meta_->modes()[mode_index()]; }

uint16_t inode_view::getuid() const { return meta_->uids()[owner_index()]; }

uint16_t inode_view::getgid() const { return meta_->gids()[group_index()]; }

// TODO: pretty certain some of this stuff can be simplified

std::string dir_entry_view::name() const {
  return std::visit(overloaded{
                        [this](DirEntryView const& dev) {
                          return g_->names()[dev.name_index()];
                        },
                        [this](InodeView const& iv) {
                          return std::string(
                              g_->meta()->names()[iv.name_index_v2_2()]);
                        },
                    },
                    v_);
}

inode_view dir_entry_view::inode() const {
  return std::visit(overloaded{
                        [this](DirEntryView const& dev) {
                          return inode_view(
                              g_->meta()->inodes()[dev.inode_num()],
                              dev.inode_num(), g_->meta());
                        },
                        [this](InodeView const& iv) {
                          return inode_view(iv, iv.inode_v2_2(), g_->meta());
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
                                     global_metadata const* g) {
  auto meta = g->meta();

  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(self_index < de->size(), "self_index out of range");
    DWARFS_CHECK(parent_index < de->size(), "parent_index out of range");

    auto dev = (*de)[self_index];

    return dir_entry_view(dev, self_index, parent_index, g);
  }

  DWARFS_CHECK(self_index < meta->inodes().size(), "self_index out of range");
  DWARFS_CHECK(parent_index < meta->inodes().size(), "self_index out of range");

  auto iv = meta->inodes()[self_index];

  return dir_entry_view(iv, self_index, parent_index, g);
}

dir_entry_view dir_entry_view::from_dir_entry_index(uint32_t self_index,
                                                    global_metadata const* g) {
  auto meta = g->meta();

  if (auto de = meta->dir_entries()) {
    DWARFS_CHECK(self_index < de->size(), "self_index out of range");
    auto dev = (*de)[self_index];
    DWARFS_CHECK(dev.inode_num() < meta->directories().size(),
                 "self_index inode out of range");
    return dir_entry_view(dev, self_index, g->parent_dir_entry(dev.inode_num()),
                          g);
  }

  DWARFS_CHECK(self_index < meta->inodes().size(), "self_index out of range");
  auto iv = meta->inodes()[self_index];

  DWARFS_CHECK(iv.inode_v2_2() < meta->directories().size(),
               "parent_index out of range");
  return dir_entry_view(
      iv, self_index,
      meta->entry_table_v2_2()[meta->directories()[iv.inode_v2_2()]
                                   .parent_entry()],
      g);
}

std::optional<dir_entry_view> dir_entry_view::parent() const {
  if (is_root()) {
    return std::nullopt;
  }

  return from_dir_entry_index(parent_index_, g_);
}

std::string dir_entry_view::name(uint32_t index, global_metadata const* g) {
  if (auto de = g->meta()->dir_entries()) {
    DWARFS_CHECK(index < de->size(), "index out of range");
    auto dev = (*de)[index];
    return g->names()[dev.name_index()];
  }

  DWARFS_CHECK(index < g->meta()->inodes().size(), "index out of range");
  auto iv = g->meta()->inodes()[index];
  return std::string(g->meta()->names()[iv.name_index_v2_2()]);
}

inode_view dir_entry_view::inode(uint32_t index, global_metadata const* g) {
  if (auto de = g->meta()->dir_entries()) {
    DWARFS_CHECK(index < de->size(), "index out of range");
    auto dev = (*de)[index];
    return inode_view(g->meta()->inodes()[dev.inode_num()], dev.inode_num(),
                      g->meta());
  }

  DWARFS_CHECK(index < g->meta()->inodes().size(), "index out of range");
  auto iv = g->meta()->inodes()[index];
  return inode_view(iv, iv.inode_v2_2(), g->meta());
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

  if (auto e = g_->meta()->dir_entries()) {
    ent = (*e)[ent].inode_num();
  }

  return ent;
}

} // namespace dwarfs

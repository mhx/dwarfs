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

#include <memory>

#include "dwarfs/chunkable.h"

namespace dwarfs {

class categorizer_manager;
class inode;
class mmif;
class single_inode_fragment;

class fragment_chunkable : public chunkable {
 public:
  fragment_chunkable(inode const& ino, single_inode_fragment& frag,
                     file_off_t offset, mmif& mm,
                     categorizer_manager const* catmgr);
  ~fragment_chunkable();

  file const* get_file() const override;
  size_t size() const override;
  std::string description() const override;
  std::span<uint8_t const> span() const override;
  void add_chunk(size_t block, size_t offset, size_t size) override;
  void release_until(size_t offset) override;

 private:
  inode const& ino_;
  single_inode_fragment& frag_;
  file_off_t offset_;
  mmif& mm_;
  categorizer_manager const* catmgr_;
};

} // namespace dwarfs

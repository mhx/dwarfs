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

#include <dwarfs/writer/internal/chunk_list.h>

namespace dwarfs::writer::internal {

class entry_storage;

class single_inode_fragment_view {
 public:
  single_inode_fragment_view(entry_storage& storage, inode_id id,
                             std::uint64_t index)
      : storage_{&storage}
      , id_{id}
      , index_{index} {}

  [[nodiscard]] fragment_category category() const;

  [[nodiscard]] file_size_t size() const;

  [[nodiscard]] auto chunks() const -> const_chunk_list;

  void add_chunk(size_t block, size_t offset, size_t size);

  void add_hole(file_size_t size);

  [[nodiscard]] bool chunks_are_consistent() const;

 private:
  entry_storage* storage_{nullptr};
  inode_id id_;
  std::uint64_t index_{0};
};

} // namespace dwarfs::writer::internal

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

#include <dwarfs/container/detail/index_based_iterator.h>
#include <dwarfs/writer/inode_fragments.h>

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

class inode_fragments_view {
 public:
  using size_type = std::size_t;
  using value_type = single_inode_fragment_view;
  using reference = single_inode_fragment_view;
  using const_reference = single_inode_fragment_view;
  using const_iterator = dwarfs::container::detail::index_based_const_iterator<
      inode_fragments_view>;

  inode_fragments_view(entry_storage& storage, inode_id id)
      : storage_{&storage}
      , id_{id} {}

  [[nodiscard]] auto size() const noexcept -> size_type;

  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  [[nodiscard]] auto
  operator[](size_type i) const -> single_inode_fragment_view {
    return single_inode_fragment_view{*storage_, id_, i};
  }

  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{this, 0};
  }

  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{this, size()};
  }

  [[nodiscard]] fragment_category get_single_category() const;

  [[nodiscard]] std::unordered_map<fragment_category, file_size_t>
  get_category_sizes() const;

 private:
  friend class dwarfs::container::detail::index_based_const_iterator<
      inode_fragments_view>;

  entry_storage* storage_{nullptr};
  inode_id id_;
};

} // namespace dwarfs::writer::internal

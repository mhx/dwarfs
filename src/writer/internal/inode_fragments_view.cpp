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

#include <numeric>

#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/inode_fragments_view.h>

namespace dwarfs::writer::internal {

fragment_category single_inode_fragment_view::category() const {
  return storage_->get_inode_fragment_category(id_, index_);
}

file_size_t single_inode_fragment_view::size() const {
  return storage_->get_inode_fragment_size(id_, index_);
}

auto single_inode_fragment_view::chunks() const -> const_chunk_list {
  return const_chunk_list{
      storage_->get_inode_fragment_packed_chunks(id_, index_)};
}

void single_inode_fragment_view::add_chunk(size_t block, size_t offset,
                                           size_t size) {
  storage_->inode_fragment_add_data_chunk(id_, index_, block, offset, size);
}

void single_inode_fragment_view::add_hole(file_size_t size) {
  storage_->inode_fragment_add_hole_chunk(id_, index_, size);
}

bool single_inode_fragment_view::chunks_are_consistent() const {
  // TODO: probably move this into entry_storage?

  auto const frag_size = size();
  auto const chks = chunks();

  if (frag_size > 0 && chks.empty()) {
    return false;
  }

  auto const total_chunks_len =
      std::accumulate(chks.begin(), chks.end(), file_size_t{0},
                      [](auto acc, auto const& c) { return acc + c.size(); });

  return total_chunks_len == frag_size;
}

auto inode_fragments_view::size() const noexcept -> size_type {
  return storage_->get_inode_fragment_count(id_);
}

fragment_category inode_fragments_view::get_single_category() const {
  assert(size() == 1);
  return this->operator[](0).category();
}

std::unordered_map<fragment_category, file_size_t>
inode_fragments_view::get_category_sizes() const {
  std::unordered_map<fragment_category, file_size_t> result;

  for (auto const& f : *this) {
    result[f.category()] += f.size();
  }

  return result;
}

} // namespace dwarfs::writer::internal

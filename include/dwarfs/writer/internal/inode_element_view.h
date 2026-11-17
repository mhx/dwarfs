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

#include <vector>

#include <dwarfs/writer/fragment_category.h>

#include <dwarfs/writer/internal/nilsimsa.h>
#include <dwarfs/writer/internal/similarity_ordering.h>
#include <dwarfs/writer/internal/sortable_inode_span.h>

namespace dwarfs::writer::internal {

class inode;

class inode_element_view final : public similarity_element_view {
 public:
  using index_type = sortable_inode_span::index_type;

  inode_element_view(sortable_inode_span& sp, index_type const& index,
                     fragment_category cat);

  bool order_less(size_t a, size_t b) const override;

  nilsimsa::hash_type const& get_bits(size_t i) const override;
  size_t release_hash_cache() override;

 private:
  sortable_inode_span& sp_;
  std::vector<nilsimsa::hash_type const*> hash_cache_;
};

} // namespace dwarfs::writer::internal

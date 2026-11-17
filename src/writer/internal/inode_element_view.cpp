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

#include <cassert>

#include <dwarfs/writer/internal/inode_element_view.h>
#include <dwarfs/writer/internal/sortable_inode_span.h>

namespace dwarfs::writer::internal {

inode_element_view::inode_element_view(sortable_inode_span& sp,
                                       index_type const& index,
                                       fragment_category cat)
    : sp_{sp} {
  hash_cache_.resize(sp_.raw_size());
  for (auto&& i : index) {
    hash_cache_[i] = sp_.raw_handle(i).nilsimsa_similarity_hash(cat);
  }
}

bool inode_element_view::order_less(size_t a, size_t b) const {
  auto const fa = sp_.raw_handle(a).any();
  auto const fb = sp_.raw_handle(b).any();
  auto sa = fa.size();
  auto sb = fb.size();
  return sa > sb || (sa == sb && fa.less_revpath(fb));
}

nilsimsa::hash_type const& inode_element_view::get_bits(size_t i) const {
  assert(i < hash_cache_.size());
  assert(hash_cache_[i] != nullptr);
  return *hash_cache_[i];
}

size_t inode_element_view::release_hash_cache() {
  auto const bytes =
      hash_cache_.capacity() * sizeof(nilsimsa::hash_type const*);
  decltype(hash_cache_){}.swap(hash_cache_);
  return bytes;
}

} // namespace dwarfs::writer::internal

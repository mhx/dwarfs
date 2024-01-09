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

#include <cassert>

#include <fmt/format.h>

#include "dwarfs/entry.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_element_view.h"

namespace dwarfs {

inode_element_view::inode_element_view(
    std::span<std::shared_ptr<inode> const> inodes,
    std::span<uint32_t const> index, fragment_category cat)
    : inodes_{inodes}
    , cat_{cat} {
  hash_cache_.resize(inodes_.size());
  for (auto i : index) {
    hash_cache_[i] = inodes_[i]->nilsimsa_similarity_hash(cat);
  }
}

bool inode_element_view::exists(size_t i) const {
  return !cat_ || inodes_[i]->has_category(*cat_);
}

size_t inode_element_view::size() const { return inodes_.size(); }

size_t inode_element_view::weight(size_t i) const {
  return inodes_[i]->any()->size();
}

bool inode_element_view::bitvec_less(size_t a, size_t b) const {
  assert(hash_cache_[a] != nullptr);
  assert(hash_cache_[b] != nullptr);
  auto const& ha = *hash_cache_[a];
  auto const& hb = *hash_cache_[b];
  if (ha < hb) {
    return true;
  }
  if (ha > hb) {
    return false;
  }
  return inodes_[a]->any()->less_revpath(*inodes_[b]->any());
}

bool inode_element_view::order_less(size_t a, size_t b) const {
  auto const& fa = *inodes_[a]->any();
  auto const& fb = *inodes_[b]->any();
  auto sa = fa.size();
  auto sb = fb.size();
  return sa > sb || (sa == sb && fa.less_revpath(fb));
}

bool inode_element_view::bits_equal(size_t a, size_t b) const {
  assert(hash_cache_[a] != nullptr);
  assert(hash_cache_[b] != nullptr);
  return *hash_cache_[a] == *hash_cache_[b];
}

std::string inode_element_view::description(size_t i) const {
  auto f = inodes_[i]->any();
  return fmt::format("{} [{}]", f->path_as_string(), f->size());
}

nilsimsa::hash_type const& inode_element_view::get_bits(size_t i) const {
  assert(hash_cache_[i] != nullptr);
  return *hash_cache_[i];
}

} // namespace dwarfs

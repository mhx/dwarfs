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

#include <optional>
#include <span>

#include "dwarfs/fragment_category.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/similarity_ordering.h"

namespace dwarfs {

class inode;

class inode_element_view
    : public basic_array_similarity_element_view<256, uint64_t> {
 public:
  inode_element_view() = default;

  inode_element_view(std::span<std::shared_ptr<inode> const> inodes,
                     std::span<uint32_t const> index, fragment_category cat);

  bool exists(size_t i) const override;
  size_t size() const override;
  size_t weight(size_t i) const override;
  bool bitvec_less(size_t a, size_t b) const override;
  bool order_less(size_t a, size_t b) const override;
  bool bits_equal(size_t a, size_t b) const override;

  std::string description(size_t i) const override;
  nilsimsa::hash_type const& get_bits(size_t i) const override;

 private:
  std::span<std::shared_ptr<inode> const> inodes_;
  std::vector<nilsimsa::hash_type const*> hash_cache_;
  std::optional<fragment_category> cat_;
};

} // namespace dwarfs

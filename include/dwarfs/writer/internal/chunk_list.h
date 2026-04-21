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

#include <dwarfs/writer/internal/chunk_ref.h>

namespace dwarfs::writer::internal {

class const_chunk_list {
 public:
  using size_type = packed_chunk_vector::size_type;
  using value_type = void;
  using reference = const_chunk_ref;
  using const_reference = const_chunk_ref;
  using iterator =
      dwarfs::container::detail::index_based_iterator<const_chunk_list>;
  using const_iterator =
      dwarfs::container::detail::index_based_const_iterator<const_chunk_list>;

  explicit const_chunk_list(packed_chunk_vector const& chunks)
      : chunks_{chunks} {}

  [[nodiscard]] auto size() const noexcept -> size_type {
    return chunks_.size();
  }

  [[nodiscard]] bool empty() const noexcept { return chunks_.empty(); }

  [[nodiscard]] auto operator[](size_type i) const -> const_chunk_ref {
    return const_chunk_ref{chunks_, i};
  }

  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{this, 0};
  }

  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{this, size()};
  }

 private:
  friend class dwarfs::container::detail::index_based_const_iterator<
      const_chunk_list>;

  packed_chunk_vector const& chunks_;
};

} // namespace dwarfs::writer::internal

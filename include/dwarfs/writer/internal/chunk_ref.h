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

#include <cassert>
#include <cstdint>
#include <tuple>

#include <dwarfs/container/compact_packed_int_vector.h>

namespace dwarfs::writer::internal {

enum class chunk_kind : std::uint64_t {
  data = 0,
  hole = 1,
};

static constexpr std::size_t kChunkBlockField = 0;
static constexpr std::size_t kChunkOffsetField = 1;
static constexpr std::size_t kChunkSizeField = 2;
static constexpr std::size_t kChunkKindField = 3;

using packed_chunk_tuple =
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, chunk_kind>;

using packed_chunk_vector =
    dwarfs::container::compact_auto_packed_int_vector<packed_chunk_tuple>;

class const_chunk_ref {
 public:
  using size_type = packed_chunk_vector::size_type;
  using block_type = std::uint64_t;
  using offset_type = std::uint64_t;
  using chunk_size_type = file_size_t;

  const_chunk_ref(packed_chunk_vector const& chunks, size_type index)
      : chunks_{chunks}
      , index_{index} {}

  bool is_hole() const { return kind() == chunk_kind::hole; }

  bool is_data() const { return !is_hole(); }

  block_type block() const {
    assert(is_data());
    return get<kChunkBlockField>(chunks_[index_]);
  }

  offset_type offset() const {
    assert(is_data());
    return get<kChunkOffsetField>(chunks_[index_]);
  }

  chunk_size_type size() const { return get<kChunkSizeField>(chunks_[index_]); }

 private:
  [[nodiscard]] auto kind() const -> chunk_kind {
    return get<kChunkKindField>(chunks_[index_]);
  }

  packed_chunk_vector const& chunks_;
  size_type index_;
};

} // namespace dwarfs::writer::internal

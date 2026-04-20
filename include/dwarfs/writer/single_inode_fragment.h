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
#include <dwarfs/metadata_defs.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/fragment_category.h>

namespace dwarfs::writer {

class single_inode_fragment {
 private:
  enum class chunk_kind : std::uint64_t {
    data = 0,
    hole = 1,
  };

  static constexpr std::size_t kBlockIndex = 0;
  static constexpr std::size_t kOffsetIndex = 1;
  static constexpr std::size_t kSizeIndex = 2;
  static constexpr std::size_t kKindIndex = 3;

  using packed_chunk_tuple =
      std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, chunk_kind>;

  using packed_chunk_vector =
      dwarfs::container::compact_auto_packed_int_vector<packed_chunk_tuple>;

 public:
  struct hole_tag {};
  static constexpr hole_tag hole{};

  using block_type = std::uint64_t;
  using offset_type = std::uint64_t;
  using chunk_size_type = file_size_t;

  class const_chunk_ref {
   public:
    using size_type = packed_chunk_vector::size_type;

    const_chunk_ref(packed_chunk_vector const& chunks, size_type index)
        : chunks_{chunks}
        , index_{index} {}

    bool is_hole() const { return kind() == chunk_kind::hole; }

    bool is_data() const { return !is_hole(); }

    block_type block() const {
      assert(is_data());
      return get<kBlockIndex>(chunks_[index_]);
    }

    offset_type offset() const {
      assert(is_data());
      return get<kOffsetIndex>(chunks_[index_]);
    }

    chunk_size_type size() const { return get<kSizeIndex>(chunks_[index_]); }

   private:
    [[nodiscard]] auto kind() const -> chunk_kind {
      return get<kKindIndex>(chunks_[index_]);
    }

    packed_chunk_vector const& chunks_;
    size_type index_;
  };

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

    [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
      return const_iterator{this, 0};
    }

    [[nodiscard]] auto cend() const noexcept -> const_iterator {
      return const_iterator{this, size()};
    }

   private:
    friend class dwarfs::container::detail::index_based_const_iterator<
        const_chunk_list>;

    packed_chunk_vector const& chunks_;
  };

  single_inode_fragment(fragment_category category, file_size_t length)
      : category_{category}
      , bits_{static_cast<uint64_t>(length)} {}

  single_inode_fragment(hole_tag, fragment_category category,
                        file_size_t length)
      : category_{category}
      , bits_{static_cast<uint64_t>(length) | kChunkBitsHoleBit} {}

  bool is_hole() const { return (bits_ & kChunkBitsHoleBit) != 0; }

  bool is_data() const { return !is_hole(); }

  fragment_category category() const { return category_; }

  file_size_t size() const { return bits_ & kChunkBitsSizeMask; }

  void add_chunk(size_t block, size_t offset, size_t size);

  void add_hole(file_size_t size);

  [[nodiscard]] auto chunks() const -> const_chunk_list {
    return const_chunk_list{chunks_};
  }

  void extend(file_size_t length) {
    bits_ = (this->size() + length) | (bits_ & kChunkBitsHoleBit);
  }

  bool chunks_are_consistent() const;

  std::size_t allocated_size_in_bytes() const;

 private:
  fragment_category category_;
  uint64_t bits_;
  packed_chunk_vector chunks_;
};

} // namespace dwarfs::writer

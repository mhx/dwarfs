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

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <dwarfs/container/packed_int_vector.h>
#include <dwarfs/writer/fragment_order_options.h>

#include <dwarfs/internal/worker_group_fwd.h>
#include <dwarfs/writer/internal/receiver.h>
#include <dwarfs/writer/internal/sortable_inode_span.h>

namespace dwarfs {

class logger;

namespace writer::internal {

class progress;

// Nilsimsa is the only similarity-ordering backend and uses 256-bit hashes.
class similarity_element_view {
 public:
  using bitvec_type = std::array<uint64_t, 4>;
  virtual ~similarity_element_view() = default;

  // Canonical order: determines starting point, tie-breaking and the
  // order of elements with identical hashes.
  virtual bool order_less(size_t a, size_t b) const = 0;
  virtual bitvec_type const& get_bits(size_t i) const = 0;

  // Release any internal cache. Only `order_less()` may be called after this.
  virtual size_t release_hash_cache() = 0;
};

struct similarity_ordering_options {
  std::string context;
  progress* prog{nullptr};
  size_t passes{fragment_order_options::kDefaultNilsimsaPasses};
  size_t window{fragment_order_options::kDefaultNilsimsaWindow};
  size_t offset_bits{fragment_order_options::kDefaultNilsimsaOffsetBits};
};

// Order canonical ranks using their caller-owned fingerprints.
struct nilsimsa_ordering_result {
  container::packed_int_vector<uint32_t> order;
  uint64_t comparisons{0};
  uint64_t total_distance{0};
  size_t projection_bytes{0};
  size_t peak_scratch_bytes{0};
  unsigned rank_bits{0};
  unsigned offset_bits{0};
  unsigned key_bits{0};
  unsigned prefix_bits{0};
  std::chrono::nanoseconds build_time{};
  std::chrono::nanoseconds walk_time{};
};

nilsimsa_ordering_result order_nilsimsa_hashes(
    std::span<std::array<uint64_t, 4> const* const> hashes, size_t passes,
    size_t window,
    size_t offset_bits = fragment_order_options::kDefaultNilsimsaOffsetBits,
    progress* prog = nullptr, std::string_view context = {});

class similarity_ordering {
 public:
  using index_value_type = uint32_t;

  similarity_ordering(logger& lgr, dwarfs::internal::worker_group& wg,
                      similarity_ordering_options const& opts);

  void order_nilsimsa(similarity_element_view& ev,
                      receiver<sortable_inode_span::index_type> rec,
                      sortable_inode_span::index_type index) const {
    impl_->order_nilsimsa(ev, std::move(rec), std::move(index));
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    order_nilsimsa(similarity_element_view& ev,
                   receiver<sortable_inode_span::index_type> rec,
                   sortable_inode_span::index_type index) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer::internal

} // namespace dwarfs

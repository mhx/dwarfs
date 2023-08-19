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

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "dwarfs/receiver.h"

namespace dwarfs {

class logger;
class progress;
class worker_group;

class similarity_element_view {
 public:
  ~similarity_element_view() = default;

  virtual bool exists(size_t i) const = 0;
  virtual size_t size() const = 0;
  virtual size_t weight(size_t i) const = 0;
  virtual bool bitvec_less(size_t a, size_t b) const = 0;
  virtual bool order_less(size_t a, size_t b) const = 0;
  virtual bool bits_equal(size_t a, size_t b) const = 0;
  virtual std::string description(size_t i) const = 0;
};

template <size_t Bits, typename BitsType>
class basic_array_similarity_element_view : public similarity_element_view {
 public:
  static_assert(Bits % (8 * sizeof(BitsType)) == 0);
  static constexpr size_t const bitvec_size = Bits / (8 * sizeof(BitsType));
  using bitvec_type = std::array<BitsType, bitvec_size>;

  virtual bitvec_type const& get_bits(size_t i) const = 0;
};

struct similarity_ordering_options {
  std::string context;
  size_t max_children{256};
  size_t max_cluster_size{256};
};

class similarity_ordering {
 public:
  using index_value_type = uint32_t;

  similarity_ordering(logger& lgr, progress& prog, worker_group& wg,
                      similarity_ordering_options const& opts);

  void order_nilsimsa(
      basic_array_similarity_element_view<256, uint64_t> const& ev,
      receiver<std::vector<index_value_type>> rec,
      std::optional<std::vector<index_value_type>> index = std::nullopt) const {
    impl_->order_nilsimsa(ev, std::move(rec), std::move(index));
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void order_nilsimsa(
        basic_array_similarity_element_view<256, uint64_t> const& ev,
        receiver<std::vector<index_value_type>> rec,
        std::optional<std::vector<index_value_type>> index) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

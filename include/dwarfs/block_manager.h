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

#include <mutex>
#include <optional>
#include <vector>

#include "dwarfs/fragment_category.h"
#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

class block_manager {
 public:
  using chunk_type = thrift::metadata::chunk;

  size_t get_logical_block() const;
  void set_written_block(size_t logical_block, size_t written_block,
                         fragment_category::value_type category);
  void map_logical_blocks(std::vector<chunk_type>& vec);
  std::vector<fragment_category::value_type>
  get_written_block_categories() const;

 private:
  std::mutex mutable mx_;
  size_t mutable num_blocks_{0};
  std::vector<std::optional<std::pair<size_t, fragment_category::value_type>>>
      block_map_;
};

} // namespace dwarfs

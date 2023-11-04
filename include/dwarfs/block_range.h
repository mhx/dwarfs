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

#include <cstdint>
#include <memory>
#include <span>

namespace dwarfs {

class cached_block;

class block_range {
 public:
  block_range(void) = default;     // This is required for MSVC toolset v142 or less
  block_range(uint8_t const* data, size_t offset, size_t size);
  block_range(std::shared_ptr<cached_block const> block, size_t offset,
              size_t size);

  auto data() const { return span_.data(); }
  auto begin() const { return span_.begin(); }
  auto end() const { return span_.end(); }
  auto size() const { return span_.size(); }

 private:
  std::span<uint8_t const> span_;
  std::shared_ptr<cached_block const> block_;
};

} // namespace dwarfs

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

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace dwarfs {

class logger;
class mmif;

namespace internal {

class fs_section;

}

namespace reader::internal {

class cached_block {
 public:
  static std::unique_ptr<cached_block>
  create(logger& lgr, dwarfs::internal::fs_section const& b,
         std::shared_ptr<mmif> mm, bool release, bool disable_integrity_check);

  virtual ~cached_block() = default;

  virtual size_t range_end() const = 0;
  virtual const uint8_t* data() const = 0;
  virtual void decompress_until(size_t end) = 0;
  virtual size_t uncompressed_size() const = 0;
  virtual void touch() = 0;
  virtual bool
  last_used_before(std::chrono::steady_clock::time_point tp) const = 0;
  virtual bool any_pages_swapped_out(std::vector<uint8_t>& tmp) const = 0;
};

} // namespace reader::internal

} // namespace dwarfs

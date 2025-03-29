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

#include <cstring>

#include <dwarfs/byte_buffer.h>

namespace dwarfs::detail {

std::strong_ordering
compare_spans(std::span<uint8_t const> lhs, std::span<uint8_t const> rhs) {
  auto const cmp =
      std::memcmp(lhs.data(), rhs.data(), std::min(lhs.size(), rhs.size()));
  return cmp == 0 ? lhs.size() <=> rhs.size() : cmp <=> 0;
}

} // namespace dwarfs::detail

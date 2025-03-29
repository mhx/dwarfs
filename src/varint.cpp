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

#include <folly/Varint.h>

#include <dwarfs/varint.h>

namespace dwarfs {

auto varint::encode(value_type value, uint8_t* buffer) -> size_t {
  return folly::encodeVarint(value, buffer);
}

auto varint::decode(std::span<uint8_t const>& buffer) -> value_type {
  folly::ByteRange range(buffer.data(), buffer.size());
  auto value = folly::decodeVarint(range);
  buffer = {range.data(), range.size()};
  return value;
}

} // namespace dwarfs

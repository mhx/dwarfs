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

namespace dwarfs {

constexpr uint64_t kChunkBitsSizeMask{UINT64_C(0x7FFF'FFFF'FFFF'FFFF)};
constexpr uint64_t kChunkBitsHoleBit{UINT64_C(0x8000'0000'0000'0000)};
constexpr uint32_t kChunkOffsetIsLargeHole{UINT32_C(0xFFFF'FFFF)};

} // namespace dwarfs

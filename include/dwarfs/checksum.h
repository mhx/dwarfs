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

#include <cstddef>
#include <stdexcept>

namespace dwarfs {

enum class checksum_algorithm {
  SHA1,
  SHA2_512_256,
  XXH3_64,
};

constexpr size_t checksum_size(checksum_algorithm alg) {
  switch (alg) {
  case checksum_algorithm::SHA1:
    return 20;
  case checksum_algorithm::SHA2_512_256:
    return 32;
  case checksum_algorithm::XXH3_64:
    return 8;
  }
  throw std::logic_error("unknown algorithm");
}

bool compute_checksum(checksum_algorithm alg, void const* data, size_t size,
                      void* result);
bool verify_checksum(checksum_algorithm alg, void const* data, size_t size,
                     const void* checksum);

} // namespace dwarfs

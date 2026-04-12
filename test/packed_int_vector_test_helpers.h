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

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/compact_packed_int_vector.h>
#include <dwarfs/internal/packed_int_vector.h>
#include <dwarfs/internal/segmented_packed_int_vector.h>

namespace dwarfs::test {

template <internal::integer_packable T>
struct packed_int_vector_type_selector {
  using type = internal::packed_int_vector<T>;
  using auto_type = internal::auto_packed_int_vector<T>;
};

template <internal::integer_packable T>
struct compact_packed_int_vector_type_selector {
  using type = internal::compact_packed_int_vector<T>;
  using auto_type = internal::compact_auto_packed_int_vector<T>;
};

} // namespace dwarfs::test

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <dwarfs/internal/basic_packed_int_vector.h>
#include <dwarfs/internal/detail/heap_only_packed_vector_policy.h>
#include <dwarfs/internal/detail/packed_vector_layout_heap_only.h>

namespace dwarfs::internal {

/**
 * Packed integer vector using heap storage only.
 *
 * This is the regular packed-vector variant. Non-empty vectors use heap
 * storage, while empty vectors require no allocation. The object itself stores
 * only the current bit width and a pointer to the heap block, making it compact
 * when embedded in other containers.
 *
 * This variant has no policy-imposed size limit beyond `std::size_t` and is
 * therefore suitable as the general-purpose packed integer vector.
 */
template <integral_but_not_bool T>
using packed_int_vector =
    basic_packed_int_vector<T, packed_vector_bit_width_strategy::fixed,
                            detail::heap_only_packed_vector_policy>;

/**
 * Heap-backed packed integer vector with automatic bit-width.
 *
 * Like `packed_int_vector`, but grows the element bit width automatically as
 * needed to represent newly inserted or assigned values.
 */
template <integral_but_not_bool T>
using auto_packed_int_vector =
    basic_packed_int_vector<T, packed_vector_bit_width_strategy::automatic,
                            detail::heap_only_packed_vector_policy>;

} // namespace dwarfs::internal

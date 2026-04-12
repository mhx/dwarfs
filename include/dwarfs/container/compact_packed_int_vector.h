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

#include <dwarfs/container/basic_packed_int_vector.h>
#include <dwarfs/container/detail/compact_packed_vector_policy.h>
#include <dwarfs/container/detail/packed_vector_layout_inline_with_heap.h>

namespace dwarfs::container {

/**
 * Packed integer vector with inline storage for small vectors.
 *
 * This variant stores small vectors directly inside the object without heap
 * allocation. Inline storage uses the same memory that would otherwise hold
 * metadata and the heap pointer. Once the requested bit width and logical
 * size no longer fit inline, storage transparently moves to the shared
 * heap-backed representation.
 *
 * Inline storage capacity depends on:
 * - the target architecture (size of `std::size_t` and heap pointer)
 * - the current element bit width
 *
 * On 64-bit architectures, the size of the object itself is 16 bytes, on
 * 32-bit architectures it is 8 bytes.
 *
 * On 64-bit architectures, a packed vector can store up to 31 elements inline
 * without allocating any memory, as long as these elements fit into 3 bits
 * each. More realisticly, you could also store up to 14 8-bit elements inline.
 *
 * The maximum number of elements that fit inline for a given bit width can be
 * queried via `inline_capacity_for_bits(bits)`. For `bits == 0`, inline storage
 * can hold up to `max_inline_size` logical zero elements without allocating.
 *
 * Once inline capacity is exceeded, this variant behaves like the regular
 * heap-backed packed vector and supports the same maximum size as
 * `packed_int_vector`.
 *
 * Due to the extra complexity of managing inline storage, this variant is
 * potentially slower and will likely generate more code than the regular
 * heap-backed variant.
 */
template <integer_packable T>
using compact_packed_int_vector =
    basic_packed_int_vector<T, packed_vector_bit_width_strategy::fixed,
                            detail::compact_packed_vector_policy>;

/**
 * Compact packed integer vector with inline storage and automatic bit-width.
 *
 * Like `compact_packed_int_vector`, but grows the element bit width
 * automatically as needed to represent newly inserted or assigned values.
 */
template <integer_packable T>
using compact_auto_packed_int_vector =
    basic_packed_int_vector<T, packed_vector_bit_width_strategy::automatic,
                            detail::compact_packed_vector_policy>;

} // namespace dwarfs::container

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

#if __has_include(<dwarfs/config.h>)
#include <dwarfs/config.h>
#elif __has_include(<include/dwarfs/config.h>)
#include <include/dwarfs/config.h>
#endif

#ifdef DWARFS_HAVE_ABSEIL

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <absl/container/node_hash_map.h>
#include <absl/container/node_hash_set.h>

namespace dwarfs::internal {

// absl::flat_hash_map<>::slot_type is private, so we need to define our own,
// propagating the original template arguments.
template <
    class K, class V,
    class Hash =
        typename absl::container_internal::FlatHashMapPolicy<K, V>::DefaultHash,
    class Eq =
        typename absl::container_internal::FlatHashMapPolicy<K, V>::DefaultEq,
    class Allocator = typename absl::container_internal::FlatHashMapPolicy<
        K, V>::DefaultAlloc>
class ABSL_ATTRIBUTE_OWNER flat_hash_map
    : public absl::flat_hash_map<K, V, Hash, Eq, Allocator> {
 public:
  using slot_type =
      typename absl::container_internal::FlatHashMapPolicy<K, V>::slot_type;
};

using absl::flat_hash_set;
using absl::node_hash_map;
using absl::node_hash_set;

} // namespace dwarfs::internal

#else

#include <parallel_hashmap/phmap.h>

namespace dwarfs::internal {

using phmap::flat_hash_map;
using phmap::flat_hash_set;
using phmap::node_hash_map;
using phmap::node_hash_set;

} // namespace dwarfs::internal

#endif

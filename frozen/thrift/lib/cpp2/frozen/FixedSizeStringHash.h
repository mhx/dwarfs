/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#pragma once

#include <cassert>
#include <cstring>
#include <span>
#include <utility>

#include <thrift/lib/cpp2/frozen/Traits.h>

namespace apache::thrift::frozen {

template <size_t kSize, typename RangeType>
struct FixedSizeStringHash {
  static uint64_t hash(const RangeType& value) {
    assert(value.size() == kSize);
    if constexpr (kSize <= 8) {
      uint64_t tmp = 0;
      std::memcpy(&tmp, value.data(), kSize);
      return std::hash<uint64_t>()(tmp);
    } else {
      return XXH3_64bits(value.data(), value.size());
    }
  }
};

} // namespace apache::thrift::frozen

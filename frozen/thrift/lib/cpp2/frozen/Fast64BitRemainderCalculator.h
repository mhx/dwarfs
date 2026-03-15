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
#include <boost/config.hpp>

namespace apache::thrift::frozen::detail {
class Fast64BitRemainderCalculator {
#ifdef BOOST_HAS_INT128
  using uint128_t = boost::uint128_type;

 public:
  Fast64BitRemainderCalculator() = default;
  explicit Fast64BitRemainderCalculator(uint64_t divisor)
      : fastRemainderConstant_(divisor ? (~uint128_t(0) / divisor + 1) : 0) {
#ifndef NDEBUG
    divisor_ = divisor;
#endif
  }

  size_t remainder(size_t lhs, size_t rhs) const {
    const uint128_t lowBits = fastRemainderConstant_ * lhs;
    auto result = mul128_u64(lowBits, rhs);
    assert(rhs == divisor_);
    assert(result == lhs % rhs);
    return result;
  }

 private:
  static uint64_t mul128_u64(uint128_t lowbits, uint64_t d) {
    uint128_t bottom = ((lowbits & 0xFFFFFFFFFFFFFFFFUL) * d) >> 64;
    uint128_t top = (lowbits >> 64) * d;
    return static_cast<uint64_t>((bottom + top) >> 64);
  }
  uint128_t fastRemainderConstant_ = 0;
#ifndef NDEBUG
  size_t divisor_ = 0;
#endif
#else
 public:
  Fast64BitRemainderCalculator() = default;
  explicit Fast64BitRemainderCalculator(size_t) {}

  auto remainder(size_t lhs, size_t rhs) const { return lhs % rhs; }
#endif
};
} // namespace apache::thrift::frozen::detail

/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>

namespace apache::thrift::frozen {

TEST(FrozenVectorTypes, Unpacked) {
  VectorUnpacked<int> viu{2, 3, 5, 7, 9, 11, 13, 17};
  std::vector<int> vip = viu;
  EXPECT_LT(frozenSize(vip), frozenSize(viu));
  auto fiu = freeze(viu);
  EXPECT_EQ(fiu[2], 5);
  EXPECT_EQ(fiu.end()[-1], 17);
  const int* raw = fiu.data();
  EXPECT_EQ(raw[3], 7);
}
} // namespace apache::thrift::frozen

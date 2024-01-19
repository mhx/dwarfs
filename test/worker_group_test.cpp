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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/worker_group.h"

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

TEST(worker_group_test, set_thread_affinity_env) {
  test::test_logger lgr;
  test::os_access_mock os;

  os.setenv("DWARFS_WORKER_GROUP_AFFINITY", "lemon=0,1:lime=2,3");

  os.set_affinity_calls.clear();
  worker_group wg_lemon(lgr, os, "lemon", 2);
  ASSERT_EQ(2, os.set_affinity_calls.size());
  EXPECT_EQ(std::vector<int>({0, 1}), std::get<1>(os.set_affinity_calls[0]));

  os.set_affinity_calls.clear();
  worker_group wg_lime(lgr, os, "lime", 3);
  ASSERT_EQ(3, os.set_affinity_calls.size());
  EXPECT_EQ(std::vector<int>({2, 3}), std::get<1>(os.set_affinity_calls[0]));

  os.set_affinity_calls.clear();
  worker_group wg_apple(lgr, os, "apple", 1);
  EXPECT_EQ(0, os.set_affinity_calls.size());
}

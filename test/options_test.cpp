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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/mlock_mode.h>

using namespace dwarfs;
using namespace dwarfs::reader;

TEST(options, fsinfo_features) {
  fsinfo_features ff;

  EXPECT_EQ(ff.to_string(), "");

  EXPECT_NO_THROW(ff |= fsinfo_features::parse("frozen_layout,history"));

  EXPECT_TRUE(ff.has(fsinfo_feature::history));
  EXPECT_TRUE(ff & fsinfo_feature::frozen_layout);

  EXPECT_FALSE(ff.has(fsinfo_feature::frozen_analysis));
  EXPECT_FALSE(ff & fsinfo_feature::version);

  EXPECT_EQ(ff.to_string(), "history,frozen_layout");

  ff.clear(fsinfo_feature::history);

  EXPECT_FALSE(ff & fsinfo_feature::history);
  EXPECT_TRUE(ff & fsinfo_feature::frozen_layout);
  EXPECT_EQ(ff.to_string(), "frozen_layout");

  ff.reset();

  EXPECT_FALSE(ff & fsinfo_feature::frozen_layout);
  EXPECT_EQ(ff.to_string(), "");

  EXPECT_THAT([]() { fsinfo_features::parse("history,whatever"); },
              testing::ThrowsMessage<runtime_error>(
                  testing::HasSubstr("invalid feature: \"whatever\"")));

  EXPECT_THAT([]() { fsinfo_features::parse("frozen_layout,history,x"); },
              testing::ThrowsMessage<runtime_error>(
                  testing::HasSubstr("invalid feature: \"x\"")));
}

TEST(options, parse_image_offset) {
  EXPECT_EQ(0, parse_image_offset("0"));
  EXPECT_EQ(1, parse_image_offset("1"));
  EXPECT_EQ(1024, parse_image_offset("1024"));
  EXPECT_EQ(filesystem_options::IMAGE_OFFSET_AUTO, parse_image_offset("auto"));
  EXPECT_THAT([] { parse_image_offset("-1"); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("image offset must be positive")));
  EXPECT_THAT([] { parse_image_offset("asd"); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("failed to parse image offset")));
}

TEST(options, parse_mlock_mode) {
  EXPECT_EQ(mlock_mode::NONE, parse_mlock_mode("none"));
  EXPECT_EQ(mlock_mode::TRY, parse_mlock_mode("try"));
  EXPECT_EQ(mlock_mode::MUST, parse_mlock_mode("must"));
  EXPECT_THAT([] { parse_mlock_mode("invalid"); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("invalid lock mode: invalid")));
}

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

#include <map>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/render_manpage.h"

using namespace dwarfs;

namespace {

std::map<std::string, manpage::document> const docs = {
    {"mkdwarfs", manpage::get_mkdwarfs_manpage()},
    {"dwarfs", manpage::get_dwarfs_manpage()},
    {"dwarfsck", manpage::get_dwarfsck_manpage()},
    {"dwarfsextract", manpage::get_dwarfsextract_manpage()},
};

}

class manpage_render_test
    : public ::testing::TestWithParam<std::tuple<std::string, bool>> {};

TEST_P(manpage_render_test, basic) {
  auto [name, color] = GetParam();
  auto doc = docs.at(name);
  for (size_t width = 20; width <= 200; width += 1) {
    auto out = render_manpage(doc, width, color);
    EXPECT_GT(out.size(), 1000);
    EXPECT_THAT(out, ::testing::HasSubstr(name));
    EXPECT_THAT(out, ::testing::HasSubstr("SYNOPSIS"));
    EXPECT_THAT(out, ::testing::HasSubstr("DESCRIPTION"));
    EXPECT_THAT(out, ::testing::HasSubstr("AUTHOR"));
    EXPECT_THAT(out, ::testing::HasSubstr("COPYRIGHT"));
  }
}

INSTANTIATE_TEST_SUITE_P(
    dwarfs, manpage_render_test,
    ::testing::Combine(::testing::Values("mkdwarfs", "dwarfs", "dwarfsck",
                                         "dwarfsextract"),
                       ::testing::Bool()));

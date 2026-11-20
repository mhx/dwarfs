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

#include <array>
#include <string_view>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <dwarfs/internal/unicode_case_folding.h>

using namespace dwarfs::internal;

namespace {

using namespace std::string_view_literals;

constexpr std::array kCaseFoldData{
#include "unicode_case_folding_test_data.inc"
};

} // namespace

TEST(unicode_test, case_folding) {
  for (auto const& [orig, folded] : kCaseFoldData) {
    std::string_view orig_sv(reinterpret_cast<char const*>(orig.data()),
                             orig.size());
    std::string_view folded_sv(reinterpret_cast<char const*>(folded.data()),
                               folded.size());
    EXPECT_TRUE(utf8::is_valid(orig_sv));
    EXPECT_TRUE(utf8::is_valid(folded_sv));
    EXPECT_NE(folded_sv, orig_sv);
    auto folded_result = utf8_case_fold(orig_sv);
    auto folded_unchecked_result = utf8_case_fold_unchecked(orig_sv);
    EXPECT_EQ(folded_sv, folded_result);
    EXPECT_EQ(folded_sv, folded_unchecked_result);
  }
}

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

#include <array>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/compute_fs_digests.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>

#include "test_helpers.h"
#include "test_logger.h"

namespace {

namespace fs = std::filesystem;
using namespace dwarfs;
using namespace std::string_view_literals;

fs::path const test_dir = fs::path(TEST_DATA_DIR).make_preferred();

struct reference_image {
  fs::path image;
  std::string_view attr;
  std::string_view tree;
  std::optional<fs_source_os> hint{};

  friend std::ostream&
  operator<<(std::ostream& os, reference_image const& test) {
    return os << test.image;
  }
};

// v0.2.0 and v0.2.3 have *slightly* different atime values, so the attr
// digest is different, but the tree digest is the same
constexpr auto compat_v0_2_tree =
    "f8f1aace727e176260946e265bcf1d5f833f3a441a808c6c4712d443228abc2a"sv;

constexpr auto compat_v0_3_attr =
    "82180dd3c004af5efa49f43a841c4b0cf999e890ed04834651fec027fbd36e86"sv;
constexpr auto compat_v0_3_tree =
    "e33607896d75f1bb43f416d343f2eac118fbaaf4c577480ff46183029d8884bb"sv;

constexpr auto compat_v0_5_attr =
    "e30a6dc07d9704f7e956e51fde28cd61d1da195cb19808c6301299da2cc7cc24"sv;
constexpr auto compat_v0_5_tree =
    "e33607896d75f1bb43f416d343f2eac118fbaaf4c577480ff46183029d8884bb"sv;

std::array const reference_images{
    reference_image{
        .image = test_dir / "data.dwarfs",
        .attr =
            "d165f489d930623a760fd9f10e2b006d802f6e90dacabbfe4cc7f90655074a2d"sv,
        .tree =
            "64b31d655c2bf46d4b2fa3c969463d1e6f133906d856dc725865a1a4487e2477"sv,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.2.0.dwarfs",
        .attr =
            "0a32ea2d01aa67dcfe21b8c0e5169e7a4fd2b97d73593d842517da3c3b6c93e8"sv,
        .tree = compat_v0_2_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.2.3.dwarfs",
        .attr =
            "86381ca9375c046a1d73b8b6826da002d43778969cabc9bae8898a4e42669fe6"sv,
        .tree = compat_v0_2_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.3.0.dwarfs",
        .attr = compat_v0_3_attr,
        .tree = compat_v0_3_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.4.0.dwarfs",
        .attr = compat_v0_3_attr,
        .tree = compat_v0_3_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.4.1.dwarfs",
        .attr = compat_v0_3_attr,
        .tree = compat_v0_3_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.5.6.dwarfs",
        .attr = compat_v0_5_attr,
        .tree = compat_v0_5_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.6.2.dwarfs",
        .attr = compat_v0_5_attr,
        .tree = compat_v0_5_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.7.5.dwarfs",
        .attr = compat_v0_5_attr,
        .tree = compat_v0_5_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.8.0.dwarfs",
        .attr = compat_v0_5_attr,
        .tree = compat_v0_5_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "compat" / "compat-v0.9.10.dwarfs",
        .attr = compat_v0_5_attr,
        .tree = compat_v0_5_tree,
        .hint = fs_source_os::os_linux,
    },
    reference_image{
        .image = test_dir / "random" / "random-sparse.dwarfs",
        .attr =
            "39ec09bb8149110d1bc15ea9abefa46c544b70c4337b8333aa7433a76361ee07"sv,
        .tree =
            "4e2d15400bb63bbd526ea612166885f24da73ae8286b619e8a0c2c716a205d42"sv,
    },
};

class reference_test : public ::testing::TestWithParam<reference_image> {
 protected:
  void SetUp() override {
    auto const& test = GetParam();
    auto mm = os_access_generic().open_file(test.image);
    reader::filesystem_options opts;
    opts.metadata.source_os_hint = test.hint;
    fs = reader::filesystem_v2{lgr, os, std::move(mm), opts};
  }

  test::os_access_mock os;
  test::test_logger lgr;
  reader::filesystem_v2 fs;
};

} // namespace

TEST_P(reference_test, attr_digest) {
  auto const& test = GetParam();
  auto const digests = reader::compute_filesystem_digests(lgr, os, fs, {});
  EXPECT_EQ(digests.attr_digest.hex(), test.attr);
  EXPECT_FALSE(digests.tree_digest.has_value());
}

TEST_P(reference_test, all_digests) {
  auto const& test = GetParam();
  auto const digests = reader::compute_filesystem_digests(
      lgr, os, fs, {.compute_tree_digest = true});
  EXPECT_EQ(digests.attr_digest.hex(), test.attr);
  EXPECT_EQ(digests.tree_digest.hex(), test.tree);
}

INSTANTIATE_TEST_SUITE_P(compute_fs_digests, reference_test,
                         ::testing::ValuesIn(reference_images));

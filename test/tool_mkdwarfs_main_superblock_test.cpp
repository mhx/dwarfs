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

#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/reader/compute_fs_digests.h>
#include <dwarfs/superblock_editor.h>

#include "filter_test_data.h"
#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace {

constexpr inline filesystem_version FS_VERSION_ACCEPTED{MAJOR_VERSION,
                                                        MINOR_VERSION_ACCEPTED};

struct build_step {
  std::vector<std::string> args;
  bool legacy_image{false};
  bool has_superblock{true};
  bool has_uuid{true};
  bool has_size{true};
  std::uint64_t size_alignment{1};
  std::string_view label{};
  bool has_attr_digest{true};
  bool has_tree_digest{true};

  friend std::ostream& operator<<(std::ostream& os, build_step const& test) {
    os << fmt::format("args={}", fmt::join(test.args, ","));
    if (test.legacy_image) {
      os << "[legacy]";
    }
    return os;
  }
};

struct rebuild_step {
  std::vector<std::string> args;
  bool legacy_image{false};
  bool has_superblock{true};
  std::optional<bool> has_uuid{};
  bool has_size{true};
  std::optional<std::uint64_t> size_alignment{};
  std::optional<std::string_view> label{};
  std::optional<bool> has_attr_digest{};
  std::optional<bool> has_tree_digest{};
  std::optional<std::string_view> attr_digest{};

  friend std::ostream& operator<<(std::ostream& os, rebuild_step const& test) {
    os << fmt::format("args={}", fmt::join(test.args, ","));
    if (test.legacy_image) {
      os << "[legacy]";
    }
    return os;
  }
};

constexpr std::string_view kUnicodeLabel{"我爱你/☀️ Sun/Γειά σας/مرحبًا/⚽️"};

constexpr std::string_view kAttrDigest{
    "779d7614bdbf825bda47f7807d57c3bdcf97766ad8c2bd2488289c2310c4514a"};
constexpr std::string_view kAttrDigestNorm{
    "46bc5c8f5e9b4ece7a26bcaca1aa52327cd14abf1bc3d4920697a4d8340b3bc2"};
constexpr std::string_view kTreeDigest{
    "01786e7be69d7460976bb7d721a3a170019691865bdba2a8e5954287025567ea"};

std::vector<build_step> build_steps{
    {
        .args = {},
    },
    {
        .args = {},
        .legacy_image = true,
        .has_superblock = false,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--image-size-alignment=512"},
        .legacy_image = true,
        .has_superblock = false,
        .size_alignment = 512,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--label=something"},
        .label = "something",
    },
    {
        .args = {"--no-superblock"},
        .has_superblock = false,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--no-superblock-init"},
        .has_size = false,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--no-superblock-init", "--image-size-alignment=512"},
        .has_size = false,
        .size_alignment = 512,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--no-superblock-digests"},
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--no-superblock-tree-digest"},
        .has_tree_digest = false,
    },
    {
        .args =
            {
                "--no-superblock-init",
                "--uuid=nil",
            },
        .has_uuid = false,
        .has_size = false,
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
};

std::vector<rebuild_step> rebuild_steps{
    {
        .args = {"--recompress=none"},
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--recompress=none", "--image-size-alignment=4096"},
        .size_alignment = 4096,
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--recompress=none", "--image-size-alignment=2048"},
        .legacy_image = true,
        .has_superblock = false,
        .size_alignment = 2048,
    },
    {
        .args = {"--recompress=none", "--no-superblock"},
        .has_superblock = false,
    },
    {
        .args = {"--rebuild-metadata"},
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--label", std::string{kUnicodeLabel}},
        .label = kUnicodeLabel,
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--chmod=norm"},
        .has_attr_digest = true,
        .has_tree_digest = true,
        .attr_digest = kAttrDigestNorm,
    },
    {
        .args = {"--recompress=none", "--no-superblock-init"},
        .has_size = false,
    },
    {
        .args = {"--recompress=none", "--no-superblock-digests"},
    },
    {
        .args = {"--recompress=none", "--no-superblock-tree-digest"},
        .has_attr_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--no-superblock-init"},
        .has_size = false,
        .has_attr_digest = false,
    },
    {
        .args = {"--rebuild-metadata", "--no-superblock-digests"},
        .has_attr_digest = false,
    },
    {
        .args = {"--rebuild-metadata", "--no-superblock-tree-digest"},
        .has_attr_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--chmod=norm",
                 "--no-superblock-digests"},
        .has_attr_digest = false,
    },
};

filesystem_digests
compute_digests(mkdwarfs_tester const& t, reader::filesystem_v2 const& fs,
                bool with_tree_digest) {
  return reader::compute_filesystem_digests(
      *t.lgr, *t.os, fs, {.compute_tree_digest = with_tree_digest});
}

class write_superblock_test
    : public ::testing::TestWithParam<
          std::tuple<build_step, bool, rebuild_step, bool>> {};

} // namespace

TEST_P(write_superblock_test, write_superblock) {
  auto const [build, build_has_header, rebuild, rebuild_has_header] =
      GetParam();
  auto const header = loremipsum(321);

  mkdwarfs_tester t;

  std::vector<std::string> args = {"-i", "/", "-o", "test.dwarfs", "-l1"};
  args.insert(args.end(), build.args.begin(), build.args.end());

  if (!build.legacy_image) {
    args.push_back("--no-backwards-compat");
  }

  if (build_has_header) {
    t.fa->set_file("lorem", header);
    args.push_back("--header=lorem");
  }

  t.fa->sync_files_to(*t.os);

  ASSERT_EQ(0, t.run(args)) << t.err();

  auto image = t.fa->get_file("test.dwarfs");
  ASSERT_TRUE(image) << "test.dwarfs not created";

  {
    superblock_editor ed;
    std::istringstream iss(*image);

    if (build_has_header) {
      EXPECT_THAT(*image, testing::StartsWith(header));
      iss.seekg(header.size(), std::ios::beg);
    }

    bool const has_any_digest = build.has_attr_digest || build.has_tree_digest;

    if (build.has_superblock) {
      ASSERT_NO_THROW(ed.read(iss));

      EXPECT_EQ(ed.major_version(), 1);
      EXPECT_EQ(ed.minor_version(), 1);

      EXPECT_EQ(ed.fs_size_alignment(), build.size_alignment);

      auto const fs_size = ed.fs_size();
      EXPECT_EQ(fs_size.has_value(), build.has_size);

      if (build.has_size) {
        EXPECT_EQ(fs_size.value(), image->size() - ed.image_offset());
      }

      auto const uuid = ed.fs_uuid();
      EXPECT_EQ(uuid.has_value(), build.has_uuid);

      EXPECT_EQ(ed.fs_label(), build.label);

      auto const expected_algo = has_any_digest
                                     ? digest_algorithm::BLAKE3_256
                                     : digest_algorithm::UNINITIALIZED;
      EXPECT_EQ(ed.digest_algo(), expected_algo);
      EXPECT_EQ(ed.digest_scheme_version(), has_any_digest ? 1 : 0);

      auto const attr_dig = ed.attr_digest();
      auto const tree_dig = ed.tree_digest();

      EXPECT_EQ(attr_dig.has_value(), build.has_attr_digest);
      EXPECT_EQ(tree_dig.has_value(), build.has_tree_digest);

      if (build.has_attr_digest) {
        EXPECT_EQ(attr_dig.hex(), kAttrDigest);
      }

      if (build.has_tree_digest) {
        EXPECT_EQ(tree_dig.hex(), kTreeDigest);
      }
    } else {
      auto const kExpectedError = build.legacy_image
                                      ? "invalid superblock version"
                                      : "invalid superblock section type";
      EXPECT_THAT([&] { ed.read(iss); },
                  testing::ThrowsMessage<std::runtime_error>(
                      testing::HasSubstr(kExpectedError)));
    }

    auto fs = t.fs_from_file(
        "test.dwarfs",
        {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});

    EXPECT_EQ(fs.version(),
              build.legacy_image ? FS_VERSION_CURRENT : FS_VERSION_ACCEPTED);
    EXPECT_EQ(fs.image_offset(), build_has_header ? header.size() : 0);
    EXPECT_EQ(fs.has_superblock(), build.has_superblock);

    if (build.has_superblock) {
      EXPECT_EQ(fs.image_size_alignment(), build.size_alignment);
    } else {
      auto const image_size = fs.image_size();
      EXPECT_EQ(image_size,
                image->size() - (build_has_header ? header.size() : 0));
      EXPECT_TRUE(image_size % build.size_alignment == 0)
          << "image size << " << image_size << " << is not aligned to "
          << build.size_alignment;
    }

    auto const digests = fs.digests();
    std::optional<filesystem_digests> computed_digests;

    if (build.has_superblock && has_any_digest) {
      computed_digests = compute_digests(t, fs, build.has_tree_digest);
    }

    if (build.has_superblock && build.has_attr_digest) {
      EXPECT_EQ(digests.attr_digest.hex(), kAttrDigest);
      EXPECT_EQ(computed_digests->attr_digest.hex(), kAttrDigest);
    }

    if (build.has_superblock && build.has_tree_digest) {
      EXPECT_EQ(digests.tree_digest.hex(), kTreeDigest);
      EXPECT_EQ(computed_digests->tree_digest.hex(), kTreeDigest);
    }
  }

  // rebuild

  auto t2 = mkdwarfs_tester::create_with_image(*image, "input.dwarfs");

  std::vector<std::string> args2 = {"-i", "input.dwarfs", "-o", "test.dwarfs",
                                    "-l1"};
  args2.insert(args2.end(), rebuild.args.begin(), rebuild.args.end());

  if (build.legacy_image && !rebuild.legacy_image) {
    args2.push_back("--no-backwards-compat");
  }

  if (!build.legacy_image && rebuild.legacy_image) {
    return; // cannot rebuild a non-legacy image into a legacy image
  }

  if (build_has_header && !rebuild_has_header) {
    args2.push_back("--remove-header");
  } else if (!build_has_header && rebuild_has_header) {
    t2.fa->set_file("lorem", header);
    args2.push_back("--header=lorem");
  }

  t2.fa->sync_files_to(*t2.os);

  ASSERT_EQ(0, t2.run(args2)) << t2.err();

  auto image2 = t2.fa->get_file("test.dwarfs");
  ASSERT_TRUE(image2) << "test.dwarfs not created";

  {
    superblock_editor ed;
    std::istringstream iss(*image2);

    if (rebuild_has_header) {
      EXPECT_THAT(*image2, testing::StartsWith(header));
      iss.seekg(header.size(), std::ios::beg);
    }

    bool const has_any_digest =
        rebuild.has_attr_digest.value_or(build.has_attr_digest) ||
        rebuild.has_tree_digest.value_or(build.has_tree_digest);

    auto const expected_alignment = rebuild.size_alignment.value_or(
        build.has_superblock ? build.size_alignment : 1);

    if (rebuild.has_superblock) {
      ASSERT_NO_THROW(ed.read(iss));

      EXPECT_EQ(ed.major_version(), 1);
      EXPECT_EQ(ed.minor_version(), 1);

      EXPECT_EQ(ed.fs_size_alignment(), expected_alignment);

      auto const fs_size = ed.fs_size();
      EXPECT_EQ(fs_size.has_value(), rebuild.has_size);

      if (rebuild.has_size) {
        EXPECT_EQ(fs_size.value(), image2->size() - ed.image_offset());
      }

      auto const uuid = ed.fs_uuid();
      EXPECT_EQ(uuid.has_value(), rebuild.has_uuid.value_or(build.has_uuid));

      EXPECT_EQ(ed.fs_label(), rebuild.label.value_or(build.label));

      auto const expected_algo = has_any_digest
                                     ? digest_algorithm::BLAKE3_256
                                     : digest_algorithm::UNINITIALIZED;
      EXPECT_EQ(ed.digest_algo(), expected_algo);
      EXPECT_EQ(ed.digest_scheme_version(), has_any_digest ? 1 : 0);

      auto const attr_dig = ed.attr_digest();
      auto const tree_dig = ed.tree_digest();

      EXPECT_EQ(attr_dig.has_value(),
                rebuild.has_attr_digest.value_or(build.has_attr_digest));
      EXPECT_EQ(tree_dig.has_value(),
                rebuild.has_tree_digest.value_or(build.has_tree_digest));

      if (rebuild.has_attr_digest.value_or(build.has_attr_digest)) {
        EXPECT_EQ(attr_dig.hex(), rebuild.attr_digest.value_or(kAttrDigest));
      }

      if (rebuild.has_tree_digest.value_or(build.has_tree_digest)) {
        EXPECT_EQ(tree_dig.hex(), kTreeDigest);
      }
    } else {
      auto const kExpectedError = rebuild.legacy_image
                                      ? "invalid superblock version"
                                      : "invalid superblock section type";
      EXPECT_THAT([&] { ed.read(iss); },
                  testing::ThrowsMessage<std::runtime_error>(
                      testing::HasSubstr(kExpectedError)));
    }

    auto fs = t2.fs_from_file(
        "test.dwarfs",
        {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});

    EXPECT_EQ(fs.version(),
              rebuild.legacy_image ? FS_VERSION_CURRENT : FS_VERSION_ACCEPTED);
    EXPECT_EQ(fs.image_offset(), rebuild_has_header ? header.size() : 0);
    EXPECT_EQ(fs.has_superblock(), rebuild.has_superblock);

    if (rebuild.has_superblock) {
      EXPECT_EQ(fs.image_size_alignment(), expected_alignment);
    } else {
      auto const image_size = fs.image_size();

      EXPECT_EQ(image_size,
                image2->size() - (rebuild_has_header ? header.size() : 0));
      EXPECT_TRUE(image_size % expected_alignment == 0)
          << "image size << " << image_size << " << is not aligned to "
          << expected_alignment;
    }

    auto const digests = fs.digests();
    std::optional<filesystem_digests> computed_digests;

    if (rebuild.has_superblock && has_any_digest) {
      computed_digests = compute_digests(
          t, fs, rebuild.has_tree_digest.value_or(build.has_tree_digest));
    }

    if (rebuild.has_superblock &&
        rebuild.has_attr_digest.value_or(build.has_attr_digest)) {
      EXPECT_EQ(digests.attr_digest.hex(),
                rebuild.attr_digest.value_or(kAttrDigest));
      EXPECT_EQ(computed_digests->attr_digest.hex(),
                rebuild.attr_digest.value_or(kAttrDigest));
    }

    if (rebuild.has_superblock &&
        rebuild.has_tree_digest.value_or(build.has_tree_digest)) {
      EXPECT_EQ(digests.tree_digest.hex(), kTreeDigest);
      EXPECT_EQ(computed_digests->tree_digest.hex(), kTreeDigest);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(mkdwarfs_test, write_superblock_test,
                         ::testing::Combine(::testing::ValuesIn(build_steps),
                                            ::testing::Bool(),
                                            ::testing::ValuesIn(rebuild_steps),
                                            ::testing::Bool()));

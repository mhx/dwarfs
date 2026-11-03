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

#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include <fmt/format.h>
#include <fmt/ostream.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <range/v3/view/cartesian_product.hpp>

#include <dwarfs/reader/compute_fs_digests.h>
#include <dwarfs/superblock_editor.h>

#include "filter_test_data.h"
#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace {

constexpr auto kBoolean = std::array{true, false};

constexpr inline filesystem_version FS_VERSION_ACCEPTED{MAJOR_VERSION,
                                                        MINOR_VERSION_ACCEPTED};

struct build_step {
  std::vector<std::string> args;
  bool legacy_image{false};
  bool has_superblock{true};
  bool has_uuid{true};
  bool has_size{true};
  std::uint64_t size_alignment{1};
  std::string label{};
  std::optional<std::string> uuid{};
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
  bool keep_uuid{true};
  bool has_size{true};
  std::optional<std::uint64_t> size_alignment{};
  std::optional<std::string> label{};
  std::optional<std::string> uuid{};
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

struct check_step {
  std::vector<std::string> args;
  bool read_only{false};
  std::optional<bool> has_uuid{};
  std::optional<bool> has_size{};
  std::optional<std::string> label{};
  std::optional<std::string> uuid{};
  std::optional<bool> has_attr_digest{};
  std::optional<bool> has_tree_digest{};

  friend std::ostream& operator<<(std::ostream& os, check_step const& test) {
    return os << fmt::format("args={}", fmt::join(test.args, ","));
  }
};

constexpr std::string_view kUnicodeLabel{"我爱你/☀️ Sun/Γειά σας/مرحبًا/⚽️"};

constexpr std::string_view kAttrDigest{
    "779d7614bdbf825bda47f7807d57c3bdcf97766ad8c2bd2488289c2310c4514a"};
constexpr std::string_view kAttrDigestNorm{
    "46bc5c8f5e9b4ece7a26bcaca1aa52327cd14abf1bc3d4920697a4d8340b3bc2"};
constexpr std::string_view kTreeDigest{
    "01786e7be69d7460976bb7d721a3a170019691865bdba2a8e5954287025567ea"};

constexpr std::string_view kTestUuid{"1fe2720a-2a81-446b-afc4-28cae43edade"};

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
        .args = {"--label=" + std::string(63, 'a')},
        .label = std::string(63, 'a'),
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
        .args = {"--no-superblock-init", "--uuid", std::string{kTestUuid}},
        .has_uuid = true,
        .has_size = false,
        .uuid = std::string{kTestUuid},
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
        .args = {"--superblock-digests=none"},
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--superblock-digests=attr"},
        .has_tree_digest = false,
    },
    {
        .args = {"--superblock-digests=tree"},
        .has_attr_digest = false,
    },
    {
        .args = {"--superblock-digests=both"},
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
    {
        .args =
            {
                "--no-superblock-init",
                "--uuid",
                std::string{kTestUuid},
            },
        .has_uuid = true,
        .has_size = false,
        .uuid = std::string{kTestUuid},
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
        .args = {"--rebuild-metadata", "--uuid", std::string{kTestUuid}},
        .has_uuid = true,
        .keep_uuid = false,
        .uuid = std::string{kTestUuid},
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--label", std::string{kUnicodeLabel}},
        .label = std::string{kUnicodeLabel},
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--recompress=none", "--clear-label"},
        .label = "",
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
        .args = {"--recompress=none", "--no-superblock-init", "--uuid=random"},
        .has_uuid = true,
        .keep_uuid = false,
        .has_size = false,
    },
    {
        .args = {"--recompress=none", "--superblock-digests=none"},
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--recompress=none", "--superblock-digests=attr"},
        .has_attr_digest = true,
        .has_tree_digest = false,
    },
    {
        .args = {"--recompress=none", "--superblock-digests=tree"},
        .has_attr_digest = false,
        .has_tree_digest = true,
    },
    {
        .args = {"--rebuild-metadata", "--no-superblock-init"},
        .has_size = false,
        .has_attr_digest = false,
    },
    {
        .args = {"--rebuild-metadata", "--superblock-digests=none"},
        .has_attr_digest = false,
        .has_tree_digest = false,
    },
    {
        .args = {"--rebuild-metadata", "--superblock-digests=attr"},
        .has_attr_digest = true,
        .has_tree_digest = false,
    },
    {
        .args = {"--rebuild-metadata", "--chmod=norm",
                 "--superblock-digests=none"},
        .has_attr_digest = false,
        .has_tree_digest = false,
        .attr_digest = kAttrDigestNorm,
    },
};

std::vector<check_step> check_steps{
    {
        .args = {},
        .read_only = true,
    },
    {
        .args = {"--check-integrity"},
        .read_only = true,
    },
    {
        .args = {"--init-superblock"},
        .has_size = true,
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--init-superblock=all"},
        .has_uuid = true,
        .has_size = true,
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--init-superblock=size"},
        .has_size = true,
    },
    {
        .args = {"--init-superblock=uuid"},
        .has_uuid = true,
    },
    {
        .args = {"--init-superblock=digests"},
        .has_attr_digest = true,
        .has_tree_digest = true,
    },
    {
        .args = {"--init-superblock=attr_digest"},
        .has_attr_digest = true,
    },
    {
        .args = {"--init-superblock=tree_digest"},
        .has_tree_digest = true,
    },
    {
        .args = {"--init-superblock=uuid,attr_digest"},
        .has_uuid = true,
        .has_attr_digest = true,
    },
    {
        .args = {"--set-label=something"},
        .label = "something",
    },
    {
        .args = {"--clear-label"},
        .label = "",
    },
    {
        .args = {"--set-uuid=nil"},
        .has_uuid = false,
    },
    {
        .args = {"--set-uuid=random"},
        .has_uuid = true,
        .uuid = "random",
    },
    {
        .args = {"--set-uuid", std::string{kTestUuid}},
        .has_uuid = true,
        .uuid = std::string{kTestUuid},
    },
};

filesystem_digests
compute_digests(mkdwarfs_tester const& t, reader::filesystem_v2 const& fs,
                bool with_tree_digest) {
  return reader::compute_filesystem_digests(
      *t.lgr, *t.os, fs, {.compute_tree_digest = with_tree_digest});
}

class write_superblock_test
    : public ::testing::TestWithParam<std::tuple<build_step, bool>> {};

} // namespace

TEST_P(write_superblock_test, write_superblock) {
  auto const [build, build_has_header] = GetParam();
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

  std::optional<std::string> original_uuid;

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

      if (build.uuid) {
        EXPECT_EQ(uuid.value(), build.uuid.value());
      }

      if (build.has_uuid) {
        original_uuid = uuid.value();
      }

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

  // check

  for (auto const& check : check_steps) {
    SCOPED_TRACE(fmt::format("check: {}", fmt::streamed(check)));

    auto ct = dwarfsck_tester::create_with_image(*image, "test.dwarfs");

    if (!check.read_only) {
      ct.fa->set_file("test.dwarfs", *image);
    }

    std::vector<std::string> cargs = {"-i", "test.dwarfs"};
    cargs.insert(cargs.end(), check.args.begin(), check.args.end());

    auto const exit_code = ct.run(cargs);

    if (check.read_only || build.has_superblock) {
      ASSERT_EQ(0, exit_code) << ct.err();
    } else {
      ASSERT_EQ(1, exit_code) << ct.err();
      if (!build.has_superblock) {
        EXPECT_THAT(ct.err(), testing::HasSubstr("no valid superblock found"));
      }
    }

    if (check.read_only) {
      continue;
    }

    auto outimg = ct.fa->get_file("test.dwarfs");
    ASSERT_TRUE(outimg) << "test.dwarfs not created";

    EXPECT_EQ(image->size(), outimg->size())
        << "image size changed after check";

    superblock_editor ed;
    std::istringstream iss(*outimg);

    if (build_has_header) {
      EXPECT_THAT(*outimg, testing::StartsWith(header));
      iss.seekg(header.size(), std::ios::beg);
    }

    bool const has_attr_digest =
        check.has_attr_digest.value_or(build.has_attr_digest);
    bool const has_tree_digest =
        check.has_tree_digest.value_or(build.has_tree_digest);
    bool const has_any_digest = has_attr_digest || has_tree_digest;

    if (build.has_superblock) {
      ASSERT_NO_THROW(ed.read(iss));

      EXPECT_EQ(ed.major_version(), 1);
      EXPECT_EQ(ed.minor_version(), 1);

      EXPECT_EQ(ed.fs_size_alignment(), build.size_alignment);

      auto const fs_size = ed.fs_size();
      EXPECT_EQ(fs_size.has_value(), check.has_size.value_or(build.has_size));
      if (fs_size) {
        EXPECT_EQ(*fs_size, outimg->size() - ed.image_offset());
      }

      bool const has_uuid = check.has_uuid.value_or(build.has_uuid);
      auto const uuid = ed.fs_uuid();
      EXPECT_EQ(uuid.has_value(), has_uuid);

      if (has_uuid) {
        if (check.uuid == "random") {
          if (original_uuid.has_value()) {
            EXPECT_NE(uuid.value(), original_uuid.value());
          }
        } else {
          auto const expected_uuid =
              check.uuid.or_else([&] { return original_uuid; });

          if (expected_uuid.has_value()) {
            EXPECT_EQ(uuid.value(), expected_uuid.value());
          }
        }
      }

      EXPECT_EQ(ed.fs_label(), check.label.value_or(build.label));

      auto const expected_algo = has_any_digest
                                     ? digest_algorithm::BLAKE3_256
                                     : digest_algorithm::UNINITIALIZED;
      EXPECT_EQ(ed.digest_algo(), expected_algo);
      EXPECT_EQ(ed.digest_scheme_version(), has_any_digest ? 1 : 0);

      auto const attr_dig = ed.attr_digest();
      auto const tree_dig = ed.tree_digest();

      EXPECT_EQ(attr_dig.has_value(), has_attr_digest);
      EXPECT_EQ(tree_dig.has_value(), has_tree_digest);

      if (has_attr_digest) {
        EXPECT_EQ(attr_dig.hex(), kAttrDigest);
      }

      if (has_tree_digest) {
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

    auto fs = ct.fs_from_file(
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
                outimg->size() - (build_has_header ? header.size() : 0));
      EXPECT_TRUE(image_size % build.size_alignment == 0)
          << "outimg size << " << image_size << " << is not aligned to "
          << build.size_alignment;
    }

    auto const digests = fs.digests();
    std::optional<filesystem_digests> computed_digests;

    if (build.has_superblock && has_any_digest) {
      computed_digests = compute_digests(
          t, fs, check.has_tree_digest.value_or(build.has_tree_digest));
    }

    if (build.has_superblock && has_attr_digest) {
      EXPECT_EQ(digests.attr_digest.hex(), kAttrDigest);
      EXPECT_EQ(computed_digests->attr_digest.hex(), kAttrDigest);
    }

    if (build.has_superblock && has_tree_digest) {
      EXPECT_EQ(digests.tree_digest.hex(), kTreeDigest);
      EXPECT_EQ(computed_digests->tree_digest.hex(), kTreeDigest);
    }
  }

  // rebuild
  for (auto const& [rebuild, rebuild_has_header] :
       ranges::views::cartesian_product(rebuild_steps, kBoolean)) {
    SCOPED_TRACE(fmt::format("rebuild: {} header: {}", fmt::streamed(rebuild),
                             rebuild_has_header));

    auto t2 = mkdwarfs_tester::create_with_image(*image, "input.dwarfs");

    std::vector<std::string> args2 = {"-i", "input.dwarfs", "-o", "test.dwarfs",
                                      "-l1"};
    args2.insert(args2.end(), rebuild.args.begin(), rebuild.args.end());

    if (build.legacy_image && !rebuild.legacy_image) {
      args2.push_back("--no-backwards-compat");
    }

    if (!build.legacy_image && rebuild.legacy_image) {
      continue; // cannot rebuild a non-legacy image into a legacy image
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

    auto const expected_alignment = rebuild.size_alignment.value_or(
        build.has_superblock ? build.size_alignment : 1);
    auto const expected_attr_digest = rebuild.attr_digest.value_or(kAttrDigest);

    std::optional<std::string> rewritten_uuid;

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

        if (uuid.has_value()) {
          rewritten_uuid = uuid.value();
        }

        if (original_uuid.has_value()) {
          if (rebuild.keep_uuid) {
            EXPECT_EQ(uuid.value(), original_uuid.value());
          } else {
            if (build.uuid != original_uuid) {
              EXPECT_NE(uuid.value(), original_uuid.value());
            }
            if (rebuild.uuid.has_value()) {
              EXPECT_EQ(uuid.value(), rebuild.uuid.value());
            }
          }
        }

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
          EXPECT_EQ(attr_dig.hex(), expected_attr_digest);
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

      EXPECT_EQ(fs.version(), rebuild.legacy_image ? FS_VERSION_CURRENT
                                                   : FS_VERSION_ACCEPTED);
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
        EXPECT_EQ(digests.attr_digest.hex(), expected_attr_digest);
        EXPECT_EQ(computed_digests->attr_digest.hex(), expected_attr_digest);
      }

      if (rebuild.has_superblock &&
          rebuild.has_tree_digest.value_or(build.has_tree_digest)) {
        EXPECT_EQ(digests.tree_digest.hex(), kTreeDigest);
        EXPECT_EQ(computed_digests->tree_digest.hex(), kTreeDigest);
      }
    }

    // check again

    for (auto const& check : check_steps) {
      SCOPED_TRACE(fmt::format("check: {}", fmt::streamed(check)));

      auto ct = dwarfsck_tester::create_with_image(*image2, "test.dwarfs");

      if (!check.read_only) {
        ct.fa->set_file("test.dwarfs", *image2);
      }

      std::vector<std::string> cargs = {"-i", "test.dwarfs"};
      cargs.insert(cargs.end(), check.args.begin(), check.args.end());

      auto const exit_code = ct.run(cargs);

      if (check.read_only || rebuild.has_superblock) {
        ASSERT_EQ(0, exit_code) << ct.err();
      } else {
        ASSERT_EQ(1, exit_code) << ct.err();
        if (!rebuild.has_superblock) {
          EXPECT_THAT(ct.err(),
                      testing::HasSubstr("no valid superblock found"));
        }
      }

      if (check.read_only) {
        continue;
      }

      auto outimg = ct.fa->get_file("test.dwarfs");
      ASSERT_TRUE(outimg) << "test.dwarfs not created";

      EXPECT_EQ(image2->size(), outimg->size())
          << "image size changed after check";

      superblock_editor ed;
      std::istringstream iss(*outimg);

      if (rebuild_has_header) {
        EXPECT_THAT(*outimg, testing::StartsWith(header));
        iss.seekg(header.size(), std::ios::beg);
      }

      bool const has_attr_digest =
          check.has_attr_digest.or_else([&] { return rebuild.has_attr_digest; })
              .value_or(build.has_attr_digest);
      bool const has_tree_digest =
          check.has_tree_digest.or_else([&] { return rebuild.has_tree_digest; })
              .value_or(build.has_tree_digest);
      bool const has_any_digest = has_attr_digest || has_tree_digest;

      if (rebuild.has_superblock) {
        ASSERT_NO_THROW(ed.read(iss));

        EXPECT_EQ(ed.major_version(), 1);
        EXPECT_EQ(ed.minor_version(), 1);

        EXPECT_EQ(ed.fs_size_alignment(), expected_alignment);

        auto const fs_size = ed.fs_size();
        EXPECT_EQ(fs_size.has_value(),
                  check.has_size.value_or(rebuild.has_size));
        if (fs_size) {
          EXPECT_EQ(*fs_size, outimg->size() - ed.image_offset());
        }

        bool const has_uuid =
            check.has_uuid.or_else([&] { return rebuild.has_uuid; })
                .value_or(build.has_uuid);
        auto const uuid = ed.fs_uuid();
        EXPECT_EQ(uuid.has_value(), has_uuid);

        if (has_uuid) {
          if (check.uuid == "random") {
            if (rewritten_uuid.has_value()) {
              EXPECT_NE(uuid.value(), rewritten_uuid.value());
            }
          } else {
            auto const expected_uuid =
                check.uuid.or_else([&] { return rewritten_uuid; });

            if (expected_uuid.has_value()) {
              EXPECT_EQ(uuid.value(), expected_uuid.value());
            }
          }
        }

        EXPECT_EQ(ed.fs_label(),
                  check.label.or_else([&] { return rebuild.label; })
                      .value_or(build.label));

        auto const expected_algo = has_any_digest
                                       ? digest_algorithm::BLAKE3_256
                                       : digest_algorithm::UNINITIALIZED;
        EXPECT_EQ(ed.digest_algo(), expected_algo);
        EXPECT_EQ(ed.digest_scheme_version(), has_any_digest ? 1 : 0);

        auto const attr_dig = ed.attr_digest();
        auto const tree_dig = ed.tree_digest();

        EXPECT_EQ(attr_dig.has_value(), has_attr_digest);
        EXPECT_EQ(tree_dig.has_value(), has_tree_digest);

        if (has_attr_digest) {
          EXPECT_EQ(attr_dig.hex(), expected_attr_digest);
        }

        if (has_tree_digest) {
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

      auto fs = ct.fs_from_file(
          "test.dwarfs",
          {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});

      EXPECT_EQ(fs.version(), rebuild.legacy_image ? FS_VERSION_CURRENT
                                                   : FS_VERSION_ACCEPTED);
      EXPECT_EQ(fs.image_offset(), rebuild_has_header ? header.size() : 0);
      EXPECT_EQ(fs.has_superblock(), rebuild.has_superblock);

      if (rebuild.has_superblock) {
        EXPECT_EQ(fs.image_size_alignment(), expected_alignment);
      } else {
        auto const image_size = fs.image_size();
        EXPECT_EQ(image_size,
                  outimg->size() - (rebuild_has_header ? header.size() : 0));
        EXPECT_TRUE(image_size % expected_alignment == 0)
            << "outimg size << " << image_size << " << is not aligned to "
            << expected_alignment;
      }

      auto const digests = fs.digests();
      std::optional<filesystem_digests> computed_digests;

      if (rebuild.has_superblock && has_any_digest) {
        computed_digests = compute_digests(t, fs, has_tree_digest);
      }

      if (rebuild.has_superblock && has_attr_digest) {
        EXPECT_EQ(digests.attr_digest.hex(), expected_attr_digest);
        EXPECT_EQ(computed_digests->attr_digest.hex(), expected_attr_digest);
      }

      if (rebuild.has_superblock && has_tree_digest) {
        EXPECT_EQ(digests.tree_digest.hex(), kTreeDigest);
        EXPECT_EQ(computed_digests->tree_digest.hex(), kTreeDigest);
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(mkdwarfs_test, write_superblock_test,
                         ::testing::Combine(::testing::ValuesIn(build_steps),
                                            ::testing::Bool()));

TEST(mkdwarfs_superblock_digest_test, hollow_preserves_attribute_digest) {
  // Use independent, but identical source trees.
  mkdwarfs_tester regular;
  mkdwarfs_tester hollow;
  regular.fa->sync_files_to(*regular.os);
  hollow.fa->sync_files_to(*hollow.os);

  ASSERT_EQ(0,
            regular.run({"-i", "/", "-o", "regular.dwarfs", "-l1",
                         "--no-backwards-compat", "--superblock-digests=both"}))
      << regular.err();
  ASSERT_EQ(0, hollow.run({"-i", "/", "-o", "hollow.dwarfs", "-l1",
                           "--no-backwards-compat", "--hollow",
                           "--superblock-digests=both"}))
      << hollow.err();

  auto regular_fs = regular.fs_from_file("regular.dwarfs");
  auto hollow_fs = hollow.fs_from_file("hollow.dwarfs");
  auto const regular_digests = regular_fs.digests();
  auto const hollow_digests = hollow_fs.digests();

  ASSERT_TRUE(regular_digests.attr_digest);
  ASSERT_TRUE(hollow_digests.attr_digest);
  EXPECT_EQ(regular_digests.attr_digest.hex(),
            hollow_digests.attr_digest.hex());
  EXPECT_EQ(regular_digests.attr_digest.hex(), kAttrDigest);

  auto const regular_computed = compute_digests(regular, regular_fs, true);
  auto const hollow_computed = compute_digests(hollow, hollow_fs, true);
  EXPECT_EQ(regular_digests.attr_digest.hex(),
            regular_computed.attr_digest.hex());
  EXPECT_EQ(hollow_digests.attr_digest.hex(),
            hollow_computed.attr_digest.hex());

  ASSERT_TRUE(regular_digests.tree_digest);
  ASSERT_TRUE(hollow_digests.tree_digest);
  EXPECT_EQ(regular_digests.tree_digest.hex(),
            regular_computed.tree_digest.hex());
  EXPECT_EQ(hollow_digests.tree_digest.hex(),
            hollow_computed.tree_digest.hex());
  EXPECT_NE(regular_digests.tree_digest.hex(),
            hollow_digests.tree_digest.hex());
}

TEST(mkdwarfs_superblock_cli_test, rejects_conflicting_or_invalid_options) {
  auto expect_rejected = [](std::vector<std::string> extra,
                            std::string_view message) {
    mkdwarfs_tester t;
    t.fa->sync_files_to(*t.os);
    std::vector<std::string> args = {
        "-i", "/", "-o", "test.dwarfs", "-l1", "--no-backwards-compat"};
    args.insert(args.end(), extra.begin(), extra.end());
    EXPECT_EQ(1, t.run(args)) << t.err();
    EXPECT_THAT(t.err(), testing::HasSubstr(std::string(message)));
  };

  expect_rejected({"--label=test", "--clear-label"}, "mutually exclusive");
  expect_rejected({"--label=" + std::string(64, 'a')},
                  "filesystem label is too long");
  expect_rejected({"--label=" + std::string(30, 'x') +
                   std::string("我爱你我爱你我爱你我爱你")},
                  "filesystem label is too long");
  expect_rejected({"--superblock-digests=invalid"},
                  "invalid '--superblock-digests'");
  expect_rejected({"--no-superblock", "--superblock-digests=both"},
                  "cannot request digests without a superblock");
  expect_rejected({"--no-superblock-init", "--superblock-digests=tree"},
                  "requested superblock digests cannot be produced");
}

TEST(mkdwarfs_superblock_cli_test, rejects_explicit_keep_without_source_uuid) {
  mkdwarfs_tester t;
  t.fa->sync_files_to(*t.os);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "legacy.dwarfs", "-l1"})) << t.err();
  auto const source = t.fa->get_file("legacy.dwarfs");
  ASSERT_TRUE(source);

  auto t2 = mkdwarfs_tester::create_with_image(*source, "input.dwarfs");
  t2.fa->sync_files_to(*t2.os);
  EXPECT_EQ(
      1, t2.run({"-i", "input.dwarfs", "-o", "out.dwarfs", "-l1",
                 "--recompress=none", "--no-backwards-compat", "--uuid=keep"}))
      << t2.err();
  EXPECT_THAT(t2.err(), testing::HasSubstr("requires a source superblock"));
}

TEST(mkdwarfs_superblock_cli_test, dwarfsck_rejects_conflicting_edits) {
  mkdwarfs_tester t;
  t.fa->sync_files_to(*t.os);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "test.dwarfs", "-l1",
                      "--no-backwards-compat"}))
      << t.err();
  auto const image = t.fa->get_file("test.dwarfs");
  ASSERT_TRUE(image);

  for (auto const& args : std::vector<std::vector<std::string>>{
           {"--set-label=test", "--clear-label"},
           {"--repair-superblock", "--set-uuid=nil"},
           {"--repair-superblock", "--init-superblock"}}) {
    auto ct = dwarfsck_tester::create_with_image(*image, "test.dwarfs");
    std::vector<std::string> cargs = {"-i", "test.dwarfs"};
    cargs.insert(cargs.end(), args.begin(), args.end());
    EXPECT_EQ(1, ct.run(cargs)) << ct.err();
    EXPECT_THAT(ct.err(), testing::HasSubstr("mutually exclusive"));
  }

  auto ct = dwarfsck_tester::create_with_image(*image, "test.dwarfs");
  EXPECT_EQ(0, ct.run({"-i", "test.dwarfs", "--repair-superblock"}))
      << ct.err();
}

TEST(mkdwarfs_superblock_rewrite_test, preserves_initialized_image_alignment) {
  auto const header = loremipsum(321);
  mkdwarfs_tester t;
  t.fa->set_file("header.bin", header);
  t.fa->sync_files_to(*t.os);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "source.dwarfs", "-l1",
                      "--no-backwards-compat", "--header=header.bin",
                      "--image-size-alignment=512"}))
      << t.err();

  auto check_alignment = [](mkdwarfs_tester& tester, std::string const& path,
                            std::string_view header, std::uint64_t alignment) {
    auto const image = tester.fa->get_file(path);
    ASSERT_TRUE(image);
    EXPECT_THAT(*image, testing::StartsWith(std::string(header)));

    auto fs = tester.fs_from_file(
        path, {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});
    EXPECT_EQ(fs.image_offset(), header.size());
    EXPECT_EQ(fs.image_size(), image->size() - header.size());
    EXPECT_EQ(fs.image_size_alignment(), alignment);
    EXPECT_EQ(fs.image_size() % alignment, 0);
    if (!header.empty()) {
      EXPECT_NE(image->size() % alignment, 0)
          << "the prepended header must not be included in alignment";
    }

    superblock_editor ed;
    std::istringstream iss(*image);
    iss.seekg(header.size(), std::ios::beg);
    ASSERT_NO_THROW(ed.read(iss));
    EXPECT_EQ(ed.fs_size_alignment(), alignment);
    ASSERT_TRUE(ed.fs_size());
    EXPECT_EQ(*ed.fs_size(), fs.image_size());
  };

  check_alignment(t, "source.dwarfs", header, 512);
  auto const source = t.fa->get_file("source.dwarfs");
  ASSERT_TRUE(source);

  // Inherit the source alignment and header.
  auto t2 = mkdwarfs_tester::create_with_image(*source, "input.dwarfs");
  t2.fa->sync_files_to(*t2.os);
  ASSERT_EQ(0, t2.run({"-i", "input.dwarfs", "-o", "rewritten.dwarfs", "-l1",
                       "--recompress=none"}))
      << t2.err();
  check_alignment(t2, "rewritten.dwarfs", header, 512);

  // Override the alignment and remove the header.
  auto const rewritten = t2.fa->get_file("rewritten.dwarfs");
  ASSERT_TRUE(rewritten);
  auto t3 = mkdwarfs_tester::create_with_image(*rewritten, "input.dwarfs");
  t3.fa->sync_files_to(*t3.os);
  ASSERT_EQ(0, t3.run({"-i", "input.dwarfs", "-o", "rewritten.dwarfs", "-l1",
                       "--recompress=none", "--remove-header",
                       "--image-size-alignment=4096"}))
      << t3.err();
  check_alignment(t3, "rewritten.dwarfs", "", 4096);
}

TEST(mkdwarfs_superblock_rewrite_test, metadata_changes_recompute_attr_digest) {
  mkdwarfs_tester t;
  t.fa->sync_files_to(*t.os);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "source.dwarfs", "-l1",
                      "--no-backwards-compat"}))
      << t.err();
  auto const source = t.fa->get_file("source.dwarfs");
  ASSERT_TRUE(source);

  auto const source_fs = t.fs_from_file("source.dwarfs");
  auto const source_digests = source_fs.digests();
  ASSERT_TRUE(source_digests.attr_digest);
  ASSERT_TRUE(source_digests.tree_digest);

  struct test_case {
    std::string_view name;
    std::vector<std::string> args;
    bool changes_attributes{true};
  };

  for (auto const& tc : std::vector<test_case>{
           {"owner", {"--rebuild-metadata", "--set-owner=123"}},
           {"group", {"--rebuild-metadata", "--set-group=456"}},
           {"time", {"--rebuild-metadata", "--set-time=98765"}},
           {"block-size and owner",
            {"--change-block-size", "-S12", "--set-owner=123"}},
           {"block-size only", {"--change-block-size", "-S12"}, false},
       }) {
    SCOPED_TRACE(tc.name);
    auto t2 = mkdwarfs_tester::create_with_image(*source, "input.dwarfs");
    t2.fa->sync_files_to(*t2.os);
    std::vector<std::string> args = {"-i", "input.dwarfs", "-o",
                                     "rewritten.dwarfs", "-l1"};
    args.insert(args.end(), tc.args.begin(), tc.args.end());
    ASSERT_EQ(0, t2.run(args)) << t2.err();

    auto fs = t2.fs_from_file("rewritten.dwarfs");
    auto const stored = fs.digests();
    ASSERT_TRUE(stored.attr_digest);
    ASSERT_TRUE(stored.tree_digest);
    auto const computed = compute_digests(t2, fs, true);

    // These transformations really changed the metadata. A copied attribute
    // digest would differ from the independently recomputed digest. A pure
    // block-size change should leave the logical attributes unchanged.
    if (tc.changes_attributes) {
      EXPECT_NE(stored.attr_digest.hex(), source_digests.attr_digest.hex());
    } else {
      EXPECT_EQ(stored.attr_digest.hex(), source_digests.attr_digest.hex());
    }
    EXPECT_EQ(stored.attr_digest.hex(), computed.attr_digest.hex());

    // File contents and the logical tree are always unchanged.
    EXPECT_EQ(stored.tree_digest.hex(), source_digests.tree_digest.hex());
    EXPECT_EQ(computed.tree_digest.hex(), source_digests.tree_digest.hex());
  }
}

TEST(mkdwarfs_superblock_rewrite_test,
     metadata_changes_clear_attr_digest_without_initialization) {
  mkdwarfs_tester t;
  t.fa->sync_files_to(*t.os);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "source.dwarfs", "-l1",
                      "--no-backwards-compat"}))
      << t.err();
  auto const source = t.fa->get_file("source.dwarfs");
  ASSERT_TRUE(source);
  auto const source_fs = t.fs_from_file("source.dwarfs");
  auto const source_digests = source_fs.digests();
  ASSERT_TRUE(source_digests.attr_digest);
  ASSERT_TRUE(source_digests.tree_digest);

  auto t2 = mkdwarfs_tester::create_with_image(*source, "input.dwarfs");
  t2.fa->sync_files_to(*t2.os);
  ASSERT_EQ(
      0, t2.run({"-i", "input.dwarfs", "-o", "rewritten.dwarfs", "-l1",
                 "--rebuild-metadata", "--chmod=norm", "--no-superblock-init"}))
      << t2.err();

  auto const rewritten = t2.fa->get_file("rewritten.dwarfs");
  ASSERT_TRUE(rewritten);
  superblock_editor ed;
  std::istringstream iss(*rewritten);
  ASSERT_NO_THROW(ed.read(iss));
  EXPECT_FALSE(ed.fs_size()); // Do not inherit the source's initialized size.
  EXPECT_FALSE(ed.attr_digest());
  ASSERT_TRUE(ed.tree_digest());
  EXPECT_EQ(ed.tree_digest().hex(), source_digests.tree_digest.hex());

  auto fs = t2.fs_from_file("rewritten.dwarfs");
  auto const stored = fs.digests();
  EXPECT_FALSE(stored.attr_digest);
  ASSERT_TRUE(stored.tree_digest);
  EXPECT_EQ(stored.tree_digest.hex(), source_digests.tree_digest.hex());

  auto const computed = compute_digests(t2, fs, true);
  EXPECT_EQ(computed.attr_digest.hex(), kAttrDigestNorm);
  EXPECT_NE(computed.attr_digest.hex(), source_digests.attr_digest.hex());
  EXPECT_EQ(computed.tree_digest.hex(), source_digests.tree_digest.hex());
}

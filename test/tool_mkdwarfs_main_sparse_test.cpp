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

#include <algorithm>
#include <array>

#include <fmt/format.h>

#include <gmock/gmock.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/file_util.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/vfs_stat.h>

#include "test_tool_main_checks.h"
#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

using namespace std::literals::string_view_literals;
using namespace dwarfs::binary_literals;

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

namespace {

struct extent_data {
  detail::file_extent_info info;
  std::optional<std::string> xxh_hexdigest{};

  friend bool operator==(extent_data const&, extent_data const&) = default;

  friend std::ostream& operator<<(std::ostream& os, extent_data const& e) {
    os << e.info;
    if (e.xxh_hexdigest) {
      os << " (xxh3_64: " << *e.xxh_hexdigest << ")";
    }
    return os;
  }

  static extent_data make_hole(uint64_t size) {
    return extent_data{detail::file_extent_info{extent_kind::hole, {0, size}}};
  }
};

struct sparse_file {
  std::string_view name;
  std::vector<extent_data> extents;
};

std::vector<sparse_file> const expected_sparse_files = {
    {"20bits",
     {
         {{extent_kind::hole, {0, 1048575}}},
     }},
    {"data_only",
     {
         {{extent_kind::data, {0, 12288}}, "045aeca5c6fa5ca4"},
     }},
    {"data_then_hole",
     {
         {{extent_kind::data, {0, 12288}}, "045aeca5c6fa5ca4"},
         {{extent_kind::hole, {12288, 1048576}}},
     }},
    {"hole_data_hole_data_hole",
     {
         {{extent_kind::hole, {0, 1048576}}},
         {{extent_kind::data, {1048576, 12288}}, "045aeca5c6fa5ca4"},
         {{extent_kind::hole, {1060864, 1048576}}},
         {{extent_kind::data, {2109440, 12288}}, "045aeca5c6fa5ca4"},
         {{extent_kind::hole, {2121728, 1048576}}},
     }},
    {"hole_only",
     {
         {{extent_kind::hole, {0, 1048576}}},
     }},
    {"hole_then_data",
     {
         {{extent_kind::hole, {0, 1048576}}},
         {{extent_kind::data, {1048576, 12288}}, "045aeca5c6fa5ca4"},
     }},
    {"large_data_hole_data",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 999997440}}},
         {{extent_kind::data, {1000026112, 31231}}, "684ef6689e96bf0a"},
     }},
    {"large_data_hole_data2",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 999997440}}},
         {{extent_kind::data, {1000026112, 31231}}, "684ef6689e96bf0a"},
     }},
    {"large_data_hole_data3",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 999997440}}},
         {{extent_kind::data, {1000026112, 32255}}, "8bc4350eeb002bc2"},
     }},
    {"large_data_hole_data4",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 999997440}}},
         {{extent_kind::data, {1000026112, 32255}}, "c157878ec3cbd5d6"},
     }},
    {"large_data_then_hole",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 1073741824}}},
     }},
    {"large_hole_only",
     {
         {{extent_kind::hole, {0, 1073741824}}},
     }},
    {"large_hole_then_data",
     {
         {{extent_kind::hole, {0, 1073741824}}},
         {{extent_kind::data, {1073741824, 28672}}, "5f7fee76fb6a26a2"},
     }},
    {"very_large_data_hole_data",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 68719476736}}},
         {{extent_kind::data, {68719505408, 28672}}, "5f7fee76fb6a26a2"},
     }},
    {"very_large_data_hole_data2",
     {
         {{extent_kind::data, {0, 28672}}, "5f7fee76fb6a26a2"},
         {{extent_kind::hole, {28672, 68719476736}}},
         {{extent_kind::data, {68719505408, 28672}}, "b31214bc5ccb2e8c"},
     }},
};

std::vector<extent_data>
get_extents(reader::filesystem_v2 const& fs, reader::inode_view iv) {
  reader::detail::file_reader fr(fs, iv);
  std::vector<extent_data> extents;
  auto const inode = iv.inode_num();

  for (auto const& ei : fr.extents()) {
    auto& e = extents.emplace_back();

    e.info = ei;

    if (ei.kind == extent_kind::data) {
      auto const& range = ei.range;
      std::vector<std::byte> buffer(range.size());
      auto const num_read =
          fs.read(inode, reinterpret_cast<char*>(buffer.data()), range.size(),
                  range.offset());

      if (std::cmp_not_equal(num_read, range.size())) {
        throw std::runtime_error(
            "failed to read data for checksum calculation");
      }

      e.xxh_hexdigest = checksum(checksum::xxh3_64).update(buffer).hexdigest();
    }
  }

  return extents;
}

void verify_sparse_files(reader::filesystem_v2 const& fs) {
  for (auto const& esf : expected_sparse_files) {
    SCOPED_TRACE(esf.name);

    auto const file = fs.find(esf.name);
    ASSERT_TRUE(file);
    EXPECT_TRUE(file->inode().is_regular_file());

    auto const attr = fs.getattr(file->inode());
    auto const size = attr.size();

    EXPECT_EQ(esf.extents.back().info.range.end(), size);

    auto extents = get_extents(fs, file->inode());

    EXPECT_THAT(extents, ElementsAreArray(esf.extents));
  }
}

void verify_boundary_hole_files(reader::filesystem_v2 const& fs) {
  for (int bits = 1; bits <= 63; ++bits) {
    SCOPED_TRACE(fmt::format("bits={}", bits));

    auto const expected_size = (1ULL << bits) - 1;
    auto const file_name = fmt::format("/{}bits", bits);
    auto const file = fs.find(file_name);
    ASSERT_TRUE(file);
    EXPECT_TRUE(file->inode().is_regular_file());

    auto const attr = fs.getattr(file->inode());
    auto const size = attr.size();

    EXPECT_EQ(expected_size, size);

    auto extents = get_extents(fs, file->inode());

    EXPECT_THAT(extents, ElementsAre(extent_data::make_hole(expected_size)));
  }
}

} // namespace

TEST(mkdwarfs_test, build_with_sparse_files_no_sparse) {
  std::string const image_file = "test.dwarfs";
  std::mt19937_64 rng{42};

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file("/sparse", {
                                {extent_kind::data, 10'000, &rng},
                                {extent_kind::hole, 20'000},
                                {extent_kind::data, 10'000, &rng},
                            });

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "-l3", "--no-sparse-files"}))
      << t.err();
  auto fs = t.fs_from_file(image_file);

  auto dev = fs.find("/sparse");
  ASSERT_TRUE(dev);
  auto iv = dev->inode();
  EXPECT_TRUE(iv.is_regular_file());
  auto stat = fs.getattr(iv);
  EXPECT_EQ(40'000, stat.size());
  EXPECT_EQ(40'000, stat.allocated_size());

  auto const info = fs.info_as_json({});
  EXPECT_THAT(info["features"],
              Not(Contains(AnyOf("sparsefiles", "sparsefiles_new_lhm"))))
      << info.dump(2);

  vfs_stat vfs;
  fs.statvfs(&vfs);

  EXPECT_EQ(2, vfs.files); // root dir + sparse file
  EXPECT_EQ(1, vfs.frsize);
  EXPECT_EQ(40'000, vfs.blocks);
}

TEST(mkdwarfs_test, build_with_sparse_files) {
  std::string const image_file = "test.dwarfs";
  std::string image;
  std::mt19937_64 rng{42};

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file("/sparse", {
                                  {extent_kind::data, 10'000, &rng},
                                  {extent_kind::hole, 20'000},
                                  {extent_kind::data, 10'000, &rng},
                              });

    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "-l3"})) << t.err();
    image = t.get_file(image_file);
    auto fs = t.fs_from_file(image_file);

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(40'000, stat.size());
    EXPECT_EQ(20'000, stat.allocated_size());

    auto const info = fs.info_as_json({});
    auto const& features = info["features"];
    EXPECT_THAT(features, Contains("sparsefiles")) << info.dump(2);
    EXPECT_THAT(features, Not(Contains("sparsefiles_new_lhm"))) << info.dump(2);

    vfs_stat vfs;
    fs.statvfs(&vfs);

    EXPECT_EQ(2, vfs.files); // root dir + sparse file
    EXPECT_EQ(1, vfs.frsize);
    EXPECT_EQ(20'000, vfs.blocks);
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    return mkdwarfs_tester::create_with_image(image_data, image_file);
  };

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(40'000, stat.size());
    EXPECT_EQ(20'000, stat.allocated_size());

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    auto const& meta = info["full_metadata"];
    auto const& features = info["features"];
    EXPECT_TRUE(meta.find("large_hole_size") == meta.end()) << info.dump(2);
    EXPECT_THAT(features, Contains("sparsefiles")) << info.dump(2);
    EXPECT_THAT(features, Not(Contains("sparsefiles_new_lhm"))) << info.dump(2);

    vfs_stat vfs;
    fs.statvfs(&vfs);

    EXPECT_EQ(2, vfs.files); // root dir + sparse file
    EXPECT_EQ(1, vfs.frsize);
    EXPECT_EQ(20'000, vfs.blocks);
  }

  {
    auto t = rebuild_tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--no-sparse-files"}))
        << t.err();
    EXPECT_THAT(
        t.err(),
        HasSubstr(
            "cannot disable sparse files when the input filesystem uses them"));
  }
}

TEST(mkdwarfs_test, huge_sparse_file) {
  std::string const image_file = "test.dwarfs";
  std::string image;
  std::mt19937_64 rng{42};
  test_file_data tfd;
  file_size_t total_data_size{0};

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    std::uniform_int_distribution<file_size_t> data_size_dist(1, 2_KiB);
    std::exponential_distribution<double> hole_size_dist(1.0 / (2_GiB));
    for (int i = 0; i < 1'000; ++i) {
      auto const hs = 1 + static_cast<file_size_t>(hole_size_dist(rng));
      auto const ds = data_size_dist(rng);
      tfd.add_hole(hs);
      tfd.add_data(ds, &rng);
      total_data_size += ds;
    }
    t.os->add_file("/sparse", tfd);

    ASSERT_EQ(0,
              t.run({"-i", "/", "-o", image_file, "-l3", "-S16", "-C", "null"}))
        << t.err();
    image = t.get_file(image_file);
    auto fs = t.fs_from_file(image_file);

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(tfd.size(), stat.size());
    EXPECT_EQ(total_data_size, stat.allocated_size());

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_THAT(info["features"],
                AllOf(Contains("sparsefiles"), Contains("sparsefiles_new_lhm")))
        << info.dump(2);
    auto const& meta = info["full_metadata"];
    auto const& size_cache = meta["reg_file_size_cache"];
    ASSERT_EQ(1, size_cache["size_lookup"].size()) << info.dump(2);
    ASSERT_EQ(1, size_cache["allocated_size_lookup"].size()) << info.dump(2);
    EXPECT_EQ(tfd.size(), size_cache["size_lookup"][0][1].get<file_size_t>())
        << info.dump(2);
    EXPECT_EQ(total_data_size,
              size_cache["allocated_size_lookup"][0][1].get<file_size_t>())
        << info.dump(2);
    EXPECT_TRUE(meta.find("large_hole_size") != meta.end()) << info.dump(2);

    for (auto const& ext : tfd.extents) {
      if (ext.info.kind == extent_kind::data) {
        auto const size = ext.info.range.size();
        auto const offset = ext.info.range.offset();
        std::error_code ec;
        auto const data = fs.read_string(iv.inode_num(), size, offset, ec);
        EXPECT_FALSE(ec) << "error at offset " << offset << ": "
                         << ec.message();
        EXPECT_EQ(size, data.size()) << "size mismatch at offset " << offset;
        EXPECT_EQ(ext.data, data) << "data mismatch at offset " << offset;
      }
    }

    reader::detail::file_reader fr(fs, iv);

    EXPECT_THAT(tfd.extents | ranges::views::transform([](auto const& e) {
                  return e.info;
                }) | ranges::to<std::vector>(),
                ElementsAreArray(fr.extents()));

    vfs_stat vfs;
    fs.statvfs(&vfs);

    EXPECT_EQ(2, vfs.files); // root dir + sparse file
    EXPECT_EQ(1, vfs.frsize);
    EXPECT_EQ(total_data_size, vfs.blocks);
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    return mkdwarfs_tester::create_with_image(image_data, image_file);
  };

  for (int block_size : {20, 25, 13, 10, 17}) {
    // std::cerr << "=================================================\n";
    // std::cerr << "Rebuild with block size " << block_size << "\n";
    // std::cerr << "=================================================\n";

    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--change-block-size",
                        "-S", std::to_string(block_size), "-C", "null"}))
        << t.err();
    auto fs = t.fs_from_stdout({.metadata = {.check_consistency = true}});
    image = t.out();

    // fs.dump(std::cerr, {.features = reader::fsinfo_features::for_level(2)});

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());

    reader::detail::file_reader fr(fs, iv);

    EXPECT_THAT(tfd.extents | ranges::views::transform([](auto const& e) {
                  return e.info;
                }) | ranges::to<std::vector>(),
                ElementsAreArray(fr.extents()));

    auto stat = fs.getattr(iv);
    EXPECT_EQ(tfd.size(), stat.size());
    EXPECT_EQ(total_data_size, stat.allocated_size());

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});

    EXPECT_THAT(info["features"], Contains("sparsefiles")) << info.dump(2);
    auto const& size_cache = info["full_metadata"]["reg_file_size_cache"];
    ASSERT_EQ(1, size_cache["size_lookup"].size()) << info.dump(2);
    ASSERT_EQ(1, size_cache["allocated_size_lookup"].size()) << info.dump(2);
    EXPECT_EQ(tfd.size(), size_cache["size_lookup"][0][1].get<file_size_t>())
        << info.dump(2);
    EXPECT_EQ(total_data_size,
              size_cache["allocated_size_lookup"][0][1].get<file_size_t>())
        << info.dump(2);

    for (auto const& ext : tfd.extents) {
      if (ext.info.kind == extent_kind::data) {
        auto const size = ext.info.range.size();
        auto const offset = ext.info.range.offset();
        std::error_code ec;
        auto const data = fs.read_string(iv.inode_num(), size, offset, ec);
        EXPECT_FALSE(ec) << "error at offset " << offset << ": "
                         << ec.message();
        EXPECT_EQ(size, data.size()) << "size mismatch at offset " << offset;
        EXPECT_EQ(ext.data, data) << "data mismatch at offset " << offset;
      }
    }

    vfs_stat vfs;
    fs.statvfs(&vfs);

    EXPECT_EQ(2, vfs.files); // root dir + sparse file
    EXPECT_EQ(1, vfs.frsize);
    EXPECT_EQ(total_data_size, vfs.blocks);
  }
}

namespace {

struct sparse_hardlink_file {
  std::string_view path;
  std::string_view hardlink;
  expected_attrs attrs;
};

std::array<sparse_hardlink_file, 4> const sparse_hardlink_files{{
    {"/sparse1",
     "/hardlink1a",
     {.type = posix_file_type::regular,
      .size = 13_KiB + 5_GiB,
      .allocated_size = 13_KiB,
      .blocks = 13_KiB / 512,
      .nlink = 3}},
    {"/sparse2",
     "/hardlink2b",
     {.type = posix_file_type::regular,
      .size = 1_TiB,
      .allocated_size = 0,
      .blocks = 0,
      .nlink = 3}},
    {"/sparse3",
     "/hardlink3a",
     {.type = posix_file_type::regular,
      .size = 7_KiB + 500_GiB,
      .allocated_size = 7_KiB,
      .blocks = 7_KiB / 512,
      .nlink = 3}},
    {"/sparse4",
     "/hardlink4b",
     {.type = posix_file_type::regular,
      .size = 9_KiB + 30_GiB,
      .allocated_size = 9_KiB,
      .blocks = 9_KiB / 512,
      .nlink = 3}},
}};

// With sparse file support disabled, holes are indistinguishable from data,
// so the whole file appears to be allocated.
expected_attrs as_dense(expected_attrs attrs) {
  attrs.allocated_size = attrs.size;
  attrs.blocks = *attrs.size / 512;
  return attrs;
}

void expect_sparse_hardlinks(reader::filesystem_v2 const& fs, bool sparse) {
  for (auto const& f : sparse_hardlink_files) {
    auto const attrs = sparse ? f.attrs : as_dense(f.attrs);
    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{f.path, attrs}, {f.hardlink, attrs}}));
    EXPECT_NO_FATAL_FAILURE(expect_same_inode(fs, f.path, f.hardlink));
  }
}

} // namespace

TEST(mkdwarfs_test, sparse_files_hardlinks_metadata) {
  std::string const image_file = "test.dwarfs";
  std::string image;
  std::mt19937_64 rng{42};

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    auto const stat1 = t.os->add_file("/sparse1",
                                      {
                                          {extent_kind::data, 10_KiB, &rng},
                                          {extent_kind::hole, 5_GiB},
                                          {extent_kind::data, 3_KiB, &rng},
                                      },
                                      {.nlink = 3});
    auto const stat2 = t.os->add_file("/sparse2",
                                      {
                                          {extent_kind::hole, 1_TiB},
                                      },
                                      {.nlink = 3});
    auto const stat3 = t.os->add_file("/sparse3",
                                      {
                                          {extent_kind::hole, 500_GiB},
                                          {extent_kind::data, 7_KiB, nullptr},
                                      },
                                      {.nlink = 3});
    auto const stat4 = t.os->add_file("/sparse4",
                                      {
                                          {extent_kind::data, 9_KiB, nullptr},
                                          {extent_kind::hole, 30_GiB},
                                      },
                                      {.nlink = 3});
    t.os->add("/hardlink1a", stat1);
    t.os->add("/hardlink1b", stat1);
    t.os->add("/hardlink2a", stat2);
    t.os->add("/hardlink2b", stat2);
    t.os->add("/hardlink3a", stat3);
    t.os->add("/hardlink3b", stat3);
    t.os->add("/hardlink4a", stat4);
    t.os->add("/hardlink4b", stat4);

    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "-l3"})) << t.err();

    image = t.get_file(image_file);

    auto fs =
        t.fs_from_file(image_file, {.metadata = {.enable_sparse_files = true}});

    ASSERT_NO_FATAL_FAILURE(expect_sparse_hardlinks(fs, true));

    vfs_stat vfs;
    fs.statvfs(&vfs);

    EXPECT_EQ(5, vfs.files); // root dir + 4 files (no hardlinks)
    EXPECT_EQ(1, vfs.frsize);
    EXPECT_EQ(29_KiB, vfs.blocks);

    EXPECT_EQ(1_TiB + 535_GiB + 29_KiB, vfs.total_fs_size);
    EXPECT_EQ((1_TiB + 535_GiB + 29_KiB) * 2, vfs.total_hardlink_size);
    EXPECT_EQ(29_KiB, vfs.total_allocated_fs_size);
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    return mkdwarfs_tester::create_with_image(image_data, image_file);
  };

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata"}))
        << t.err();

    {
      auto fs = t.fs_from_stdout({.metadata = {.enable_sparse_files = true,
                                               .check_consistency = true}});

      ASSERT_NO_FATAL_FAILURE(expect_sparse_hardlinks(fs, true));

      {
        auto const dev = fs.find("/sparse1");
        ASSERT_TRUE(dev);
        auto const info = fs.get_inode_info(dev->inode());
        ASSERT_EQ(3, info["chunks"].size()) << info.dump(2);
        EXPECT_EQ("data", info["chunks"][0]["kind"]) << info.dump(2);
        EXPECT_EQ(10_KiB, info["chunks"][0]["size"].get<uint64_t>())
            << info.dump(2);
        EXPECT_EQ("hole", info["chunks"][1]["kind"]) << info.dump(2);
        EXPECT_EQ(5_GiB, info["chunks"][1]["size"].get<uint64_t>())
            << info.dump(2);
        EXPECT_EQ("data", info["chunks"][2]["kind"]) << info.dump(2);
        EXPECT_EQ(3_KiB, info["chunks"][2]["size"].get<uint64_t>())
            << info.dump(2);
      }

      vfs_stat vfs;
      fs.statvfs(&vfs);

      EXPECT_EQ(5, vfs.files); // root dir + 4 files (no hardlinks)
      EXPECT_EQ(1, vfs.frsize);
      EXPECT_EQ(29_KiB, vfs.blocks);
    }

    {
      auto fs = t.fs_from_stdout({.metadata = {.enable_sparse_files = false}});

      ASSERT_NO_FATAL_FAILURE(expect_sparse_hardlinks(fs, false));

      {
        auto const dev = fs.find("/sparse4");
        ASSERT_TRUE(dev);
        auto const iv = dev->inode();

        // seek is only supported with sparse files enabled
        EXPECT_THAT(
            [&] { fs.seek(iv.inode_num(), 0, reader::seek_whence::data); },
            testing::Throws<std::system_error>(testing::Property(
                &std::system_error::code,
                testing::Property(
                    &std::error_code::value,
                    testing::Eq(static_cast<int>(std::errc::not_supported))))));
      }

      vfs_stat vfs;
      fs.statvfs(&vfs);

      EXPECT_EQ(5, vfs.files); // root dir + 4 files (no hardlinks)
      EXPECT_EQ(1, vfs.frsize);
      EXPECT_EQ(29_KiB + 1559_GiB, vfs.blocks);
    }
  }
}

TEST(mkdwarfs_test, hollow_filesystem) {
  mkdwarfs_tester t;
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "--hollow"})) << t.err();

  auto fs = t.fs_from_stdout();

  auto ipsum = fs.find("/somedir/ipsum.py");
  ASSERT_TRUE(ipsum);
  EXPECT_TRUE(ipsum->inode().is_regular_file());

  auto ipsum_str = fs.read_string(fs.open(ipsum->inode()));
  EXPECT_EQ(10000, ipsum_str.size());
  EXPECT_THAT(ipsum_str, testing::Each('\0'));

  auto empty = fs.find("/empty");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->inode().is_regular_file());
  EXPECT_EQ(0, fs.getattr(empty->inode()).size());
  EXPECT_TRUE(fs.read_string(fs.open(empty->inode())).empty());
}

TEST(mkdwarfs_test, rebuild_with_new_large_hole_marker) {
  std::string const image_file = "sparse-v0.15.3.dwarfs";
  auto const sparse_image = test_dir / "compat" / image_file;
  auto image_data = read_file(sparse_image);

  {
    auto t = mkdwarfs_tester::create_empty();
    auto fs = t.fs_from_data(image_data);

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 20, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];
    EXPECT_THAT(meta["large_hole_size"], UnorderedElementsAre(64_GiB));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // We expect the old marker to consume 32 bits, even though it should
    // really consume at most 20 bits (i.e. block_size - 1).
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [15]"), HasSubstr("- offset [32]")));

    auto const& features = info["features"];

    EXPECT_THAT(features, UnorderedElementsAre("sparsefiles"));

    ASSERT_NO_FATAL_FAILURE(verify_sparse_files(fs));
  }

  {
    auto t = mkdwarfs_tester::create_with_image(image_data, image_file);

    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-S", "18", "-C",
                        "zstd:level=5", "--change-block-size"}))
        << t.err();

    auto fs = t.fs_from_stdout();

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 18, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];
    EXPECT_THAT(meta["large_hole_size"],
                UnorderedElementsAre(1_MiB - 1, 64_GiB));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // The new marker should consume *exactly* 18 bits in the offset field.
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [15]"), HasSubstr("- offset [18]")));

    auto const& features = info["features"];

    EXPECT_THAT(features,
                UnorderedElementsAre("sparsefiles", "sparsefiles_new_lhm"));

    ASSERT_NO_FATAL_FAILURE(verify_sparse_files(fs));
  }

  {
    auto t = mkdwarfs_tester::create_with_image(image_data, image_file);

    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-S", "22", "-C",
                        "zstd:level=5", "--change-block-size"}))
        << t.err();

    auto fs = t.fs_from_stdout();

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 22, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];
    EXPECT_TRUE(meta.find("large_hole_size") == meta.end()) << info.dump(2);

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // There is no marker and the offset field might use less bits than the
    // block size.
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [15]"), HasSubstr("- offset [21]")));

    auto const& features = info["features"];

    EXPECT_THAT(features, UnorderedElementsAre("sparsefiles"));

    ASSERT_NO_FATAL_FAILURE(verify_sparse_files(fs));
  }

  {
    auto t = mkdwarfs_tester::create_with_image(image_data, image_file);

    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-C", "zstd:level=5",
                        "--rebuild-metadata"}))
        << t.err();

    auto fs = t.fs_from_stdout();

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});

    // Block size should be unchanged.
    EXPECT_EQ(1 << 20, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];
    EXPECT_THAT(meta["large_hole_size"],
                UnorderedElementsAre(1_MiB - 1, 64_GiB));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // The new marker should consume *exactly* 20 bits in the offset field.
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [15]"), HasSubstr("- offset [20]")));

    auto const& features = info["features"];

    EXPECT_THAT(features,
                UnorderedElementsAre("sparsefiles", "sparsefiles_new_lhm"));

    ASSERT_NO_FATAL_FAILURE(verify_sparse_files(fs));
  }
}

TEST(mkdwarfs_test, rebuild_with_new_large_hole_marker_boundary_holes) {
  std::string const image_file = "sparse-holes-v0.15.3.dwarfs";
  auto const sparse_image = test_dir / "compat" / image_file;
  auto image_data = read_file(sparse_image);

  {
    auto t = mkdwarfs_tester::create_empty();
    auto fs = t.fs_from_data(image_data);

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 24, info["block_size"].get<int>());

    // (1 << 24) - 1 is the largest hole size that can be represented, since
    // the large hole marker is buggy and uses 31 bits for the size field.
    std::vector<uint64_t> expected_large_hole_sizes;
    for (uint64_t i = 25; i <= 63; ++i) {
      expected_large_hole_sizes.push_back((1ULL << i) - 1);
    }

    auto const& meta = info["full_metadata"];

    EXPECT_THAT(meta["large_hole_size"],
                ElementsAreArray(expected_large_hole_sizes));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // size = 6 since we have more than 31 holes that use a large hole size
    // offset = 32 because of the buggy large hole marker
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [6]"), HasSubstr("- offset [32]")));

    auto const& features = info["features"];

    EXPECT_THAT(features, UnorderedElementsAre("sparsefiles"));

    ASSERT_NO_FATAL_FAILURE(verify_boundary_hole_files(fs));
  }

  {
    auto t = mkdwarfs_tester::create_with_image(image_data, image_file);

    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-S", "18", "-C",
                        "zstd:level=5", "--change-block-size"}))
        << t.err();

    auto fs = t.fs_from_stdout();

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 18, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];

    // When rebuilding, (1 << 18) - 1 is *included*, since that's the
    // new large hole size marker.
    std::vector<uint64_t> expected_large_hole_sizes;
    for (uint64_t i = 18; i <= 63; ++i) {
      expected_large_hole_sizes.push_back((1ULL << i) - 1);
    }

    EXPECT_THAT(meta["large_hole_size"],
                ElementsAreArray(expected_large_hole_sizes));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // The new marker should consume *exactly* 18 bits in the offset field.
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [6]"), HasSubstr("- offset [18]")));

    auto const& features = info["features"];

    EXPECT_THAT(features,
                UnorderedElementsAre("sparsefiles", "sparsefiles_new_lhm"));

    ASSERT_NO_FATAL_FAILURE(verify_boundary_hole_files(fs));
  }

  {
    auto t = mkdwarfs_tester::create_with_image(image_data, image_file);

    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-C", "zstd:level=5",
                        "--rebuild-metadata"}))
        << t.err();

    auto fs = t.fs_from_stdout();

    EXPECT_EQ(0, fs.check(reader::filesystem_check_level::FULL));

    auto const info = fs.info_as_json(
        {.features = {reader::fsinfo_feature::metadata_summary,
                      reader::fsinfo_feature::metadata_full_dump}});
    EXPECT_EQ(1 << 24, info["block_size"].get<int>());

    auto const& meta = info["full_metadata"];

    // When rebuilding, (1 << 24) - 1 is *included*, since that's the
    // new large hole size marker.
    std::vector<uint64_t> expected_large_hole_sizes;
    for (uint64_t i = 24; i <= 63; ++i) {
      expected_large_hole_sizes.push_back((1ULL << i) - 1);
    }

    EXPECT_THAT(meta["large_hole_size"],
                ElementsAreArray(expected_large_hole_sizes));

    auto const analysis =
        fs.dump({.features = {reader::fsinfo_feature::frozen_analysis}});

    // The new marker should consume *exactly* 24 bits in the offset field.
    EXPECT_THAT(analysis,
                AllOf(HasSubstr("- size [6]"), HasSubstr("- offset [24]")));

    auto const& features = info["features"];

    EXPECT_THAT(features,
                UnorderedElementsAre("sparsefiles", "sparsefiles_new_lhm"));

    ASSERT_NO_FATAL_FAILURE(verify_boundary_hole_files(fs));
  }
}

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

#include <nlohmann/json.hpp>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/file_util.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/sorted_array_map.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

using namespace std::literals::string_view_literals;
using namespace dwarfs::binary_literals;

TEST(mkdwarfs_test, rebuild_metadata) {
  std::string const image_file = "test.dwarfs";
  std::string image;

  {
    mkdwarfs_tester t;
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--with-devices",
                        "--with-specials", "--keep-all-times", "-l3"}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    ASSERT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);

    auto dev = fs.find("/somedir/ipsum.py");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(10'000, stat.size());
    EXPECT_EQ(6001, stat.atime());
    EXPECT_EQ(6002, stat.mtime());
    EXPECT_EQ(6003, stat.ctime());
    EXPECT_EQ(1000, stat.uid());
    EXPECT_EQ(100, stat.gid());
    EXPECT_EQ(0644, stat.permissions());
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6001, stat.atime());
      EXPECT_EQ(6002, stat.mtime());
      EXPECT_EQ(6003, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000010001, stat.atime());
      EXPECT_EQ(4000020002, stat.mtime());
      EXPECT_EQ(4000030003, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(8001, stat.atime());
      EXPECT_EQ(8002, stat.mtime());
      EXPECT_EQ(8003, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }

    {
      auto analysis =
          fs.dump({.features = reader::fsinfo_features::for_level(2)});
      EXPECT_THAT(analysis,
                  ::testing::HasSubstr("1 metadata_version_history..."));
      EXPECT_THAT(analysis,
                  ::testing::HasSubstr("previous metadata versions:"));
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6002, stat.atime());
      EXPECT_EQ(6002, stat.mtime());
      EXPECT_EQ(6002, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000020002, stat.atime());
      EXPECT_EQ(4000020002, stat.mtime());
      EXPECT_EQ(4000020002, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(8002, stat.atime());
      EXPECT_EQ(8002, stat.mtime());
      EXPECT_EQ(8002, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--time-resolution=min"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6000, stat.atime());
      EXPECT_EQ(6000, stat.mtime());
      EXPECT_EQ(6000, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000020000, stat.atime());
      EXPECT_EQ(4000020000, stat.mtime());
      EXPECT_EQ(4000020000, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(7980, stat.atime());
      EXPECT_EQ(7980, stat.mtime());
      EXPECT_EQ(7980, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }

    auto t2 = rebuild_tester(t.out());
    EXPECT_EQ(0, t2.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                         "--time-resolution=sec"}))
        << t2.err();
    EXPECT_THAT(
        t2.err(),
        ::testing::HasSubstr("cannot increase time resolution from 60s to 1s"));
    auto fs2 = t2.fs_from_stdout();

    {
      auto dev = fs2.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6000, stat.atime());
      EXPECT_EQ(6000, stat.mtime());
      EXPECT_EQ(6000, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--set-time=98765"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(98765, stat.atime());
      EXPECT_EQ(98765, stat.mtime());
      EXPECT_EQ(98765, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(98765, stat.atime());
      EXPECT_EQ(98765, stat.mtime());
      EXPECT_EQ(98765, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(98765, stat.atime());
      EXPECT_EQ(98765, stat.mtime());
      EXPECT_EQ(98765, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--set-owner=123"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6002, stat.atime());
      EXPECT_EQ(6002, stat.mtime());
      EXPECT_EQ(6002, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000020002, stat.atime());
      EXPECT_EQ(4000020002, stat.mtime());
      EXPECT_EQ(4000020002, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(8002, stat.atime());
      EXPECT_EQ(8002, stat.mtime());
      EXPECT_EQ(8002, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--set-group=456"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6002, stat.atime());
      EXPECT_EQ(6002, stat.mtime());
      EXPECT_EQ(6002, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000020002, stat.atime());
      EXPECT_EQ(4000020002, stat.mtime());
      EXPECT_EQ(4000020002, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(8002, stat.atime());
      EXPECT_EQ(8002, stat.mtime());
      EXPECT_EQ(8002, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--set-owner=123", "--set-group=456",
                        "--keep-all-times", "--time-resolution=min"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6000, stat.atime());
      EXPECT_EQ(6000, stat.mtime());
      EXPECT_EQ(6000, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000009980, stat.atime());
      EXPECT_EQ(4000020000, stat.mtime());
      EXPECT_EQ(4000029960, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0666, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(7980, stat.atime());
      EXPECT_EQ(7980, stat.mtime());
      EXPECT_EQ(7980, stat.ctime());
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(0600, stat.permissions());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--chmod=a+r,go-w"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    {
      auto dev = fs.find("/somedir/ipsum.py");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(10'000, stat.size());
      EXPECT_EQ(6001, stat.atime());
      EXPECT_EQ(6002, stat.mtime());
      EXPECT_EQ(6003, stat.ctime());
      EXPECT_EQ(1000, stat.uid());
      EXPECT_EQ(100, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/somedir/zero");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(4000010001, stat.atime());
      EXPECT_EQ(4000020002, stat.mtime());
      EXPECT_EQ(4000030003, stat.ctime());
      EXPECT_EQ(0, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }

    {
      auto dev = fs.find("/baz.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      auto stat = fs.getattr(iv);
      EXPECT_EQ(8001, stat.atime());
      EXPECT_EQ(8002, stat.mtime());
      EXPECT_EQ(8003, stat.ctime());
      EXPECT_EQ(1337, stat.uid());
      EXPECT_EQ(0, stat.gid());
      EXPECT_EQ(0644, stat.permissions());
    }
  }
}

TEST(mkdwarfs_test, change_block_size) {
  DWARFS_SLOW_TEST();

  std::string const image_file = "test.dwarfs";
  std::string image;

  std::unordered_map<std::string, std::string> ref_checksums;

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_local_files(audio_data_dir);
    t.os->add_local_files(fits_data_dir);
    auto files = t.add_random_file_tree({.avg_size = 8192.0, .dimension = 13});
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--with-devices",
                        "--with-specials", "--keep-all-times", "--categorize",
                        "-S18", "-B3", "-l4"}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    ASSERT_TRUE(img);
    image = std::move(img.value());

    auto fs = t.fs_from_file(image_file);

    // std::cerr << "Image size: " << image.size() << " bytes\n" << t.err();
    // fs.dump(std::cerr,
    //         {.features = reader::fsinfo_features::for_level(3)});

    ref_checksums = get_md5_checksums(image);

    auto checksum_files =
        ref_checksums | ranges::views::keys |
        ranges::views::transform([](auto const& s) { return fs::path{s}; }) |
        ranges::to<std::set<fs::path>>();

    auto random_files =
        files | ranges::views::keys | ranges::to<std::set<fs::path>>();

    EXPECT_GT(checksum_files.size(), 1000);
    EXPECT_GT(random_files.size(), 1000);

    // All random files should be in the checksum set
    std::vector<fs::path> missing_files;
    std::set_difference(random_files.begin(), random_files.end(),
                        checksum_files.begin(), checksum_files.end(),
                        std::back_inserter(missing_files));

    EXPECT_EQ(0, missing_files.size());
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

#ifdef DWARFS_TEST_CROSS_COMPILE
  static constexpr int kMinBlockSize = 14;
  static constexpr int kMaxBlockSize = 20;
#else
  static constexpr int kMinBlockSize = 10;
  static constexpr int kMaxBlockSize = 26;
#endif

  for (int lg_block_size = kMinBlockSize; lg_block_size <= kMaxBlockSize;
       ++lg_block_size) {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "-S",
                        std::to_string(lg_block_size), "-C", "zstd:level=5",
                        "--change-block-size", "--keep-all-times"}))
        << t.err();

    {
      auto const checksums = get_md5_checksums(t.out());
      EXPECT_EQ(ref_checksums, checksums);

      auto fs = t.fs_from_stdout();
      auto info =
          fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});

      EXPECT_EQ(1 << lg_block_size, info["block_size"].get<int>());

      auto const& hist = info["meta"]["metadata_version_history"];

      ASSERT_EQ(1, hist.size());

      EXPECT_EQ(1 << 18, hist[0]["block_size"].get<int>());
    }

    auto t2 = rebuild_tester(t.out());
    ASSERT_EQ(0, t2.run({"-i", image_file, "-o", "-", "-S18", "-C",
                         "zstd:level=5", "--change-block-size",
                         "--keep-all-times", "--log-level=debug"}))
        << t2.err();

    {
      auto const checksums = get_md5_checksums(t2.out());
      EXPECT_EQ(ref_checksums, checksums);

      auto fs = t2.fs_from_stdout();
      auto info =
          fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});

      EXPECT_EQ(1 << 18, info["block_size"].get<int>());

      auto const& hist = info["meta"]["metadata_version_history"];

      ASSERT_EQ(2, hist.size());

      EXPECT_EQ(1 << 18, hist[0]["block_size"].get<int>());
      EXPECT_EQ(1 << lg_block_size, hist[1]["block_size"].get<int>());
    }

    if (lg_block_size == kMinBlockSize) {
      auto t3 = rebuild_tester(t2.out());
      ASSERT_EQ(
          0, t3.run({"-i", image_file, "-o", "-", "-S20", "-C", "zstd:level=5",
                     "--change-block-size", "--keep-all-times",
                     "--log-level=debug", "--no-metadata-version-history"}))
          << t3.err();

      {
        auto fs = t3.fs_from_stdout();
        auto info = fs.info_as_json(
            {.features = reader::fsinfo_features::for_level(3)});

        EXPECT_EQ(1 << 20, info["block_size"].get<int>());

        EXPECT_FALSE(info["meta"].contains("metadata_version_history"))
            << info.dump(2);
      }
    }
  }
}

namespace {

constexpr sorted_array_map catdata_md5{
    std::pair{"audio/test16-1.wav"sv, "f9c2148b3e0f2bb9527cc1ebc7ff18da"sv},
    std::pair{"audio/test16-2.aiff"sv, "330d63fc43b29d7b381b5ef2a0ae9339"sv},
    std::pair{"audio/test16-2.caf"sv, "3f54aa0f2536b7afcc960e05d36e304b"sv},
    std::pair{"audio/test16-2.w64"sv, "4e0c1b3e36b9c9db4354f36625881652"sv},
    std::pair{"audio/test16-2.wav"sv, "36a17673d35b669b60dfe720856d200c"sv},
    std::pair{"audio/test16-3.aiff"sv, "747278e7a8f186dea3729baa6481bc18"sv},
    std::pair{"audio/test16-3.caf"sv, "ff54ad5f905dd346c0d0a6d1346f1559"sv},
    std::pair{"audio/test16-3.w64"sv, "c25656ba400ce2a8ce9d98b1c244699f"sv},
    std::pair{"audio/test16-3.wav"sv, "355dba5f01c6fc592638c90b5d9927c0"sv},
    std::pair{"audio/test16-4.aiff"sv, "ad39b02769adce30a2fc13d2187401db"sv},
    std::pair{"audio/test16-4.caf"sv, "1ccf4adf466dbf81663d98254740fab6"sv},
    std::pair{"audio/test16-4.w64"sv, "811ddc3dd30fee42f970706d124463e3"sv},
    std::pair{"audio/test16-4.wav"sv, "56312ba7a4f9caa9ebe9d25617aecf27"sv},
    std::pair{"audio/test16-5.aiff"sv, "3bfbbcce59fcc6961641b6fd7d55a8e3"sv},
    std::pair{"audio/test16-5.caf"sv, "1f4b5637a02c548f53520239948bc930"sv},
    std::pair{"audio/test16-5.w64"sv, "f7e507129cd27eeb518b5b2073c03abf"sv},
    std::pair{"audio/test16-5.wav"sv, "1138ee1c2aeb767a422c5ba766d109ec"sv},
    std::pair{"audio/test16-6.aiff"sv, "e232fe1f468eff20485a83192a10801f"sv},
    std::pair{"audio/test16-6.caf"sv, "8db491beba24fbe20011dd5d16e806b0"sv},
    std::pair{"audio/test16-6.w64"sv, "8f3b3bef779e1159b8bd88e14c89ae3f"sv},
    std::pair{"audio/test16-6.wav"sv, "480c726c4b9b35aaa6a2b55adf339a03"sv},
    std::pair{"audio/test16.aiff"sv, "489527849947c117849fbeca3c9ac7ef"sv},
    std::pair{"audio/test16.caf"sv, "b7c8332fd5c0eace79c542e547333329"sv},
    std::pair{"audio/test16.w64"sv, "6d634a6bc3afd44829eaf0262a61d954"sv},
    std::pair{"audio/test16.wav"sv, "428a89911f1b5adee0af44d88688c989"sv},
    std::pair{"audio/test20-1.wav"sv, "85c257407bae30f9d549f33c8fd0f65e"sv},
    std::pair{"audio/test20-2.aiff"sv, "6206452ed20d7e8562a354862cfaf921"sv},
    std::pair{"audio/test20-2.caf"sv, "cd4cf8c942a3ae78e3c011853931477e"sv},
    std::pair{"audio/test20-2.w64"sv, "2d49579c0dc8d1ba899a0f2c01598a9d"sv},
    std::pair{"audio/test20-2.wav"sv, "88f1166f1663bdf848973afa1832c5f1"sv},
    std::pair{"audio/test20-3.aiff"sv, "2c87ec84de8a0b4925a8e54aef047870"sv},
    std::pair{"audio/test20-3.caf"sv, "bf4ee2da4214f470b82ba5db3aa0cec5"sv},
    std::pair{"audio/test20-3.w64"sv, "8c24cbcd243ea27f9689906963a95f6f"sv},
    std::pair{"audio/test20-3.wav"sv, "0c100083f8e40177531becdbd90beee6"sv},
    std::pair{"audio/test20-4.aiff"sv, "ac27c311680c072f81e7a7c2f84d5beb"sv},
    std::pair{"audio/test20-4.caf"sv, "2328b7adf7561be6cf18c337d1e8f5c3"sv},
    std::pair{"audio/test20-4.w64"sv, "5368727787fa9c815197f8a8be0b6b43"sv},
    std::pair{"audio/test20-4.wav"sv, "46c4f66bedd6979a0f0e6ea4e2a45757"sv},
    std::pair{"audio/test20-5.aiff"sv, "842d3568a933cf6c9a444e6381f965ce"sv},
    std::pair{"audio/test20-5.caf"sv, "8612d5c6c3ccfe8d0a3023bb1e9a309c"sv},
    std::pair{"audio/test20-5.w64"sv, "29e8ed10166a0c5c54f70d5575f99db0"sv},
    std::pair{"audio/test20-5.wav"sv, "83c65551cd036b65f42b9253cf0c3fa7"sv},
    std::pair{"audio/test20-6.aiff"sv, "4afc297effa20aae812a9c06f887c7a7"sv},
    std::pair{"audio/test20-6.caf"sv, "59c5e2ef11de2dfc55e6bac04fd9ef93"sv},
    std::pair{"audio/test20-6.w64"sv, "09b9221f335e93a3396eb09f8c110c09"sv},
    std::pair{"audio/test20-6.wav"sv, "0a17ac79445189644abc81aa90447eaf"sv},
    std::pair{"audio/test20.aiff"sv, "13e225a77a4a3b7f2fcdcace2623ca82"sv},
    std::pair{"audio/test20.caf"sv, "5e2f032ef1c1c9774de3496e04f27d55"sv},
    std::pair{"audio/test20.w64"sv, "7525edb6a8aa13df75c3ba14b8115281"sv},
    std::pair{"audio/test20.wav"sv, "87227330f105188d1ae62033f63a3e7d"sv},
    std::pair{"audio/test24-1.wav"sv, "a07b9011224a78caf91eec99eeb8305e"sv},
    std::pair{"audio/test24-2.aiff"sv, "f87dbe528f7688c81c6b60109c47d27d"sv},
    std::pair{"audio/test24-2.caf"sv, "9c081bff2722051579077050f37d078d"sv},
    std::pair{"audio/test24-2.w64"sv, "a7892a82bc0efb91a604436cb8e92da8"sv},
    std::pair{"audio/test24-2.wav"sv, "32b5b955c6355efeca113854380c5f30"sv},
    std::pair{"audio/test24-3.aiff"sv, "a80b4a76cb2cc1e805a8287f4b3ec857"sv},
    std::pair{"audio/test24-3.caf"sv, "2e4090d9bfd3fa92d0b16e4492f9ad50"sv},
    std::pair{"audio/test24-3.w64"sv, "a2a7832631b538b2016448b81fc61fd9"sv},
    std::pair{"audio/test24-3.wav"sv, "ace1e3a34533b729bf50439fb711dd61"sv},
    std::pair{"audio/test24-4.aiff"sv, "6e6d208c90ccee28002e73a87e161503"sv},
    std::pair{"audio/test24-4.caf"sv, "ec29f45cd4e4c4d93fb60bee411c0db1"sv},
    std::pair{"audio/test24-4.w64"sv, "63b82b9c2091732dfa7fbc7ba5e5c6b9"sv},
    std::pair{"audio/test24-4.wav"sv, "7743d363d3304ca681614ff3484a30b6"sv},
    std::pair{"audio/test24-5.aiff"sv, "52a80647bc76ba6343b1c0bafc8534be"sv},
    std::pair{"audio/test24-5.caf"sv, "656e7756c7b81728db76f0fcaa3b977b"sv},
    std::pair{"audio/test24-5.w64"sv, "9e11fcdb31051778e879cf0ee213f4cc"sv},
    std::pair{"audio/test24-5.wav"sv, "85d9e7c46ce6bc56a29bdf8232e0d851"sv},
    std::pair{"audio/test24-6.aiff"sv, "6fee950dffcf0066015856df846963b7"sv},
    std::pair{"audio/test24-6.caf"sv, "00b1326e361b49b52766fabe5be82f98"sv},
    std::pair{"audio/test24-6.w64"sv, "83d67e7880717a74a4409a5146e65e4f"sv},
    std::pair{"audio/test24-6.wav"sv, "320fe315c135f05d367970406fc6716e"sv},
    std::pair{"audio/test24.aiff"sv, "5b7c4b315b4edf5c5bd03b54f7e44b07"sv},
    std::pair{"audio/test24.caf"sv, "6874e90ff3aa9d566a25de5a6b5c78ac"sv},
    std::pair{"audio/test24.w64"sv, "609309cd6676f7ccf9325aa35b50be7f"sv},
    std::pair{"audio/test24.wav"sv, "189d2cad17ee00ecd2d842ae78a6e5cf"sv},
    std::pair{"audio/test32-1.wav"sv, "08e1813bc67544fd1118431faa7036dd"sv},
    std::pair{"audio/test32-2.aiff"sv, "04c5d051fb64520b46ca6715e0ea132c"sv},
    std::pair{"audio/test32-2.caf"sv, "d1b4ea74fecec67f96034760babfc0c7"sv},
    std::pair{"audio/test32-2.w64"sv, "0e503c61df3ecf32ef60c829e57f81bc"sv},
    std::pair{"audio/test32-2.wav"sv, "2d3adc5615fccf60570287ce15aef0c3"sv},
    std::pair{"audio/test32-3.aiff"sv, "e6249929dcfb827b31c740f63fc82cd2"sv},
    std::pair{"audio/test32-3.caf"sv, "b3d445f76326fe932892977cbbf934d7"sv},
    std::pair{"audio/test32-3.w64"sv, "e4282a828b68deda53ed0d71b3fe3403"sv},
    std::pair{"audio/test32-3.wav"sv, "d8c4692b2f5ed6d9d240143db674d6e9"sv},
    std::pair{"audio/test32-4.aiff"sv, "9708e947bc70f09371d941b9f9df29ff"sv},
    std::pair{"audio/test32-4.caf"sv, "7f1848a150eaf9d3051190afad4354f6"sv},
    std::pair{"audio/test32-4.w64"sv, "911afcd7438b60952bac9de1a3121621"sv},
    std::pair{"audio/test32-4.wav"sv, "46161eaa81b30eeec96d423c45983565"sv},
    std::pair{"audio/test32-5.aiff"sv, "d2354d3e2341ead476844d5adb3dfb13"sv},
    std::pair{"audio/test32-5.caf"sv, "51dcb09fedf7ae414d7ad860618556ce"sv},
    std::pair{"audio/test32-5.w64"sv, "4ea2f1fa063c05f67a5bbbf5b854f6fd"sv},
    std::pair{"audio/test32-5.wav"sv, "70f7106929f155126fa3b8550443fb8a"sv},
    std::pair{"audio/test32-6.aiff"sv, "43d8c8bf867a1ec03872dd2b6f83b6a0"sv},
    std::pair{"audio/test32-6.caf"sv, "31b6c7cf5e3414613a90de043fa129bd"sv},
    std::pair{"audio/test32-6.w64"sv, "a3d118d9aaf93cf70f502fb07f60e6ec"sv},
    std::pair{"audio/test32-6.wav"sv, "b158a2ee946a5b68f9ca992a34018729"sv},
    std::pair{"audio/test32.aiff"sv, "cd948369dca513e55828c7e65958a848"sv},
    std::pair{"audio/test32.caf"sv, "168effe99eafc9c4994d9e828ba847e8"sv},
    std::pair{"audio/test32.w64"sv, "ad79d9566c91bc7058d112e56a111499"sv},
    std::pair{"audio/test32.wav"sv, "24d5869a7376318a7e25e72b23f2a8a9"sv},
    std::pair{"audio/test8-1.wav"sv, "55a192e95b83951d215e1fa9c72c41c0"sv},
    std::pair{"audio/test8-2.aiff"sv, "9db33df311ed6f9389fc534a6c559172"sv},
    std::pair{"audio/test8-2.caf"sv, "c8c2c5fa60e40e295d81c31a1deb68bb"sv},
    std::pair{"audio/test8-2.w64"sv, "5e3a3a4c631a62ae992c7593978eb402"sv},
    std::pair{"audio/test8-2.wav"sv, "dd8f79a205dd067f06aeb66f04485aef"sv},
    std::pair{"audio/test8-3.aiff"sv, "424dd7aabed58151f98b186bfad4bba2"sv},
    std::pair{"audio/test8-3.caf"sv, "86cabf47c3dbe36e2a014f73d1cbf359"sv},
    std::pair{"audio/test8-3.w64"sv, "ba78d15742c7b1820754dcc582eb3d68"sv},
    std::pair{"audio/test8-3.wav"sv, "ed05b7d6dd7cc342faa149e25481f27b"sv},
    std::pair{"audio/test8-4.aiff"sv, "bb947d291c15604920814865439464fe"sv},
    std::pair{"audio/test8-4.caf"sv, "77ef57423fd8419d53a411fe86a636dd"sv},
    std::pair{"audio/test8-4.w64"sv, "9e3b1d3b967e15c226059606c930e7a3"sv},
    std::pair{"audio/test8-4.wav"sv, "e403f9b91cedd334696f56b5c8feb7f7"sv},
    std::pair{"audio/test8-5.aiff"sv, "13256745a30e8e38608ac3dbe517e320"sv},
    std::pair{"audio/test8-5.caf"sv, "5e94100af782d8fa1fb01464a87eb6c0"sv},
    std::pair{"audio/test8-5.w64"sv, "7d14ca30317b05462b54902ea76ed959"sv},
    std::pair{"audio/test8-5.wav"sv, "04a5ddb40d77c09bdeb384687434a1ad"sv},
    std::pair{"audio/test8-6.aiff"sv, "6651384f84d0d1f92f9ea3bf352d5ccb"sv},
    std::pair{"audio/test8-6.caf"sv, "76335125bd0cb0db003fda6d108af1a1"sv},
    std::pair{"audio/test8-6.w64"sv, "011bfb37cdd67d53cc686bee151e2c88"sv},
    std::pair{"audio/test8-6.wav"sv, "9f01ed7018221a453633bcaf86412ddf"sv},
    std::pair{"audio/test8.aiff"sv, "76a55b5645ea8fe8d3eab14ffdfec276"sv},
    std::pair{"audio/test8.caf"sv, "aab3a2b7c4781b8dc7600c73bd9ebefc"sv},
    std::pair{"audio/test8.w64"sv, "e54de8b1d704a556d8a95b081607007c"sv},
    std::pair{"audio/test8.wav"sv, "7d455a3b730013af1d2b02532bbc2997"sv},
    std::pair{"dwarfsextract.md"sv, "8b729b774a2db7f72f9f0111d727745b"sv},
    std::pair{"pcmaudio/test12.aiff"sv, "84795c79f52804a884c1f8906178f8a8"sv},
    std::pair{"pcmaudio/test12.caf"sv, "4855cfa1b322e39162e194d215fa93d2"sv},
    std::pair{"pcmaudio/test12.w64"sv, "c4e88844fc8e8d95674c38ba85f09372"sv},
    std::pair{"pcmaudio/test12.wav"sv, "5a39c2df63de6caee2bc844d88e98d8d"sv},
    std::pair{"pcmaudio/test16.aiff"sv, "84795c79f52804a884c1f8906178f8a8"sv},
    std::pair{"pcmaudio/test16.caf"sv, "4855cfa1b322e39162e194d215fa93d2"sv},
    std::pair{"pcmaudio/test16.w64"sv, "c4e88844fc8e8d95674c38ba85f09372"sv},
    std::pair{"pcmaudio/test16.wav"sv, "5a39c2df63de6caee2bc844d88e98d8d"sv},
    std::pair{"pcmaudio/test20.aiff"sv, "ee32abc285b1b7a943af8d6e006989a5"sv},
    std::pair{"pcmaudio/test20.caf"sv, "997e77cdf5425df454cd1c3abe6eda51"sv},
    std::pair{"pcmaudio/test20.w64"sv, "e388292faacd248914e628e14fd315fe"sv},
    std::pair{"pcmaudio/test20.wav"sv, "a4ece26f5446db93836a572647ab5132"sv},
    std::pair{"pcmaudio/test24.aiff"sv, "ee32abc285b1b7a943af8d6e006989a5"sv},
    std::pair{"pcmaudio/test24.caf"sv, "997e77cdf5425df454cd1c3abe6eda51"sv},
    std::pair{"pcmaudio/test24.w64"sv, "e388292faacd248914e628e14fd315fe"sv},
    std::pair{"pcmaudio/test24.wav"sv, "a4ece26f5446db93836a572647ab5132"sv},
    std::pair{"pcmaudio/test32.aiff"sv, "e0ff44422a17d4849ef15a45c6ae066c"sv},
    std::pair{"pcmaudio/test32.caf"sv, "d5c2519500c318f7250a52541176d797"sv},
    std::pair{"pcmaudio/test32.w64"sv, "30747ae5977982f5d39ea85e9a73d180"sv},
    std::pair{"pcmaudio/test32.wav"sv, "6a380566a3d8c50979838433c8007c78"sv},
    std::pair{"pcmaudio/test8.aiff"sv, "6cd42d7d18aec1d697a6dc20a5308bd0"sv},
    std::pair{"pcmaudio/test8.caf"sv, "c88b695d0f96c44017b08479d1da9484"sv},
    std::pair{"pcmaudio/test8.w64"sv, "c5f53ae69b7829b959ef1d611c44af79"sv},
    std::pair{"pcmaudio/test8.wav"sv, "79cd84e4670315f8639c0932ed4c8f74"sv},
    std::pair{"random"sv, "319b0d53fb1ccf63671c4efeb3b510d0"sv},
};

}

TEST(mkdwarfs_test, change_block_size_catdata) {
  DWARFS_SLOW_TEST();

  std::unordered_map<std::string, std::string> ref_checksums;

  for (auto const& [file, md5] : catdata_md5) {
    ref_checksums.emplace(file, md5);
  }

  std::string const image_file = "catdata.dwarfs";
  auto const catdata_image = test_dir / image_file;
  auto image0 = read_file(catdata_image);

  auto t0 = mkdwarfs_tester::create_empty();
  auto fs0 = t0.fs_from_data(image0);
  auto info0 =
      fs0.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image0), ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(1 << 24, info0["block_size"].get<int>());
  EXPECT_EQ(55, info0["sections"].size());

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  auto t1 = rebuild_tester(std::move(image0));
  ASSERT_EQ(0, t1.run({"-i", image_file, "-o", "-", "-S", "15", "-C",
                       "zstd:level=5", "-C", "pcmaudio/waveform::zstd:level=5",
                       "--change-block-size"}))
      << t1.err();
  auto image1 = t1.out();
  auto fs1 = t1.fs_from_stdout();
  auto info1 =
      fs1.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image1), ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(1 << 15, info1["block_size"].get<int>());
  EXPECT_EQ(1757, info1["sections"].size());

#ifdef DWARFS_HAVE_FLAC

  auto t1b = rebuild_tester(image1);
  ASSERT_EQ(0, t1b.run({"-i", image_file, "-o", "-", "-C", "zstd:level=5", "-S",
                        "15", "-C", "pcmaudio/waveform::flac:level=3",
                        "--change-block-size"}))
      << t1b.err();
  auto image1b = t1b.out();
  auto fs1b = t1b.fs_from_stdout();
  auto info1b =
      fs1b.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image1b),
              ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(1 << 15, info1b["block_size"].get<int>());
  EXPECT_EQ(1761, info1b["sections"].size());

  auto t1c = rebuild_tester(image1);
  ASSERT_EQ(0, t1c.run({"-i", image_file, "-o", "-", "-C", "zstd:level=5", "-S",
                        "16", "-C", "pcmaudio/waveform::flac:level=3",
                        "--change-block-size"}))
      << t1c.err();
  auto image1c = t1c.out();
  auto fs1c = t1c.fs_from_stdout();
  auto info1c =
      fs1c.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image1c),
              ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(1 << 16, info1c["block_size"].get<int>());
  EXPECT_EQ(897, info1c["sections"].size());

  EXPECT_LT(image1c.size(), image1b.size());

  image1b.clear();
  image1 = std::move(image1c);

#endif

  auto t2 = rebuild_tester(image1);
  ASSERT_EQ(0, t2.run({"-i", image_file, "-o", "-", "-S", "24", "-C",
                       "zstd:level=5", "-C", "pcmaudio/waveform::zstd:level=5",
                       "--change-block-size"}))
      << t2.err();
  auto image2 = t2.out();
  auto fs2 = t2.fs_from_stdout();
  auto info2 =
      fs2.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image2), ::testing::ContainerEq(ref_checksums));

  // Back to original block size and block count
  EXPECT_EQ(1 << 24, info2["block_size"].get<int>());
  EXPECT_EQ(55, info2["sections"].size());

#ifdef DWARFS_HAVE_FLAC

  auto t2b = rebuild_tester(image1);
  ASSERT_EQ(0, t2b.run({"-i", image_file, "-o", "-", "--recompress", "-C",
                        "pcmaudio/waveform::zstd:level=5", "--rebuild-metadata",
                        "--no-category-metadata"}))
      << t2b.err();
  auto image2b = t2b.out();
  auto fs2b = t2b.fs_from_stdout();
  auto info2b =
      fs2b.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  EXPECT_THAT(get_md5_checksums(image2b),
              ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(1 << 16, info2b["block_size"].get<int>());
  EXPECT_EQ(897, info2b["sections"].size());

  auto t2c = rebuild_tester(image2b);
  EXPECT_NE(0,
            t2c.run({"-i", image_file, "-o", "-", "-S", "24", "-C",
                     "pcmaudio/waveform::flac:level=3", "--change-block-size"}))
      << t2c.err();

  EXPECT_THAT(t2c.err(), ::testing::HasSubstr(
                             "cannot compress ZSTD compressed block with "
                             "compressor 'flac [level=3]' because the "
                             "following metadata requirements are not met"));

#endif
}

#if defined(DWARFS_HAVE_FLAC) || defined(DWARFS_HAVE_RICEPP)
TEST(mkdwarfs_test, recompress_with_metadata) {
  auto t = mkdwarfs_tester::create_empty();

  t.add_root_dir();
#ifdef DWARFS_HAVE_FLAC
  t.os->add_local_files(audio_data_dir);
#endif
#ifdef DWARFS_HAVE_RICEPP
  t.os->add_local_files(fits_data_dir);
#endif

  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "--categorize", "-l4"})) << t.err();

  auto image = t.out();
  auto const ref_checksums = get_md5_checksums(image);

  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  std::set<std::string> const expected_compressors{
      "NONE",
      "ZSTD",
#ifdef DWARFS_HAVE_FLAC
      "FLAC",
#endif
#ifdef DWARFS_HAVE_RICEPP
      "RICEPP",
#endif
  };
  std::set<std::string> compressors;

  for (auto const& sec : info["sections"]) {
    compressors.insert(sec["compression"].get<std::string>());
  }

  EXPECT_THAT(compressors, ::testing::ContainerEq(expected_compressors));

  std::string const image_file = "image.dwarfs";

  auto recompress_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  std::vector<std::string> args{"-i",           image_file, "-o",           "-",
                                "--recompress", "-C",       "zstd:level=11"};
#ifdef DWARFS_HAVE_FLAC
  args.push_back("-C");
  args.push_back("pcmaudio/waveform::zstd:level=11");
#endif
#ifdef DWARFS_HAVE_RICEPP
  args.push_back("-C");
  args.push_back("fits/image::zstd:level=11");
#endif

  auto t2 = recompress_tester(image);
  ASSERT_EQ(0, t2.run(args)) << t2.err();

  auto image2 = t2.out();
  auto fs2 = t2.fs_from_stdout();
  auto info2 =
      fs2.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  compressors.clear();

  size_t pcmaudio_blocks{0};
  size_t fits_blocks{0};

  for (auto const& sec : info2["sections"]) {
    compressors.insert(sec["compression"].get<std::string>());
    if (sec["type"] == "BLOCK") {
      ASSERT_TRUE(sec.contains("category"));
      if (sec["category"] == "pcmaudio/waveform") {
        ++pcmaudio_blocks;
        ASSERT_TRUE(sec.contains("metadata"));
        EXPECT_TRUE(sec["metadata"].contains("bits_per_sample"));
      }
      if (sec["category"] == "fits/image") {
        ++fits_blocks;
        ASSERT_TRUE(sec.contains("metadata"));
        EXPECT_TRUE(sec["metadata"].contains("component_count"));
      }
    }
  }

#ifdef DWARFS_HAVE_FLAC
  EXPECT_GT(pcmaudio_blocks, 0);
#else
  EXPECT_EQ(pcmaudio_blocks, 0);
#endif
#ifdef DWARFS_HAVE_RICEPP
  EXPECT_GT(fits_blocks, 0);
#else
  EXPECT_EQ(fits_blocks, 0);
#endif

  EXPECT_THAT(compressors, ::testing::ElementsAre("NONE", "ZSTD"));
  EXPECT_THAT(get_md5_checksums(image2), ::testing::ContainerEq(ref_checksums));

  args = std::vector<std::string>{
      "-i", image_file, "-o", "-", "--recompress", "-C", "zstd:level=11"};
#ifdef DWARFS_HAVE_FLAC
  args.push_back("-C");
  args.push_back("pcmaudio/waveform::flac:level=3");
#endif
#ifdef DWARFS_HAVE_RICEPP
  args.push_back("-C");
  args.push_back("fits/image::ricepp");
#endif
  auto t3 = recompress_tester(image2);
  ASSERT_EQ(0, t3.run(args)) << t3.err();

  auto image3 = t3.out();
  auto fs3 = t3.fs_from_stdout();
  auto info3 =
      fs3.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  compressors.clear();

  for (auto const& sec : info3["sections"]) {
    compressors.insert(sec["compression"].get<std::string>());
  }

  EXPECT_THAT(compressors, testing::ContainerEq(expected_compressors));
  EXPECT_THAT(get_md5_checksums(image3), ::testing::ContainerEq(ref_checksums));

  EXPECT_EQ(3, info3["history"].size());

  auto t4 = recompress_tester(image3);
  ASSERT_EQ(0, t4.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                       "--no-category-names"}))
      << t4.err();

  auto fs4 = t4.fs_from_stdout();

  auto info4 =
      fs4.info_as_json({.features = reader::fsinfo_features::for_level(3)});

  for (auto const& sec : info4["sections"]) {
    EXPECT_FALSE(sec.contains("category"));
  }
}
#endif

TEST(mkdwarfs_test, no_timestamps) {
  {
    mkdwarfs_tester t;
    EXPECT_EQ(
        0, t.run("-i / -o - -l2 --no-create-timestamp --no-history-timestamps"))
        << t.err();
    auto fs = t.fs_from_stdout();
    auto info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_FALSE(info.contains("created_on"));
    ASSERT_EQ(1, info["history"].size());
    EXPECT_FALSE(info["history"][0].contains("timestamp"));
  }

  {
    mkdwarfs_tester t;
    EXPECT_EQ(0, t.run("-i / -o - -l2")) << t.err();
    auto fs = t.fs_from_stdout();
    auto info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_TRUE(info.contains("created_on"));
    ASSERT_EQ(1, info["history"].size());
    EXPECT_TRUE(info["history"][0].contains("timestamp"));

    auto t2 = mkdwarfs_tester::create_empty();
    t2.add_root_dir();
    t2.os->add_file("test.dwarfs", t.out());

    EXPECT_EQ(
        0, t2.run({"-i", "test.dwarfs", "-o", "-", "-l2", "--rebuild-metadata",
                   "--no-create-timestamp", "--no-history-timestamps"}))
        << t2.err();
    auto fs2 = t2.fs_from_stdout();
    auto info2 =
        fs2.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_FALSE(info2.contains("created_on"));
    ASSERT_EQ(2, info2["history"].size());
    EXPECT_FALSE(info2["history"][0].contains("timestamp"));
    EXPECT_FALSE(info2["history"][1].contains("timestamp"));
  }
}

TEST(mkdwarfs_test, empty_filesystem) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  EXPECT_EQ(0, t.run("-i / -o -")) << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(0, info["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info["block_count"].get<int>());
  EXPECT_EQ(16_MiB, info["block_size"].get<int>());
  EXPECT_EQ(1, info["inode_count"].get<int>());
  EXPECT_EQ(4, info["sections"].size());

  auto t2 = mkdwarfs_tester::create_empty();
  t2.add_root_dir();
  t2.os->add_file("test.dwarfs", t.out());
  EXPECT_EQ(0, t2.run({"-i", "test.dwarfs", "-o", "-", "--rebuild-metadata"}))
      << t2.err();
  auto fs2 = t2.fs_from_stdout();
  auto info2 =
      fs2.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(0, info2["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info2["block_count"].get<int>());
  EXPECT_EQ(16_MiB, info2["block_size"].get<int>());
  EXPECT_EQ(1, info2["inode_count"].get<int>());
  EXPECT_EQ(4, info2["sections"].size());

  auto t3 = mkdwarfs_tester::create_empty();
  t3.add_root_dir();
  t3.os->add_file("test.dwarfs", t2.out());
  EXPECT_EQ(0, t3.run({"-i", "test.dwarfs", "-o", "-", "--rebuild-metadata",
                       "-S10", "--change-block-size"}))
      << t3.err();
  auto fs3 = t3.fs_from_stdout();
  auto info3 =
      fs3.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(0, info3["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info3["block_count"].get<int>());
  EXPECT_EQ(1_KiB, info3["block_size"].get<int>());
  EXPECT_EQ(1, info3["inode_count"].get<int>());
  EXPECT_EQ(4, info3["sections"].size());
}

TEST(mkdwarfs_test, minimal_empty_filesystem) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  EXPECT_EQ(
      0,
      t.run("-i / -o - --no-create-timestamp --no-history --no-section-index"))
      << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(0, info["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info["block_count"].get<int>());
  EXPECT_EQ(1, info["inode_count"].get<int>());
  EXPECT_EQ(2, info["sections"].size());

  auto t2 = mkdwarfs_tester::create_empty();
  t2.add_root_dir();
  t2.os->add_file("test.dwarfs", t.out());
  EXPECT_EQ(0, t2.run({"-i", "test.dwarfs", "-o", "-", "--rebuild-metadata",
                       "--no-create-timestamp", "--no-history",
                       "--no-section-index"}))
      << t2.err();
  auto fs2 = t2.fs_from_stdout();
  auto info2 =
      fs2.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(0, info2["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info2["block_count"].get<int>());
  EXPECT_EQ(1, info2["inode_count"].get<int>());
  EXPECT_EQ(2, info2["sections"].size());
}

TEST(mkdwarfs_test, metadata_only_filesystem) {
  static constexpr size_t kTotalSymlinkSize{273};
  static constexpr size_t kTotalInodeCount{276};

  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree(false);
  t.add_special_files(false);

  EXPECT_EQ(0, t.run("-i / -o - --with-devices --with-specials")) << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(kTotalSymlinkSize, info["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info["block_count"].get<int>());
  EXPECT_EQ(16_MiB, info["block_size"].get<int>());
  EXPECT_EQ(kTotalInodeCount, info["inode_count"].get<int>());
  EXPECT_EQ(4, info["sections"].size());

  auto t2 = mkdwarfs_tester::create_empty();
  t2.add_root_dir();
  t2.os->add_file("test.dwarfs", t.out());
  EXPECT_EQ(0, t2.run({"-i", "test.dwarfs", "-o", "-", "--rebuild-metadata"}))
      << t2.err();
  auto fs2 = t2.fs_from_stdout();
  auto info2 =
      fs2.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(kTotalSymlinkSize, info2["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info2["block_count"].get<int>());
  EXPECT_EQ(16_MiB, info2["block_size"].get<int>());
  EXPECT_EQ(kTotalInodeCount, info2["inode_count"].get<int>());
  EXPECT_EQ(4, info2["sections"].size());

  auto t3 = mkdwarfs_tester::create_empty();
  t3.add_root_dir();
  t3.os->add_file("test.dwarfs", t2.out());
  EXPECT_EQ(0, t3.run({"-i", "test.dwarfs", "-o", "-", "--rebuild-metadata",
                       "-S10", "--change-block-size"}))
      << t3.err();
  auto fs3 = t3.fs_from_stdout();
  auto info3 =
      fs3.info_as_json({.features = reader::fsinfo_features::for_level(3)});
  EXPECT_EQ(kTotalSymlinkSize, info3["original_filesystem_size"].get<int>());
  EXPECT_EQ(0, info3["block_count"].get<int>());
  EXPECT_EQ(1_KiB, info3["block_size"].get<int>());
  EXPECT_EQ(kTotalInodeCount, info3["inode_count"].get<int>());
  EXPECT_EQ(4, info3["sections"].size());

  size_t symlink_size{0};
  fs3.walk([&](reader::dir_entry_view const& e) {
    auto iv = e.inode();
    if (iv.is_symlink()) {
      symlink_size += fs.getattr(iv).size();
    }
  });

  EXPECT_EQ(kTotalSymlinkSize, symlink_size);
}

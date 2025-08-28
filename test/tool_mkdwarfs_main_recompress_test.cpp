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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <gmock/gmock.h>

#include <boost/algorithm/string.hpp>

#include <fmt/format.h>

#include <dwarfs/history.h>
#include <dwarfs/reader/fsinfo_options.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

class mkdwarfs_recompress_test
    : public testing::TestWithParam<std::string_view> {};

TEST_P(mkdwarfs_recompress_test, recompress) {
  std::string const compression{GetParam()};
  std::string compression_type = compression;
  std::string const image_file = "test.dwarfs";
  std::string image;
  reader::fsinfo_options const info_opts{
      .features = {reader::fsinfo_feature::history,
                   reader::fsinfo_feature::section_details}};

  if (auto pos = compression_type.find(':'); pos != std::string::npos) {
    compression_type.erase(pos);
  }
  boost::algorithm::to_upper(compression_type);
  if (compression_type == "NULL") {
    compression_type = "NONE";
  }

  auto get_block_compression = [](nlohmann::json const& info) {
    std::map<std::string, std::set<std::string>> ccmap;
    for (auto const& sec : info["sections"]) {
      if (sec["type"] == "BLOCK") {
        ccmap[sec["category"].get<std::string>()].insert(
            sec["compression"].get<std::string>());
      }
    }
    return ccmap;
  };

  std::set<std::string> const waveform_compressions{
#ifdef DWARFS_HAVE_FLAC
      "FLAC",
#else
      "ZSTD",
      "NONE",
#endif
  };

  std::string const fits_compression{
#ifdef DWARFS_HAVE_RICEPP
      "RICEPP",
#else
      "ZSTD",
#endif
  };

  std::string const l1_compression{
#ifdef DWARFS_HAVE_LIBLZ4
      "LZ4",
#else
      "ZSTD",
#endif
  };

  {
    mkdwarfs_tester t;
    t.os->add_local_files(audio_data_dir);
    t.os->add_local_files(fits_data_dir);
    t.os->add_file("random", test::create_random_string(4096));
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize", "-C",
                        compression}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    EXPECT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);
    auto info = fs.info_as_json(info_opts);
    auto history = info["history"];
    EXPECT_EQ(1, history.size());

    auto ccmap = get_block_compression(info);
    std::map<std::string, std::set<std::string>> const expected_ccmap{
        {"<default>", {compression_type}},
        {"incompressible", {"NONE"}},
        {"pcmaudio/waveform", waveform_compressions},
        {"pcmaudio/metadata", {compression_type}},
        {"fits/image", {fits_compression}},
        {"fits/metadata", {compression_type}},
    };
    EXPECT_EQ(expected_ccmap, ccmap);
  }

  auto tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress", "-l0"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    auto info = fs.info_as_json(info_opts);
    auto history = info["history"];
    EXPECT_EQ(2, history.size());

    auto ccmap = get_block_compression(info);
    std::map<std::string, std::set<std::string>> const expected_ccmap{
        {"<default>", {"NONE"}},         {"incompressible", {"NONE"}},
        {"pcmaudio/waveform", {"NONE"}}, {"pcmaudio/metadata", {"NONE"}},
        {"fits/image", {"NONE"}},        {"fits/metadata", {"NONE"}},
    };
    EXPECT_EQ(expected_ccmap, ccmap);
  }

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress", "-l1"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    auto info = fs.info_as_json(info_opts);
    auto history = info["history"];
    EXPECT_EQ(2, history.size());

    auto ccmap = get_block_compression(info);
    std::map<std::string, std::set<std::string>> const expected_ccmap{
        {"<default>", {l1_compression}},
        {"incompressible", {"NONE"}},
        {"pcmaudio/waveform", waveform_compressions},
        {"pcmaudio/metadata", {l1_compression}},
        {"fits/image", {fits_compression}},
        {"fits/metadata", {l1_compression}},
    };
    EXPECT_EQ(expected_ccmap, ccmap);
  }

  {
    auto t = tester(image);
    EXPECT_NE(0, t.run({"-i", image_file, "-o", "-", "--recompress=foo"}));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid recompress mode"));
  }

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=metadata"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
  }

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::null", "-l1"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));

    auto info = fs.info_as_json(info_opts);
    auto ccmap = get_block_compression(info);
    std::map<std::string, std::set<std::string>> const expected_ccmap{
        {"<default>", {l1_compression}},
        {"incompressible", {"NONE"}},
        {"pcmaudio/waveform", waveform_compressions},
        {"pcmaudio/metadata", {"NONE"}},
        {"fits/image", {fits_compression}},
        {"fits/metadata", {l1_compression}},
    };
    EXPECT_EQ(expected_ccmap, ccmap);
  }

#ifdef DWARFS_HAVE_FLAC
  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::flac:level=4"}))
        << t.err();
    EXPECT_THAT(t.err(),
                ::testing::HasSubstr(fmt::format(
                    "cannot compress {} compressed block with compressor 'flac "
                    "[level=4]' because the following metadata requirements "
                    "are not met: missing requirement 'bits_per_sample'",
                    compression_type)));
  }
#endif

#ifdef DWARFS_HAVE_RICEPP
  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::ricepp"}))
        << t.err();
    EXPECT_THAT(
        t.err(),
        ::testing::HasSubstr(fmt::format(
            "cannot compress {} compressed block with compressor 'ricepp "
            "[block_size=128]' because the following metadata requirements are "
            "not met: missing requirement 'bytes_per_sample'",
            compression_type)));
  }
#endif

  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress",
                        "--recompress-categories=pcmaudio/metadata,SoMeThInG"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr(
                             "no category 'SoMeThInG' in input filesystem"));
  }

  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress", "-C",
                        "SoMeThInG::null"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("unknown category: 'SoMeThInG'"));
  }

  {
    auto t = tester(image);
    EXPECT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=none",
                        "--log-level=verbose", "--no-history"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    EXPECT_EQ(0, fs.get_history().size());
    EXPECT_EQ(1, fs.info_as_json(info_opts).count("history"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("removing HISTORY"));

    auto t2 = tester(t.out());
    EXPECT_EQ(0, t2.run({"-i", image_file, "-o", "-", "--recompress=none",
                         "--log-level=verbose"}))
        << t.err();
    auto fs2 = t2.fs_from_stdout();
    EXPECT_TRUE(fs2.find("/random"));
    EXPECT_EQ(1, fs2.get_history().size());
    EXPECT_THAT(t2.err(), ::testing::HasSubstr("adding HISTORY"));
  }

  {
    auto corrupt_image = image;
    corrupt_image[64] ^= 0x01; // flip a bit right after the header
    auto t = tester(corrupt_image);
    EXPECT_NE(0, t.run({"-i", image_file, "-o", "-", "--recompress"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("input filesystem is corrupt"));
  }
}

namespace {

constexpr std::array<std::string_view, 2> const source_fs_compression = {
    "zstd:level=5",
    "null",
};

} // namespace

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_recompress_test,
                         ::testing::ValuesIn(source_fs_compression));

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

#include <cstddef>
#include <cstring>

#include <gmock/gmock.h>

#include <boost/algorithm/string.hpp>

#include <fmt/format.h>

#include <dwarfs/checksum.h>
#include <dwarfs/fstypes.h>
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
    ASSERT_TRUE(img);
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

    ASSERT_TRUE(info.contains("valid_section_index"));
    EXPECT_TRUE(info["valid_section_index"].get<bool>());
  }

  auto tester = [&image_file](std::string const& image_data) {
    return mkdwarfs_tester::create_with_image(image_data, image_file);
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
    EXPECT_EQ(
        0, t.run({"-i", image_file, "-o", "-", "--recompress=none",
                  "--log-level=verbose", "--no-history", "--no-section-index"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    EXPECT_EQ(0, fs.get_history().size());
    EXPECT_EQ(1, fs.info_as_json(info_opts).count("history"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("removing HISTORY"));

    auto info = fs.info_as_json(info_opts);
    ASSERT_TRUE(info.contains("valid_section_index"));
    EXPECT_FALSE(info["valid_section_index"].get<bool>());

    auto t2 = tester(t.out());
    EXPECT_EQ(0, t2.run({"-i", image_file, "-o", "-", "--recompress=none",
                         "--log-level=verbose"}))
        << t.err();
    auto fs2 = t2.fs_from_stdout();
    EXPECT_TRUE(fs2.find("/random"));
    EXPECT_EQ(1, fs2.get_history().size());
    EXPECT_THAT(t2.err(), ::testing::HasSubstr("adding HISTORY"));

    auto info2 = fs2.info_as_json(info_opts);
    ASSERT_TRUE(info2.contains("valid_section_index"));
    EXPECT_TRUE(info2["valid_section_index"].get<bool>());
  }

  {
    auto corrupt_image = image;
    corrupt_image[64] ^= 0x01; // flip a bit right after the header
    auto t = tester(corrupt_image);
    EXPECT_NE(0, t.run({"-i", image_file, "-o", "-", "--recompress"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("input filesystem is corrupt"));
  }

  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress", "-C",
                        "SoMeThInG::null"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("unknown category: 'SoMeThInG'"));
  }
}

TEST(mkdwarfs_recompress_test, preserves_minor_version_of_unknown_section) {
  // No section index so we can append an unknown section below
  auto image = build_test_image({"--no-history", "--no-section-index"});
  auto input = mkdwarfs_tester::create_with_image(image, "input.dwarfs");
  auto input_fs = input.fs_from_file("input.dwarfs");

  ASSERT_EQ(input_fs.version(), FS_VERSION_CURRENT);

  ASSERT_FALSE(input_fs.has_valid_section_index());
  reader::fsinfo_options const info_opts{
      .features = {reader::fsinfo_feature::section_details}};
  auto const section_number = static_cast<std::uint32_t>(
      input_fs.info_as_json(info_opts)["sections"].size());

  // Append a "future" (but accepted) section type
  constexpr auto unknown_type = static_cast<section_type>(0x7fff);
  constexpr std::string_view payload = "future section contents";
  section_header_v2 hdr{};
  std::memcpy(hdr.magic.data(), "DWARFS", hdr.magic.size());
  hdr.major = MAJOR_VERSION;
  hdr.minor = MINOR_VERSION_ACCEPTED;
  hdr.number = section_number;
  hdr.type = static_cast<uint16_t>(unknown_type);
  hdr.compression = static_cast<uint16_t>(compression_type::NONE);
  hdr.length = payload.size();

  checksum xxh(checksum::xxh3_64);
  xxh.update(&hdr.number, sizeof(hdr) - offsetof(section_header_v2, number));
  xxh.update(payload.data(), payload.size());
  ASSERT_TRUE(xxh.finalize(&hdr.xxh3_64));

  checksum sha(checksum::sha2_512_256);
  sha.update(&hdr.xxh3_64, sizeof(hdr) - offsetof(section_header_v2, xxh3_64));
  sha.update(payload.data(), payload.size());
  ASSERT_TRUE(sha.finalize(hdr.sha2_512_256.data()));

  image.append(reinterpret_cast<char const*>(&hdr), sizeof(hdr));
  image.append(payload.data(), payload.size());

  for (bool const with_index : {true, false}) {
    auto t = mkdwarfs_tester::create_with_image(image, "input.dwarfs");
    std::vector<std::string> args{"-i", "input.dwarfs",      "-o",
                                  "-",  "--recompress=none", "--no-history"};
    if (!with_index) {
      args.emplace_back("--no-section-index");
    }
    ASSERT_EQ(0, t.run(args)) << t.err();

    auto result = t.fs_from_stdout();
    EXPECT_EQ(result.version(), FS_VERSION_CURRENT);
    EXPECT_TRUE(
        result.check(reader::filesystem_check_level::CHECKSUM).passed());

    EXPECT_EQ(result.has_valid_section_index(), with_index);

    // The public reader API does not expose individual section headers,
    // and we cannot use internal headers in this test.
    // Find our unique payload and inspect the preceding on-disk header.
    auto const output = t.out();
    auto const payload_pos = output.find(payload);
    ASSERT_NE(payload_pos, std::string::npos);
    ASSERT_GE(payload_pos, sizeof(section_header_v2));
    EXPECT_EQ(output.find(payload, payload_pos + payload.size()),
              std::string::npos);

    section_header_v2 copied{};
    std::memcpy(&copied, output.data() + payload_pos - sizeof(copied),
                sizeof(copied));
    EXPECT_EQ(std::string_view(copied.magic.data(), copied.magic.size()),
              "DWARFS");
    EXPECT_EQ(copied.major, MAJOR_VERSION);
    EXPECT_EQ(copied.minor, MINOR_VERSION_ACCEPTED);
    EXPECT_EQ(copied.type, static_cast<std::uint16_t>(unknown_type));
    EXPECT_EQ(copied.number, section_number);
    EXPECT_EQ(copied.compression,
              static_cast<std::uint16_t>(compression_type::NONE));
    EXPECT_EQ(copied.length, payload.size());
    image = output;
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

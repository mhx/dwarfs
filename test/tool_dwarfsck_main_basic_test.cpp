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

#include <regex>

#include <gmock/gmock.h>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/file_util.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/string.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

TEST(dwarfsck_test, check_exclusive) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--no-check", "--check-integrity"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "--no-check and --check-integrity are mutually exclusive"));
}

TEST(dwarfsck_test, print_header_and_json) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header", "--json"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::ContainsRegex(
                  "--print-header is mutually exclusive with.*--json"));
}

TEST(dwarfsck_test, print_header) {
  std::string const header = "interesting stuff in the header\n";
  auto image =
      build_test_image({"--header", "header.txt"}, {{"header.txt", header}});

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--print-header"})) << t.err();
    EXPECT_EQ(header, t.out());
  }

  {
    auto t = dwarfsck_tester::create_with_image(image);
    t.iol->out_stream().setstate(std::ios_base::failbit);
    EXPECT_EQ(1, t.run({"image.dwarfs", "--print-header"})) << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("error writing header"));
  }
}

TEST(dwarfsck_test, check_fail) {
  static constexpr size_t section_header_size{64};
  auto image = build_test_image();

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs"})) << t.err();
  }

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--check-integrity"})) << t.err();
  }

  std::vector<std::pair<std::string, size_t>> section_offsets;

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--no-check", "-j", "-d3"})) << t.err();

    auto info = nlohmann::json::parse(t.out());
    ASSERT_TRUE(info.count("sections") > 0) << info;

    size_t offset = 0;

    for (auto const& section : info["sections"]) {
      auto type = section["type"].get<std::string>();
      auto size = section["compressed_size"].get<int>();
      section_offsets.emplace_back(type, offset);
      offset += section_header_size + size;
    }

    EXPECT_EQ(image.size(), offset);
  }

  size_t index = 0;

  for (auto const& [type, offset] : section_offsets) {
    bool const is_metadata_section =
        type == "METADATA_V2" || type == "METADATA_V2_SCHEMA";
    bool const is_block = type == "BLOCK";
    auto corrupt_image = image;
    // flip a bit right after the header
    corrupt_image[offset + section_header_size] ^= 0x01;

    // std::cout << "corrupting section: " << type << " @ " << offset << "\n";

    {
      test::test_logger lgr;
      test::os_access_mock os;
      auto make_fs = [&] {
        return reader::filesystem_v2{lgr, os,
                                     test::make_mock_file_view(corrupt_image)};
      };
      if (is_metadata_section) {
        EXPECT_THAT([&] { make_fs(); },
                    ::testing::ThrowsMessage<dwarfs::runtime_error>(
                        ::testing::HasSubstr(fmt::format(
                            "checksum error in section: {}", type))));
      } else {
        auto fs = make_fs();
        auto& log = lgr.get_log();
        if (is_block) {
          EXPECT_EQ(0, log.size());
        } else {
          ASSERT_EQ(1, log.size());
          EXPECT_THAT(log[0].output,
                      ::testing::HasSubstr(
                          fmt::format("checksum error in section: {}", type)));
        }
        auto info = fs.info_as_json(
            {.features = reader::fsinfo_features::for_level(3)});
        ASSERT_EQ(1, info.count("sections"));
        ASSERT_EQ(section_offsets.size(), info["sections"].size());
        for (auto const& [i, section] :
             ranges::views::enumerate(info["sections"])) {
          EXPECT_EQ(section["checksum_ok"].get<bool>(), i != index)
              << type << ", " << index;
        }
        auto dump =
            fs.dump({.features = reader::fsinfo_features::for_level(3)});
        EXPECT_THAT(dump, ::testing::HasSubstr("CHECKSUM ERROR"));
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      if (is_metadata_section) {
        EXPECT_EQ(1, t.run({"image.dwarfs", "--no-check", "-j"})) << t.err();
      } else {
        EXPECT_EQ(0, t.run({"image.dwarfs", "--no-check", "-j"})) << t.err();
      }

      // for blocks, we skip checks with --no-check
      if (!is_block) {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "checksum error in section: {}", type)));
      }

      auto json = t.out();

      // std::cout << "[" << type << ", nocheck]\n" << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "-j"})) << t.err();

      EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                               "checksum error in section: {}", type)));

      auto json = t.out();

      // std::cout << "[" << type << "]\n" << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "--check-integrity", "-j"}))
          << t.err();

      if (is_block) {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "integrity check error in section: BLOCK")));
      } else {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "checksum error in section: {}", type)));
      }

      auto json = t.out();

      // std::cout << "[" << type << ", integrity]\n"  << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "-d3"})) << t.err();

      EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                               "checksum error in section: {}", type)));

      if (is_metadata_section) {
        EXPECT_EQ(0, t.out().size()) << t.out();
      } else {
        EXPECT_THAT(t.out(), ::testing::HasSubstr("CHECKSUM ERROR"));
      }
    }

    ++index;
  }
}

TEST(dwarfsck_test, print_header_and_export_metadata) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header",
                      "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(
      t.err(),
      ::testing::ContainsRegex(
          "--print-header is mutually exclusive with.*--export-metadata"));
}

TEST(dwarfsck_test, print_header_and_check_integrity) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header", "--check-integrity"}))
      << t.err();
  EXPECT_THAT(
      t.err(),
      ::testing::ContainsRegex(
          "--print-header is mutually exclusive with.*--check-integrity"));
}

TEST(dwarfsck_test, print_header_no_header) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(2, t.run({"image.dwarfs", "--print-header"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("filesystem does not contain a header"));
}

TEST(dwarfsck_test, export_metadata) {
  auto t = dwarfsck_tester::create_with_image();
  ASSERT_EQ(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  auto meta = t.fa->get_file("image.meta");
  ASSERT_TRUE(meta);
  EXPECT_GT(meta->size(), 1000);
  EXPECT_TRUE(nlohmann::json::accept(meta.value())) << meta.value();
}

TEST(dwarfsck_test, export_metadata_open_error) {
  auto t = dwarfsck_tester::create_with_image();
  t.fa->set_open_error(
      "image.meta", std::make_error_code(std::errc::device_or_resource_busy));
  EXPECT_NE(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("failed to open metadata output file"));
}

TEST(dwarfsck_test, export_metadata_close_error) {
  auto t = dwarfsck_tester::create_with_image();
  t.fa->set_close_error("image.meta",
                        std::make_error_code(std::errc::no_space_on_device));
  EXPECT_NE(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("failed to close metadata output file"));
}

TEST(dwarfsck_test, checksum_algorithm_not_available) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--checksum=grmpf"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("checksum algorithm not available: grmpf"));
}

TEST(dwarfsck_test, list_files) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--list"})) << t.err();
  auto out = t.out();

  auto files = split_to<std::set<std::string>>(out, '\n');
  files.erase("");

  std::set<std::string> const expected{
      "test.pl",     "somelink",      "somedir",   "foo.pl",
      "bar.pl",      "baz.pl",        "ipsum.txt", "somedir/ipsum.py",
      "somedir/bad", "somedir/empty", "empty",
  };

  EXPECT_EQ(expected, files);
}

TEST(dwarfsck_test, list_files_verbose) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--list", "--verbose"})) << t.err();
  auto out = t.out();

  auto num_lines = std::count(out.begin(), out.end(), '\n');
  EXPECT_EQ(12, num_lines);
  auto format_time = [](time_t t) {
    return fmt::format("{:%F %H:%M}", safe_localtime(t));
  };

  std::vector<std::string> expected_re{
      fmt::format("drwxrwxrwx\\s+1000/100\\s+8\\s+{}\\s*\n", format_time(2)),
      fmt::format("-rw-------\\s+1337/  0\\s+{:L}\\s+{}\\s+baz.pl\n", 23456,
                  format_time(8002)),
      fmt::format("lrwxrwxrwx\\s+1000/100\\s+16\\s+{}\\s+somelink -> "
                  "somedir/ipsum.py\n",
                  format_time(2002)),
  };

  for (auto const& str : expected_re) {
    std::regex re{str};
    EXPECT_TRUE(std::regex_search(out, re)) << "[" << str << "]\n" << out;
  }
}

TEST(dwarfsck_test, checksum_files) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--checksum=md5"})) << t.err();
  auto out = t.out();

  auto num_lines = std::count(out.begin(), out.end(), '\n');
  EXPECT_EQ(8, num_lines);

  std::map<std::string, std::string> actual;

  for (auto line : split_to<std::vector<std::string_view>>(out, '\n')) {
    if (line.empty()) {
      continue;
    }
    auto pos = line.find("  ");
    ASSERT_NE(std::string::npos, pos);
    auto hash = line.substr(0, pos);
    auto file = line.substr(pos + 2);
    EXPECT_TRUE(actual.emplace(file, hash).second);
  }

  std::map<std::string, std::string> const expected{
      {"empty", "d41d8cd98f00b204e9800998ecf8427e"},
      {"somedir/empty", "d41d8cd98f00b204e9800998ecf8427e"},
      {"test.pl", "d41d8cd98f00b204e9800998ecf8427e"},
      {"baz.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"somedir/ipsum.py", "70fe813c36ed50ebd7f4991857683676"},
      {"foo.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"bar.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"ipsum.txt", "0782b6a546cedd8be8fc86ac47dc6d96"},
  };

  EXPECT_EQ(expected, actual);
}

TEST(dwarfsck_test, bug_sentinel_self_entry_nonzero) {
  auto const bug_file =
      test_dir / "bugs" / "dir-sentinel-self-entry-nonzero.dwarfs";
  auto bug_image = read_file(bug_file);
  auto t = dwarfsck_tester::create_with_image(bug_image);
  EXPECT_EQ(0, t.run({"image.dwarfs"})) << t.err();
  EXPECT_THAT(
      t.err(),
      ::testing::HasSubstr(
          "self_entry for sentinel directory should be 0, but is 2, this is "
          "harmless and can be fixed by rebuilding the metadata"))
      << t.err();
}

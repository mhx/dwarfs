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

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs_tool_main.h"

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;
// namespace po = boost::program_options;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";

class tool_main_test : public testing::Test {
 public:
  void SetUp() override { iol = std::make_unique<test::test_iolayer>(); }

  void TearDown() override { iol.reset(); }

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::unique_ptr<test::test_iolayer> iol;
};

} // namespace

class mkdwarfs_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "mkdwarfs");
    return mkdwarfs_main(args, iol->get());
  }
};

class dwarfsck_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsck");
    return dwarfsck_main(args, iol->get());
  }
};

class dwarfsextract_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsextract");
    return dwarfsextract_main(args, iol->get());
  }
};

TEST_F(mkdwarfs_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsck_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsextract_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(mkdwarfs_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsck_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsextract_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(mkdwarfs_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--long-help"));
  // check that the detailed help is not shown
  EXPECT_THAT(out(), ::testing::Not(::testing::HasSubstr("Advanced options:")));
  EXPECT_THAT(out(),
              ::testing::Not(::testing::HasSubstr("Compression algorithms:")));
}

TEST_F(mkdwarfs_main_test, cmdline_long_help_arg) {
  auto exit_code = run({"--long-help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Advanced options:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression level defaults:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression algorithms:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Categories:"));
}

TEST_F(dwarfsck_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
}

TEST_F(dwarfsextract_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
}

class categorizer_test : public testing::TestWithParam<std::string> {};

TEST_P(categorizer_test, end_to_end) {
  auto level = GetParam();

  auto input = std::make_shared<test::os_access_mock>();

  input->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  input->add_local_files(audio_data_dir);
  input->add_file("random", 4096, true);

  auto fa = std::make_shared<test::test_file_access>();
  test::test_iolayer iolayer(input, fa);

  auto args = test::parse_args(fmt::format(
      "mkdwarfs -i / -o test.dwarfs --categorize --log-level={}", level));
  auto exit_code = mkdwarfs_main(args, iolayer.get());

  EXPECT_EQ(exit_code, 0);

  auto fsimage = fa->get_file("test.dwarfs");

  EXPECT_TRUE(fsimage);

  auto mm = std::make_shared<test::mmap_mock>(std::move(fsimage.value()));

  test::test_logger lgr;
  filesystem_v2 fs(lgr, mm);

  auto iv16 = fs.find("/test8.aiff");
  auto iv32 = fs.find("/test8.caf");

  EXPECT_TRUE(iv16);
  EXPECT_TRUE(iv32);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, categorizer_test,
                         ::testing::Values("error", "warn", "info", "verbose",
                                           "debug", "trace"));

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

#include <folly/experimental/TestUtil.h>

#include "dwarfs/file_access.h"
#include "dwarfs/file_access_generic.h"
#include "test_helpers.h"

using namespace dwarfs;
namespace fs = std::filesystem;

TEST(file_access_generic_test, basic) {
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());

  auto text_file = td / "test.txt";
  auto binary_file = td / "test.bin";

  auto fa = create_file_access_generic();

  {
    auto text_os = fa->open_output(text_file);
    text_os->os() << "line1" << std::endl;
    text_os->os() << "line2" << std::endl;
    text_os->close();
  }

  auto binary_data = test::create_random_string(4096);

  {
    auto binary_os = fa->open_output_binary(binary_file);
    binary_os->os().write(binary_data.data(), binary_data.size());
    binary_os->close();
  }

  EXPECT_TRUE(fa->exists(text_file));
  EXPECT_TRUE(fa->exists(binary_file));

  EXPECT_TRUE(std::filesystem::exists(text_file));
  EXPECT_TRUE(std::filesystem::exists(binary_file));

  EXPECT_FALSE(fa->exists(td / "nonexistent"));

  auto binary_size = std::filesystem::file_size(binary_file);

  {
    auto text_is = fa->open_input(text_file);
    std::string line;
    std::getline(text_is->is(), line);
    EXPECT_EQ(line, "line1");
    std::getline(text_is->is(), line);
    EXPECT_EQ(line, "line2");
    text_is->close();
  }

  {
    auto binary_is = fa->open_input_binary(binary_file);
    std::string data;
    data.resize(binary_size);
    binary_is->is().read(data.data(), data.size());
    EXPECT_EQ(data, binary_data);
    binary_is->close();
  }
}

TEST(file_access_generic_test, error_handling) {
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());

  auto nonexistent_file = td / "nonexistent";
  auto file_in_subdir = td / "subdir" / "test.txt";

  auto fa = create_file_access_generic();

  {
    auto matcher = testing::Throws<std::system_error>(testing::Property(
        &std::system_error::code,
        testing::Property(&std::error_code::value,
                          testing::Eq(static_cast<int>(
                              std::errc::no_such_file_or_directory)))));

    EXPECT_THAT([&] { fa->open_input(nonexistent_file); }, matcher);
    EXPECT_THAT([&] { fa->open_input_binary(nonexistent_file); }, matcher);
    EXPECT_THAT([&] { fa->open_output(file_in_subdir); }, matcher);
    EXPECT_THAT([&] { fa->open_output_binary(file_in_subdir); }, matcher);
  }

  {
    auto matcher = testing::Property(
        &std::system_error::code,
        testing::Property(&std::error_code::value,
                          testing::Eq(static_cast<int>(
                              std::errc::no_such_file_or_directory))));

    {
      std::error_code ec;
      auto x [[maybe_unused]] = fa->open_input(nonexistent_file, ec);
      EXPECT_THAT(ec, matcher);
    }
    {
      std::error_code ec;
      auto x [[maybe_unused]] = fa->open_input_binary(nonexistent_file, ec);
      EXPECT_THAT(ec, matcher);
    }
    {
      std::error_code ec;
      auto x [[maybe_unused]] = fa->open_output(file_in_subdir, ec);
      EXPECT_THAT(ec, matcher);
    }
    {
      std::error_code ec;
      auto x [[maybe_unused]] = fa->open_output_binary(file_in_subdir, ec);
      EXPECT_THAT(ec, matcher);
    }
  }
}

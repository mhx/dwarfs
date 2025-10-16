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
#include <gtest/gtest.h>

#include "test_helpers.h"

namespace fs = std::filesystem;

using namespace dwarfs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Property;
using ::testing::Throws;

TEST(test_iolayer, file_access) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;

  tfa->set_file("/test/file1", "Hello World!\n");
  tfa->set_file("/test/error", "something");
  tfa->set_open_error("/test/error", std::make_error_code(std::errc::io_error));
  tfa->set_close_error(
      "/test/file1", std::make_error_code(std::errc::device_or_resource_busy));
  tfa->set_close_error("/test/file3",
                       std::make_error_code(std::errc::bad_address));
  tfa->set_open_error("/test/file4",
                      std::make_error_code(std::errc::bad_file_descriptor));

  EXPECT_THAT(
      [&] { fa->open_input_binary("/test/does_not_exist"); },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));

  EXPECT_THAT([&] { fa->open_output_binary(fs::path{}); },
              Throws<std::system_error>(Property(
                  &std::system_error::code,
                  Eq(std::make_error_code(std::errc::invalid_argument)))));

  EXPECT_THAT([&] { fa->open_input("/test/error"); },
              Throws<std::system_error>(
                  Property(&std::system_error::code,
                           Eq(std::make_error_code(std::errc::io_error)))));

  EXPECT_THAT([&] { fa->open_output("/test/file4"); },
              Throws<std::system_error>(Property(
                  &std::system_error::code,
                  Eq(std::make_error_code(std::errc::bad_file_descriptor)))));

  {
    auto in = fa->open_input_binary("/test/file1");
    std::vector<std::string> lines;
    while (in->is().good()) {
      std::string line;
      std::getline(in->is(), line);
      lines.push_back(line);
    }
    std::error_code ec;
    in->close(ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec, std::make_error_code(std::errc::device_or_resource_busy));
    EXPECT_THAT(lines, ElementsAre("Hello World!", ""));
  }

  {
    auto out = fa->open_output("/test/file2");
    out->os() << "Line 1\nLine 2\n";
    out->close(); // close again, should be fine
  }

  auto f2 = tfa->get_file("/test/file2");
  ASSERT_TRUE(f2.has_value());
  EXPECT_EQ(*f2, "Line 1\nLine 2\n");

  {
    auto out = fa->open_output("/test/file3");
    EXPECT_THAT([&] { out->close(); },
                Throws<std::system_error>(Property(
                    &std::system_error::code,
                    Eq(std::make_error_code(std::errc::bad_address)))));
  }
}

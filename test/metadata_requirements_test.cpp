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

#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/compression_metadata_requirements.h"

using namespace dwarfs;

TEST(metadata_requirements, dynamic_test) {
  std::string requirements = R"({
    "compression": ["set", ["lz4", "zstd"]],
    "block_size": ["range", 16, 1024]
  })";

  std::unique_ptr<compression_metadata_requirements<folly::dynamic>> req;

  ASSERT_NO_THROW(
      req = std::make_unique<compression_metadata_requirements<folly::dynamic>>(
          requirements));
  {
    std::string metadata = R"({
      "compression": "lz4",
      "block_size": 256
    })";

    EXPECT_NO_THROW(req->check(metadata));
  }

  {
    std::string metadata = R"({
      "compression": "lz4",
      "foo": "bar",
      "block_size": 256
    })";

    EXPECT_NO_THROW(req->check(metadata));
  }

  {
    std::string metadata = R"({
      "compression": "lzma",
      "block_size": 256
    })";

    EXPECT_THAT(
        [&]() { req->check(metadata); },
        ThrowsMessage<std::runtime_error>(testing::HasSubstr(
            "compression 'lzma' does not meet requirements [lz4, zstd]")));
  }

  {
    std::string metadata = R"({
      "block_size": 256
    })";

    EXPECT_THAT([&]() { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(
                    testing::HasSubstr("missing requirement 'compression'")));
  }

  {
    std::string metadata = R"({
      "compression": "zstd",
      "block_size": 8
    })";

    EXPECT_THAT([&]() { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "block_size '8' does not meet requirements [16, 1024]")));
  }
}

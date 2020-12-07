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

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include <folly/json.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"

namespace {

char const* reference = R"(
{
  "root": {
    "entries": [
      {
        "inode": 11,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "bench.sh",
        "size": 1517,
        "type": "file"
      },
      {
        "entries": [],
        "inode": 1,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "dev",
        "type": "directory"
      },
      {
        "entries": [
          {
            "entries": [],
            "inode": 3,
            "mode": 16877,
            "modestring": "---drwxr-xr-x",
            "name": "alsoempty",
            "type": "directory"
          }
        ],
        "inode": 2,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "empty",
        "type": "directory"
      },
      {
        "entries": [
          {
            "inode": 5,
            "mode": 41471,
            "modestring": "---lrwxrwxrwx",
            "name": "bad",
            "target": "../foo",
            "type": "link"
          },
          {
            "inode": 7,
            "mode": 33188,
            "modestring": "----rw-r--r--",
            "name": "bar",
            "size": 0,
            "type": "file"
          },
          {
            "inode": 11,
            "mode": 33188,
            "modestring": "----rw-r--r--",
            "name": "bla.sh",
            "size": 1517,
            "type": "file"
          }
        ],
        "inode": 4,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "foo",
        "type": "directory"
      },
      {
        "inode": 6,
        "mode": 41471,
        "modestring": "---lrwxrwxrwx",
        "name": "foobar",
        "target": "foo/bar",
        "type": "link"
      },
      {
        "inode": 8,
        "mode": 33261,
        "modestring": "----rwxr-xr-x",
        "name": "format.sh",
        "size": 94,
        "type": "file"
      },
      {
        "inode": 10,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "perl-exec.sh",
        "size": 87,
        "type": "file"
      },
      {
        "inode": 9,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "test.py",
        "size": 1012,
        "type": "file"
      }
    ],
    "inode": 0,
    "mode": 16877,
    "modestring": "---drwxr-xr-x",
    "type": "directory"
  },
  "statvfs": {
    "f_blocks": 4240,
    "f_bsize": 1048576,
    "f_files": 12
  }
}
)";

std::vector<std::string> versions{
    "0.2.0",
    "0.2.3",
};

} // namespace

using namespace dwarfs;

class compat : public testing::TestWithParam<std::string> {};

TEST_P(compat, backwards_compatibility) {
  std::ostringstream oss;
  stream_logger lgr(oss);
  auto filename =
      std::string(TEST_DATA_DIR "/compat-v") + GetParam() + ".dwarfs";
  filesystem_v2 fs(lgr, std::make_shared<mmap>(filename));
  auto meta = fs.metadata_as_dynamic();
  auto ref = folly::parseJson(reference);
  EXPECT_EQ(ref, meta);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, compat, ::testing::ValuesIn(versions));

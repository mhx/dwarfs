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

#include <sstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/block_compressor.h>
#include <dwarfs/config.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/writer_progress.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

TEST(filesystem_writer, compression_metadata_requirements) {
  using writer::filesystem_writer;

  test::test_logger lgr;
  auto os = test::os_access_mock::create_test_instance();
  writer::writer_progress prog;
  thread_pool pool(lgr, *os, "worker", 1);
  std::ostringstream devnull;

  block_compressor bcnull("null");

  EXPECT_NO_THROW(filesystem_writer(devnull, lgr, pool, prog));

#ifdef DWARFS_HAVE_FLAC
  block_compressor bcflac("flac:level=1");

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::METADATA_V2_SCHEMA, bcflac);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'flac [level=1]' for schema compression because "
          "compression metadata requirements are not met: missing requirement "
          "'bits_per_sample'")));

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::METADATA_V2, bcflac);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'flac [level=1]' for metadata compression because "
          "compression metadata requirements are not met: missing requirement "
          "'bits_per_sample'")));

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::HISTORY, bcflac);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'flac [level=1]' for history compression because "
          "compression metadata requirements are not met: missing requirement "
          "'bits_per_sample'")));
#endif

#ifdef DWARFS_HAVE_RICEPP
  block_compressor bcrice("ricepp");

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::METADATA_V2_SCHEMA, bcrice);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'ricepp [block_size=128]' for schema compression because "
          "compression metadata requirements are not met: missing requirement "
          "'bytes_per_sample'")));

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::METADATA_V2, bcrice);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'ricepp [block_size=128]' for metadata compression "
          "because "
          "compression metadata requirements are not met: missing requirement "
          "'bytes_per_sample'")));

  EXPECT_THAT(
      [&] {
        auto fsw = filesystem_writer(devnull, lgr, pool, prog);
        fsw.add_section_compressor(section_type::HISTORY, bcrice);
      },
      testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
          "cannot use 'ricepp [block_size=128]' for history compression "
          "because "
          "compression metadata requirements are not met: missing requirement "
          "'bytes_per_sample'")));
#endif
}

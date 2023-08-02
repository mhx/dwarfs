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

#include <exception>
#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <folly/String.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/mmap.h"

#include "test_logger.h"

using namespace dwarfs;
using testing::MatchesRegex;

namespace fs = std::filesystem;

auto test_dir = fs::path(TEST_DATA_DIR);

TEST(pcmaudio_categorizer, requirements) {
  test::test_logger logger(logger::INFO);
  boost::program_options::variables_map vm;
  auto& catreg = categorizer_registry::instance();
  auto catmgr = categorizer_manager(logger);

  catmgr.add(catreg.create(logger, "pcmaudio", vm));

  try {
    catmgr.set_metadata_requirements(
        catmgr.category_value("pcmaudio/metadata").value(),
        R"({"endianness": ["set", ["big"]], "bytes_per_sample": ["range", 2, 3]})");
    FAIL() << "expected std::runtime_error";
  } catch (std::runtime_error const& e) {
    EXPECT_STREQ(
        "unsupported metadata requirements: bytes_per_sample, endianness",
        e.what());
  } catch (...) {
    FAIL() << "unexpected exception: "
           << folly::exceptionStr(std::current_exception());
  }

  catmgr.set_metadata_requirements(
      catmgr.category_value("pcmaudio/waveform").value(),
      R"({"endianness": ["set", ["mixed"]], "bytes_per_sample": ["range", 2, 3]})");

  auto wav = test_dir / "pcmaudio" / "test16.wav";
  auto mm = mmap(wav);

  {
    auto job = catmgr.job(wav);
    job.set_total_size(mm.size());

    EXPECT_TRUE(logger.empty());

    job.categorize_random_access(mm.span());
    auto frag = job.result();

    ASSERT_EQ(1, logger.get_log().size());
    auto const& ent = logger.get_log().front();
    EXPECT_EQ(logger::WARN, ent.level);
    EXPECT_THAT(
        ent.output,
        MatchesRegex(
            R"(^\[WAV\] ".*": endianness 'little' does not meet requirements$)"));

    EXPECT_TRUE(frag.empty());

    logger.clear();
  }

  catmgr.set_metadata_requirements(
      catmgr.category_value("pcmaudio/waveform").value(),
      R"({"endianness": ["set", ["big", "little"]], "bytes_per_sample": ["range", 1, 4]})");

  {
    auto job = catmgr.job(wav);
    job.set_total_size(mm.size());

    EXPECT_TRUE(logger.empty());

    job.categorize_random_access(mm.span());
    auto frag = job.result();

    EXPECT_TRUE(logger.empty());

    EXPECT_EQ(2, frag.size());

    auto const& first = frag.span()[0];
    auto const& second = frag.span()[1];
    EXPECT_EQ("pcmaudio/metadata",
              catmgr.category_name(first.category().value()));
    EXPECT_EQ(44, first.size());
    EXPECT_EQ("pcmaudio/waveform",
              catmgr.category_name(second.category().value()));
    EXPECT_EQ(14, second.size());
    EXPECT_EQ(mm.size(), first.size() + second.size());
  }
}

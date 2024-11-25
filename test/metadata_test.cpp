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

#include <array>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/debug_thrift_data_difference/debug.h>
#include <thrift/lib/cpp2/debug_thrift_data_difference/diff.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>

#include <fmt/format.h>

#include <dwarfs/config.h>

#include <dwarfs/block_compressor.h>
#include <dwarfs/mmif.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/metadata_options.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/filesystem_writer_options.h>
#include <dwarfs/writer/metadata_options.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/reader/internal/metadata_v2.h>
#include <dwarfs/writer/internal/metadata_builder.h>
#include <dwarfs/writer/internal/metadata_freezer.h>

// #include <dwarfs/gen-cpp2/metadata_types.h>
#include <dwarfs/gen-cpp2/metadata_types_custom_protocol.h>

#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace {

std::string make_fragmented_file(size_t fragment_size, size_t fragment_count) {
  std::mt19937_64 rng{0};
  auto const fragment = test::create_random_string(fragment_size, rng);

  std::string file;

  for (size_t i = 0; i < fragment_count; ++i) {
    file.append(fragment);
    file.append(test::create_random_string(4, rng));
  }

  return file;
}

auto rebuild_metadata(logger& lgr, thrift::metadata::metadata const& md,
                      writer::metadata_options const& options) {
  using namespace writer::internal;
  return metadata_freezer(lgr).freeze(
      metadata_builder(lgr, md, options).build());
}

template <typename T>
std::string thrift_diff(T const& t1, T const& t2) {
  using namespace ::facebook::thrift;
  std::ostringstream oss;
  debug_thrift_data_difference(t1, t2, make_diff_output_callback(oss));
  return oss.str();
}

} // namespace

class metadata_test : public ::testing::Test {
 protected:
  void SetUp() override {
    os = test::os_access_mock::create_test_instance();
    os->add("lib", {333, posix_file_type::directory | 0755, 1, 1000, 100, 0, 0,
                    100, 200, 300});
    auto libc = make_fragmented_file(1024, 130);
    os->add("lib/libc.so",
            {334, posix_file_type::regular | 0755, 1, 1000, 100,
             static_cast<file_stat::off_type>(libc.size()), 0, 100, 200, 300},
            libc);

    writer::writer_progress prog;

    writer::segmenter_factory::config sf_cfg;
    sf_cfg.blockhash_window_size.set_default(9);
    sf_cfg.window_increment_shift.set_default(1);
    sf_cfg.max_active_blocks.set_default(1);
    sf_cfg.bloom_filter_size.set_default(4);
    sf_cfg.block_size_bits = 12;
    writer::segmenter_factory sf(lgr, prog, sf_cfg);

    writer::entry_factory ef;

    thread_pool pool(lgr, *os, "worker", 4);

    writer::scanner_options options;
    options.metadata.no_create_timestamp = true;
    writer::scanner s(lgr, pool, sf, ef, *os, options);

    block_compressor bc("null");
    std::ostringstream oss;

    writer::filesystem_writer fsw(oss, lgr, pool, prog, {});
    fsw.add_default_compressor(bc);

    s.scan(fsw, std::filesystem::path("/"), prog);

    mm = std::make_shared<test::mmap_mock>(oss.str());
  }

  void TearDown() override {}

  test::test_logger lgr;
  std::shared_ptr<test::os_access_mock> os;
  std::shared_ptr<mmif> mm;
};

TEST_F(metadata_test, basic) {
  reader::filesystem_v2 fs(lgr, *os, mm);

  auto thawed1 = *fs.thawed_metadata();
  auto unpacked1 = *fs.unpacked_metadata();

  // std::cout << ::apache::thrift::debugString(thawed1) << std::endl;
  // std::cout << ::apache::thrift::debugString(unpacked1) << std::endl;

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked1,
        {.plain_names_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema.span(), data.span(), {});
    using utils = reader::internal::metadata_v2_utils;

    auto thawed2 = *utils(mv2).thaw();
    auto unpacked2 = *utils(mv2).unpack();

    // std::cout << ::apache::thrift::debugString(unpacked2) << std::endl;

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    // std::cout << thrift_diff(thawed1, thawed2);
    // std::cout << thrift_diff(unpacked1, unpacked2);
  }
}

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
#include <dwarfs/file_view.h>
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
                      thrift::metadata::fs_options const* fs_options,
                      filesystem_version const& fs_version,
                      writer::metadata_options const& options) {
  using namespace writer::internal;
  return metadata_freezer(lgr).freeze(
      metadata_builder(lgr, md, fs_options, fs_version, options).build());
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

    mm = test::make_mock_file_view(oss.str());
  }

  void TearDown() override {}

  test::test_logger lgr;
  std::shared_ptr<test::os_access_mock> os;
  file_view mm;
};

TEST_F(metadata_test, basic) {
  reader::filesystem_v2 fs(lgr, *os, mm);

  auto thawed1 = *fs.thawed_metadata();
  auto unpacked1 = *fs.unpacked_metadata();

  // std::cout << ::apache::thrift::debugString(thawed1) << std::endl;
  // std::cout << ::apache::thrift::debugString(unpacked1) << std::endl;

  {
    auto fsopts = fs.thawed_fs_options();
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked1, fsopts.get(), fs.version(),
        {.plain_names_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema.span(), data.span(), {});
    using utils = reader::internal::metadata_v2_utils;

    auto thawed2 = *utils(mv2).thaw();
    auto unpacked2 = *utils(mv2).unpack();

    // std::cout << ::apache::thrift::debugString(unpacked2) << std::endl;

    auto history = unpacked2.metadata_version_history();

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 1);
    auto hent = history->at(0);
    EXPECT_EQ(hent.major().value(), fs.version().major);
    EXPECT_EQ(hent.minor().value(), fs.version().minor);
    ASSERT_TRUE(hent.dwarfs_version().has_value());
    ASSERT_TRUE(unpacked1.dwarfs_version().has_value());
    EXPECT_EQ(hent.dwarfs_version().value(),
              unpacked1.dwarfs_version().value());
    EXPECT_EQ(hent.block_size().value(), unpacked1.block_size().value());
    ASSERT_TRUE(hent.options().has_value());
    ASSERT_TRUE(unpacked1.options().has_value());
    EXPECT_EQ(hent.options().value(), unpacked1.options().value())
        << thrift_diff(hent.options().value(), unpacked1.options().value());

    unpacked2.metadata_version_history().reset();

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    // std::cout << thrift_diff(thawed1, thawed2);
    // std::cout << thrift_diff(unpacked1, unpacked2);
  }
}

TEST(metadata_options, output_stream) {
  using namespace dwarfs::writer;

  metadata_options opts;
  opts.uid = 1000;
  opts.gid = 1000;
  opts.timestamp = 1234567890;
  opts.keep_all_times = true;
  opts.time_resolution = std::chrono::seconds(1);
  opts.pack_chunk_table = true;
  opts.pack_directories = true;
  opts.pack_shared_files_table = true;
  opts.plain_names_table = true;
  opts.pack_names = true;
  opts.pack_names_index = true;
  opts.plain_symlinks_table = true;
  opts.pack_symlinks = true;
  opts.pack_symlinks_index = true;
  opts.force_pack_string_tables = true;
  opts.no_create_timestamp = true;
  opts.inode_size_cache_min_chunk_count = 1000;

  std::ostringstream oss;
  oss << opts;

  EXPECT_EQ(
      oss.str(),
      "{uid: 1000, gid: 1000, timestamp: 1234567890, keep_all_times, "
      "time_resolution: 1s, pack_chunk_table, pack_directories, "
      "pack_shared_files_table, plain_names_table, pack_names, "
      "pack_names_index, plain_symlinks_table, pack_symlinks, "
      "pack_symlinks_index, force_pack_string_tables, no_create_timestamp, "
      "inode_size_cache_min_chunk_count: 1000}");
}

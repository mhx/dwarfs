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
#include <filesystem>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

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
#include <dwarfs/writer/categorizer.h>
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

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";

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

std::string make_incompressible_file(size_t size) {
  std::mt19937_64 rng{42};
  return test::create_random_string(size, rng);
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
    auto incomp = make_incompressible_file(20000);
    os->add("incompressible",
            {335, posix_file_type::regular | 0755, 1, 1000, 100,
             static_cast<file_stat::off_type>(incomp.size()), 0, 100, 200, 300},
            incomp);
    os->add_local_files(audio_data_dir);

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
    options.inode.categorizer_mgr = create_catmgr({});
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

  std::shared_ptr<writer::categorizer_manager>
  create_catmgr(std::vector<char const*> args) {
    auto& catreg = writer::categorizer_registry::instance();

    po::options_description opts;
    catreg.add_options(opts);

    args.insert(args.begin(), "program");

    po::variables_map vm;
    auto parsed = po::parse_command_line(args.size(), args.data(), opts);

    po::store(parsed, vm);
    po::notify(vm);

    auto catmgr = std::make_shared<writer::categorizer_manager>(lgr);

    catmgr->add(catreg.create(lgr, "incompressible", vm));
    catmgr->add(catreg.create(lgr, "pcmaudio", vm));

    return catmgr;
  }

  test::test_logger lgr;
  std::shared_ptr<test::os_access_mock> os;
  std::shared_ptr<mmif> mm;
};

TEST_F(metadata_test, non_destructive) {
  reader::filesystem_v2 fs(lgr, *os, mm);

  auto thawed1 = *fs.thawed_metadata();
  auto unpacked1 = *fs.unpacked_metadata();
  auto unpacked_orig = *fs.unpacked_metadata();

  // std::cout << ::apache::thrift::debugString(thawed1) << std::endl;
  // std::cout << ::apache::thrift::debugString(unpacked1) << std::endl;

  unpacked1.rebuild_dwarfs_versions().ensure().push_back(
      unpacked1.dwarfs_version().value());

  {
    ASSERT_TRUE(thawed1.options().has_value());
    auto& opts = thawed1.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed1.names().value().size());
    EXPECT_EQ(0, thawed1.symlinks().value().size());
    EXPECT_TRUE(thawed1.compact_names().has_value());
    EXPECT_TRUE(thawed1.compact_symlinks().has_value());
    EXPECT_FALSE(thawed1.create_timestamp().has_value());
    ASSERT_TRUE(thawed1.category_names().has_value());
    EXPECT_EQ(4, thawed1.category_names().value().size());
    ASSERT_TRUE(thawed1.block_categories().has_value());
    EXPECT_FALSE(thawed1.block_categories().value().empty());
    ASSERT_TRUE(thawed1.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed1.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed1.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed1.category_metadata_json().value().size());
    ASSERT_TRUE(thawed1.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed1.block_category_metadata().value().size());
    EXPECT_TRUE(thawed1.dwarfs_version().has_value());
    EXPECT_FALSE(thawed1.rebuild_dwarfs_versions().has_value());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked_orig,
        {.plain_names_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    // std::cout << ::apache::thrift::debugString(unpacked2) << std::endl;

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    // std::cout << thrift_diff(thawed1, thawed2);
    // std::cout << thrift_diff(unpacked1, unpacked2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_NE(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_FALSE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked_orig,
        {.plain_symlinks_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_NE(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_FALSE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked_orig,
        {.pack_chunk_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_TRUE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked_orig,
        {.pack_directories = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_TRUE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked_orig,
        {.pack_shared_files_table = true, .no_create_timestamp = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    EXPECT_EQ(unpacked1, unpacked2) << thrift_diff(unpacked1, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_TRUE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }
}

TEST_F(metadata_test, remove_category_info) {
  reader::filesystem_v2 fs(lgr, *os, mm);

  auto thawed1 = *fs.thawed_metadata();
  auto unpacked1 = *fs.unpacked_metadata();

  // std::cout << ::apache::thrift::debugString(thawed1) << std::endl;
  // std::cout << ::apache::thrift::debugString(unpacked1) << std::endl;

  {
    ASSERT_TRUE(thawed1.options().has_value());
    auto& opts = thawed1.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed1.names().value().size());
    EXPECT_EQ(0, thawed1.symlinks().value().size());
    EXPECT_TRUE(thawed1.compact_names().has_value());
    EXPECT_TRUE(thawed1.compact_symlinks().has_value());
    EXPECT_FALSE(thawed1.create_timestamp().has_value());
    ASSERT_TRUE(thawed1.category_names().has_value());
    EXPECT_EQ(4, thawed1.category_names().value().size());
    ASSERT_TRUE(thawed1.block_categories().has_value());
    EXPECT_FALSE(thawed1.block_categories().value().empty());
    ASSERT_TRUE(thawed1.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed1.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed1.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed1.category_metadata_json().value().size());
    ASSERT_TRUE(thawed1.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed1.block_category_metadata().value().size());
    EXPECT_TRUE(thawed1.dwarfs_version().has_value());
    EXPECT_FALSE(thawed1.rebuild_dwarfs_versions().has_value());
  }

  {
    auto [schema, data] = rebuild_metadata(
        lgr, unpacked1,
        {.no_create_timestamp = true, .no_category_metadata = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    auto expected_unpacked = *fs.unpacked_metadata();
    expected_unpacked.category_metadata_json().reset();
    expected_unpacked.block_category_metadata().reset();
    expected_unpacked.rebuild_dwarfs_versions().ensure().push_back(
        expected_unpacked.dwarfs_version().value());

    EXPECT_EQ(expected_unpacked, unpacked2)
        << thrift_diff(expected_unpacked, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    EXPECT_FALSE(thawed2.category_metadata_json().has_value());
    EXPECT_FALSE(thawed2.block_category_metadata().has_value());
  }

  {
    auto [schema, data] = rebuild_metadata(lgr, unpacked1,
                                           {.no_create_timestamp = true,
                                            .no_category_names = true,
                                            .no_category_metadata = true});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    auto expected_unpacked = *fs.unpacked_metadata();
    expected_unpacked.category_names().reset();
    expected_unpacked.block_categories().reset();
    expected_unpacked.category_metadata_json().reset();
    expected_unpacked.block_category_metadata().reset();
    expected_unpacked.rebuild_dwarfs_versions().ensure().push_back(
        expected_unpacked.dwarfs_version().value());

    EXPECT_EQ(expected_unpacked, unpacked2)
        << thrift_diff(expected_unpacked, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    EXPECT_FALSE(thawed2.category_names().has_value());
    EXPECT_FALSE(thawed2.block_categories().has_value());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed2.reg_file_size_cache().value().lookup()->size());
    EXPECT_FALSE(thawed2.category_metadata_json().has_value());
    EXPECT_FALSE(thawed2.block_category_metadata().has_value());
  }
}

TEST_F(metadata_test, change_inode_size_cache) {
  reader::filesystem_v2 fs(lgr, *os, mm);

  auto thawed1 = *fs.thawed_metadata();
  auto unpacked1 = *fs.unpacked_metadata();

  // std::cout << ::apache::thrift::debugString(thawed1) << std::endl;
  // std::cout << ::apache::thrift::debugString(unpacked1) << std::endl;

  {
    ASSERT_TRUE(thawed1.options().has_value());
    auto& opts = thawed1.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed1.names().value().size());
    EXPECT_EQ(0, thawed1.symlinks().value().size());
    EXPECT_TRUE(thawed1.compact_names().has_value());
    EXPECT_TRUE(thawed1.compact_symlinks().has_value());
    EXPECT_FALSE(thawed1.create_timestamp().has_value());
    ASSERT_TRUE(thawed1.category_names().has_value());
    EXPECT_EQ(4, thawed1.category_names().value().size());
    ASSERT_TRUE(thawed1.block_categories().has_value());
    EXPECT_FALSE(thawed1.block_categories().value().empty());
    ASSERT_TRUE(thawed1.reg_file_size_cache().has_value());
    EXPECT_EQ(2, thawed1.reg_file_size_cache().value().lookup()->size());
    ASSERT_TRUE(thawed1.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed1.category_metadata_json().value().size());
    ASSERT_TRUE(thawed1.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed1.block_category_metadata().value().size());
    EXPECT_TRUE(thawed1.dwarfs_version().has_value());
    EXPECT_FALSE(thawed1.rebuild_dwarfs_versions().has_value());
  }

  {
    auto [schema, data] =
        rebuild_metadata(lgr, unpacked1,
                         {.no_create_timestamp = true,
                          .inode_size_cache_min_chunk_count = 4096});
    reader::internal::metadata_v2 mv2(lgr, schema, data, {});

    auto thawed2 = *mv2.thaw();
    auto unpacked2 = *mv2.unpack();

    auto expected_unpacked = *fs.unpacked_metadata();
    expected_unpacked.reg_file_size_cache()->lookup()->clear();
    expected_unpacked.reg_file_size_cache()->min_chunk_count() = 4096;
    expected_unpacked.rebuild_dwarfs_versions().ensure().push_back(
        expected_unpacked.dwarfs_version().value());

    EXPECT_EQ(expected_unpacked, unpacked2)
        << thrift_diff(expected_unpacked, unpacked2);
    EXPECT_NE(thawed1, thawed2) << thrift_diff(thawed1, thawed2);

    ASSERT_TRUE(thawed2.options().has_value());
    auto& opts = thawed2.options().value();
    EXPECT_TRUE(opts.mtime_only().value());
    EXPECT_FALSE(opts.packed_chunk_table().value());
    EXPECT_FALSE(opts.packed_directories().value());
    EXPECT_FALSE(opts.packed_shared_files_table().value());
    EXPECT_EQ(0, thawed2.names().value().size());
    EXPECT_EQ(0, thawed2.symlinks().value().size());
    EXPECT_TRUE(thawed2.compact_names().has_value());
    EXPECT_TRUE(thawed2.compact_symlinks().has_value());
    EXPECT_FALSE(thawed2.create_timestamp().has_value());
    ASSERT_TRUE(thawed2.category_names().has_value());
    EXPECT_EQ(4, thawed2.category_names().value().size());
    ASSERT_TRUE(thawed2.block_categories().has_value());
    EXPECT_FALSE(thawed2.block_categories().value().empty());
    ASSERT_TRUE(thawed2.reg_file_size_cache().has_value());
    EXPECT_EQ(4096, thawed2.reg_file_size_cache().value().min_chunk_count());
    ASSERT_TRUE(thawed2.category_metadata_json().has_value());
    EXPECT_EQ(8, thawed2.category_metadata_json().value().size());
    ASSERT_TRUE(thawed2.block_category_metadata().has_value());
    EXPECT_EQ(8, thawed2.block_category_metadata().value().size());
  }
}

TEST_F(metadata_test, set_owner) {
  // TODO
  // This is easy, we only need to set a single uid and update
  // all entries accordingly.
}

TEST_F(metadata_test, set_group) {
  // TODO
  // This is easy, we only need to set a single gid and update
  // all entries accordingly.
}

TEST_F(metadata_test, chmod) {
  // TODO
  // This is a little more tricky. We need to keep track of which
  // old mode gets mapped to which new mode and then update all
  // entries after we've modified the mode list.
}

TEST_F(metadata_test, no_create_timestamp) {
  // TODO
  // Ensure that this can only be removed, but not set after the fact
}

TEST_F(metadata_test, set_time) {
  // TODO
  // Easy
}

TEST_F(metadata_test, keep_all_times) {
  // TODO
  // Ensure that this is one-way: if all times are kept, ctime/atime
  // can be removed. But they can never be recovered.
}

TEST_F(metadata_test, time_resolution) {
  // TODO
  // Ensure this is one-way: we can only decrease the resolution,
  // but never increase it.
}

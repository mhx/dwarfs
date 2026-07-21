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

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/metadata_defs.h>

#include <dwarfs/internal/features.h>
#include <dwarfs/reader/internal/metadata_types.h>

#include <dwarfs/gen-cpp-lite/metadata_layouts.h>

#include "test_logger.h"

using namespace dwarfs::reader::internal;
using namespace dwarfs::thrift::metadata;
using namespace apache::thrift::frozen;
using namespace dwarfs::test;

namespace {

// A minimal but fully consistent metadata object, matching the end state
// of the `check_metadata` test below. Used as a base for tests that only
// want to violate a single invariant.
metadata make_valid_metadata() {
  metadata raw;
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.chunk_table()->push_back(2);
  raw.chunks()->emplace_back().size() = 1;
  raw.chunks()->emplace_back().size() = 1;
  raw.inodes()->resize(2);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);
  raw.block_size() = 1024;
  raw.options().emplace();

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all dir_entries

  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 1;

  return raw;
}

// Consistent metadata with a second (empty) directory, so that
// `self_entry` is actually populated and `dir_has_self_entry()` is true.
//
//   dir_entries[0] -> inode 0 (root dir)
//   dir_entries[1] -> inode 1 (sub dir, empty)
//   dir_entries[2] -> inode 2 (regular file)
metadata make_valid_metadata_with_subdir() {
  metadata raw;
  raw.directories()->resize(3);
  raw.chunk_table()->push_back(1);
  raw.chunk_table()->push_back(2);
  raw.chunks()->emplace_back().size() = 1;
  raw.chunks()->emplace_back().size() = 1;
  raw.inodes()->resize(3);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(3);
  raw.block_size() = 1024;
  raw.options().emplace();

  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 0;
  raw.inodes()->at(2).mode_index() = 1;

  auto& des = *raw.dir_entries();
  des[0].inode_num() = 0;
  des[1].inode_num() = 1;
  des[2].inode_num() = 2;

  auto& ds = *raw.directories();
  // root directory
  ds[0].first_entry() = 1;
  ds[0].parent_entry() = 0;
  ds[0].self_entry() = 0;
  // sub directory (empty)
  ds[1].first_entry() = 3;
  ds[1].parent_entry() = 0;
  ds[1].self_entry() = 1;
  // sentinel
  ds[2].first_entry() = 3;
  ds[2].parent_entry() = 0;
  ds[2].self_entry() = 0;

  return raw;
}

} // namespace

class global_metadata_test : public ::testing::Test {
 public:
  void
  check(metadata const& raw,
        std::span<std::optional<std::size_t> const> uncompressed_block_size) {
    auto meta = freeze(raw);
    global_metadata::check_consistency(lgr, meta, uncompressed_block_size);
  }

  void check(metadata const& raw) {
    std::vector<std::optional<std::size_t>> uncompressed_block_size;
    uncompressed_block_size.push_back(1);
    check(raw, uncompressed_block_size);
  }

  static auto throws_error(std::string_view msg) {
    return testing::ThrowsMessage<dwarfs::error>(testing::HasSubstr(msg));
  };

  test_logger lgr;
};

TEST_F(global_metadata_test, check_empty_tables) {
  metadata raw;
  EXPECT_THAT([&] { check(raw); }, throws_error("empty inodes table"));

  raw.inodes()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("empty directories table"));

  raw.directories()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("empty chunk_table table"));

  raw.chunk_table()->resize(1);
  EXPECT_THAT([&] { check(raw); },
              throws_error("empty entry_table_v2_2 table"));

  raw.dir_entries().emplace();
  EXPECT_THAT([&] { check(raw); }, throws_error("empty dir_entries table"));

  raw.dir_entries()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("empty modes table"));
}

// On aarch64, Clang will wrongly optimize away some of the assignments
// in this test, leading to false positives. I'm pretty sure this is a bug
// in Clang, need to reproduce it and file a bug report.
#if defined(__clang__) && defined(__aarch64__)
#pragma clang optimize off
#endif

TEST_F(global_metadata_test, check_index_range) {
  metadata raw;
  raw.directories()->resize(1);
  raw.chunk_table()->resize(1);
  raw.modes()->resize(2);
  raw.uids()->resize(2);
  raw.gids()->resize(2);
  raw.names()->resize(2);
  raw.inodes()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(1);

  EXPECT_THAT([&] { check(raw); }, throws_error("invalid number of modes"));
  raw.modes()->resize(1);

  EXPECT_THAT([&] { check(raw); }, throws_error("invalid number of uids"));
  raw.uids()->resize(1);

  EXPECT_THAT([&] { check(raw); }, throws_error("invalid number of gids"));
  raw.gids()->resize(1);

  EXPECT_THAT([&] { check(raw); }, throws_error("invalid number of names"));
  raw.names()->resize(1);

  raw.inodes()->resize(2);
  EXPECT_THAT([&] { check(raw); }, throws_error("invalid number of inodes"));

  raw.dir_entries().reset();
  raw.inodes()->clear();
  auto&& ino = raw.inodes()->emplace_back();
  raw.entry_table_v2_2()->push_back(1);

  ino.mode_index() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("mode_index out of range"));
  ino.mode_index() = 0;

  ino.owner_index() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("owner_index out of range"));
  ino.owner_index() = 0;

  ino.group_index() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("group_index out of range"));
  ino.group_index() = 0;

  ino.name_index_v2_2() = 1;
  EXPECT_THAT([&] { check(raw); },
              throws_error("name_index_v2_2 out of range"));
  ino.name_index_v2_2() = 0;

  EXPECT_THAT([&] { check(raw); },
              throws_error("entry_table_v2_2 value out of range"));

  // make this metadata v2.3+
  raw.entry_table_v2_2()->clear();
  raw.dir_entries().emplace();

  EXPECT_THAT([&] { check(raw); }, throws_error("empty dir_entries table"));

  auto&& de = raw.dir_entries()->emplace_back();

  raw.compact_names().emplace();
  EXPECT_THAT([&] { check(raw); }, throws_error("empty compact_names index"));
  raw.compact_names().reset();

  de.name_index() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("name_index out of range"));
  de.name_index() = 0;

  de.inode_num() = 1;
  EXPECT_THAT([&] { check(raw); },
              throws_error("root inode number out of range"));
  de.inode_num() = 0;

  auto&& de2 = raw.dir_entries()->emplace_back();
  de2.name_index() = 0;
  de2.inode_num() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("inode_num out of range"));
}

#if defined(__clang__) && defined(__aarch64__)
#pragma clang optimize on
#endif

TEST_F(global_metadata_test, check_packed_tables) {
  metadata raw;
  raw.inodes()->resize(2);
  raw.directories()->resize(4);
  raw.chunk_table()->resize(3);
  raw.chunks()->resize(1);
  raw.modes()->resize(1);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  auto& des = raw.dir_entries().emplace();
  des.resize(2);

  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid number of directories"));

  auto& ds = *raw.directories();
  ds.resize(2);

  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid number of chunk_table entries"));

  raw.chunk_table()->resize(1);

  ds[0].first_entry() = 1;
  ds[1].first_entry() = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("first_entry values not sorted"));

  ds[0].first_entry() = 1;
  ds[1].first_entry() = 3; // sentinel value may be equal to entry count
  EXPECT_THAT([&] { check(raw); },
              throws_error("[1] first_entry out of range"));

  ds[1].first_entry() = 2;
  ds[1].parent_entry() = 2;
  EXPECT_THAT([&] { check(raw); },
              throws_error("[1] parent_entry out of range"));
  ds[1].parent_entry() = 0;

  auto& ct = *raw.chunk_table();
  ct.resize(2);
  ct[0] = 1;
  ct[1] = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("chunk_table values not sorted"));
  ct[0] = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("chunk_table end value mismatch"));

  auto& opts = raw.options().emplace();
  opts.packed_directories() = true;
  ds[1].parent_entry() = 1;
  EXPECT_THAT([&] { check(raw); },
              throws_error("parent_entry set in packed directory"));
  ds[1].parent_entry() = 0;
  ds[1].first_entry() = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("first_entry inconsistency in packed directories"));
  ds[1].first_entry() = 1;

  opts.packed_chunk_table() = true;
  EXPECT_THAT([&] { check(raw); },
              throws_error("packed chunk_table inconsistency"));
}

TEST_F(global_metadata_test, check_string_tables) {
  metadata raw;
  raw.inodes()->resize(2);
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.chunks()->resize(1);
  raw.modes()->resize(1);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all dir_entries

  raw.names()->resize(2);
  EXPECT_THAT([&] { check(raw); }, throws_error("unexpected number of names"));
  raw.names()->clear();

  raw.names()->push_back(std::string(513, 'a'));
  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid item length in names"));

  auto& cn = raw.compact_names().emplace();
  cn.index()->resize(3);
  EXPECT_THAT([&] { check(raw); },
              throws_error("both compact and plain names tables populated"));
  raw.names()->clear();

  EXPECT_THAT([&] { check(raw); },
              throws_error("unexpected number of compact names"));

  raw.dir_entries()->at(0).name_index() = 1;

  cn.index()[0] = 1;
  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid first compact names index"));
  cn.index()[0] = 0;

  cn.index()[1] = 2;
  EXPECT_THAT([&] { check(raw); },
              throws_error("compact names index not sorted"));

  cn.index()[0] = 0;
  cn.index()[2] = 10;
  EXPECT_THAT([&] { check(raw); },
              throws_error("data size mismatch for compact names"));

  cn.index()[2] = 515;
  cn.buffer()->resize(515);
  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid item length in compact names"));

  cn.packed_index() = true;
  cn.index()->resize(2);
  cn.index()[0] = 1;
  cn.index()[1] = 513;
  EXPECT_THAT([&] { check(raw); },
              throws_error("data size mismatch for compact names"));

  cn.buffer()->resize(514);
  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid item length in compact names"));
  cn.index()[1] = 512;
  cn.buffer()->resize(513);

  raw.symlinks()->resize(1);
  raw.compact_symlinks().emplace();
  EXPECT_THAT([&] { check(raw); },
              throws_error("both compact and plain symlinks tables populated"));
}

TEST_F(global_metadata_test, check_chunks) {
  metadata raw;
  raw.inodes()->resize(2);
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.modes()->resize(1);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);
  auto&& c = raw.chunks()->emplace_back();

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all dir_entries

  raw.block_size() = 3;
  EXPECT_THAT([&] { check(raw); }, throws_error("invalid block size"));
  raw.block_size() = 65536;

  c.offset() = 65536;
  EXPECT_THAT([&] { check(raw); }, throws_error("chunk offset out of range"));

  c.offset() = 0;
  c.size() = 65537;
  EXPECT_THAT([&] { check(raw); }, throws_error("chunk size out of range"));

  c.offset() = 32768;
  c.size() = 32769;
  EXPECT_THAT([&] { check(raw); }, throws_error("chunk end outside of block"));
  c.size() = 32768;
}

TEST_F(global_metadata_test, check_partitioning) {
  metadata raw;
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.chunks()->emplace_back().size() = 1;
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.block_size() = 1024;

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all dir_entries

  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.inodes()->resize(2);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 1;
  raw.entry_table_v2_2()->push_back(0);
  raw.entry_table_v2_2()->push_back(1);

  EXPECT_THAT([&] { check(raw); },
              throws_error("entry_table_v2_2 is not partitioned"));

  raw.entry_table_v2_2()->clear();
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);

  EXPECT_THAT([&] { check(raw); },
              throws_error("inode table is not partitioned"));
}

TEST_F(global_metadata_test, check_metadata) {
  metadata raw;
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.chunks()->emplace_back().size() = 1;
  raw.inodes()->resize(2);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);
  raw.block_size() = 1024;

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all dir_entries

  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.inodes()->resize(2);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 0;

  EXPECT_THAT([&] { check(raw); },
              throws_error("dir_entries present but options missing"));

  raw.shared_files_table().emplace();

  EXPECT_THAT([&] { check(raw); },
              throws_error("shared_files_table present but options missing"));

  raw.options().emplace();

  raw.shared_files_table()->push_back(1);
  raw.shared_files_table()->push_back(0);
  EXPECT_THAT([&] { check(raw); },
              throws_error("unpacked shared_files_table is not sorted"));
  raw.shared_files_table().reset();

  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of directories"));
  raw.inodes()->at(1).mode_index() = 1;

  raw.symlink_table()->resize(1);
  raw.symlinks()->resize(1);
  EXPECT_THAT([&] { check(raw); },
              throws_error("empty item in symlink strings is not allowed"));
  raw.symlinks()->at(0) = "a";
  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of links"));
  raw.symlink_table()->clear();
  raw.symlinks()->clear();

  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of files"));
  raw.chunk_table()->push_back(2);
  raw.chunks()->emplace_back().size() = 1;

  raw.devices().emplace();
  raw.devices()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of devices"));
  raw.devices().reset();

  EXPECT_NO_THROW(check(raw));
}

TEST_F(global_metadata_test, check_valid_metadata_baselines) {
  EXPECT_NO_THROW(check(make_valid_metadata()));
  EXPECT_NO_THROW(check(make_valid_metadata_with_subdir()));
}

TEST_F(global_metadata_test, check_chunks_sparse_features) {
  using dwarfs::internal::feature;
  using dwarfs::internal::feature_set;

  {
    auto raw = make_valid_metadata();
    feature_set fs;
    fs.add(feature::sparsefiles_new_lhm);
    raw.features() = fs.get();
    EXPECT_THAT(
        [&] { check(raw); },
        throws_error("sparsefiles_new_lhm feature implies sparsefiles"));
  }

  {
    auto raw = make_valid_metadata();
    raw.hole_block_index() = 1;
    EXPECT_THAT(
        [&] { check(raw); },
        throws_error("hole_block_index set but sparsefiles feature missing"));
  }

  {
    auto raw = make_valid_metadata();
    feature_set fs;
    fs.add(feature::sparsefiles);
    raw.features() = fs.get();
    EXPECT_THAT(
        [&] { check(raw); },
        throws_error("sparsefiles feature set but hole_block_index missing"));
  }
}

namespace {

// Sparse-capable metadata: block 0 is a real block, block 1 is the hole
// block. The second chunk (the only one referenced by the single regular
// file) is turned into a hole chunk by the individual tests.
metadata make_sparse_metadata(bool new_hole_marker) {
  using dwarfs::internal::feature;
  using dwarfs::internal::feature_set;

  auto raw = make_valid_metadata();

  feature_set fs;
  fs.add(feature::sparsefiles);
  if (new_hole_marker) {
    fs.add(feature::sparsefiles_new_lhm);
  }
  raw.features() = fs.get();
  raw.hole_block_index() = 1;

  return raw;
}

} // namespace

TEST_F(global_metadata_test, check_chunks_hole_remainder_out_of_range) {
  auto raw = make_sparse_metadata(false);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1024; // == block_size, and not the legacy marker
  c.size() = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("hole chunk size remainder exceeds block size"));
}

TEST_F(global_metadata_test, check_chunks_large_hole_list_missing) {
  auto raw = make_sparse_metadata(true);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1023; // block_size - 1 == large hole marker
  c.size() = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("large hole chunk but no large_hole_size set"));
}

TEST_F(global_metadata_test, check_chunks_large_hole_index_out_of_range) {
  auto raw = make_sparse_metadata(true);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1023;
  c.size() = 1; // only one entry in large_hole_size
  raw.large_hole_size().emplace().push_back(4096);
  EXPECT_THAT([&] { check(raw); },
              throws_error("large hole chunk size index out of range"));
}

TEST_F(global_metadata_test, check_chunks_large_hole_size_out_of_range) {
  auto raw = make_sparse_metadata(true);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1023;
  c.size() = 0;
  raw.large_hole_size().emplace().push_back(dwarfs::kChunkBitsSizeMask + 1);
  EXPECT_THAT([&] { check(raw); },
              throws_error("large hole chunk size out of range"));
}

TEST_F(global_metadata_test, check_chunks_zero_size_large_hole) {
  auto raw = make_sparse_metadata(true);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1023;
  c.size() = 0;
  raw.large_hole_size().emplace().push_back(0);
  EXPECT_THAT([&] { check(raw); }, throws_error("chunk size is zero"));
}

TEST_F(global_metadata_test, check_chunks_valid_large_hole) {
  auto raw = make_sparse_metadata(true);
  auto&& c = raw.chunks()->at(1);
  c.block() = 1;
  c.offset() = 1023;
  c.size() = 0;
  raw.large_hole_size().emplace().push_back(1 << 20);
  EXPECT_NO_THROW(check(raw));
}

TEST_F(global_metadata_test, check_chunks_uncompressed_block_size) {
  auto raw = make_valid_metadata();
  raw.chunks()->at(1).size() = 2; // block 0 is only one byte

  EXPECT_THAT([&] { check(raw); },
              throws_error("chunk end outside of uncompressed block"));

  {
    // an unknown uncompressed block size (unsupported compression in this
    // build) relaxes the check
    std::vector<std::optional<std::size_t>> ubs;
    ubs.push_back(std::nullopt);
    EXPECT_NO_THROW(check(raw, ubs));
  }

  {
    std::vector<std::optional<std::size_t>> ubs;
    ubs.push_back(4096); // > block_size
    EXPECT_THAT([&] { check(raw, ubs); },
                throws_error("invalid uncompressed block size for block"));
  }
}

TEST_F(global_metadata_test, check_categories) {
  {
    auto raw = make_valid_metadata();
    raw.block_categories().emplace().push_back(0);
    EXPECT_THAT([&] { check(raw); },
                throws_error("categories and category_names must be both "
                             "present or both absent"));
  }

  {
    auto raw = make_valid_metadata();
    raw.category_names().emplace().push_back("pcmaudio");
    EXPECT_THAT([&] { check(raw); },
                throws_error("categories and category_names must be both "
                             "present or both absent"));
  }

  {
    auto raw = make_valid_metadata();
    raw.category_names().emplace().push_back("pcmaudio");
    raw.block_categories().emplace().push_back(1);
    EXPECT_THAT([&] { check(raw); },
                throws_error("category index out of range"));
  }

  {
    auto raw = make_valid_metadata();
    raw.category_names().emplace().push_back("pcmaudio");
    raw.block_categories().emplace().push_back(0);
    EXPECT_NO_THROW(check(raw));
  }

  {
    auto raw = make_valid_metadata();
    raw.category_metadata_json().emplace().push_back("{\"a\":1}");
    raw.block_category_metadata().emplace()[0] = 1;
    EXPECT_THAT([&] { check(raw); },
                throws_error("category metadata index out of range"));
  }

  {
    auto raw = make_valid_metadata();
    raw.category_metadata_json().emplace().push_back("{not json");
    raw.block_category_metadata().emplace()[0] = 0;
    EXPECT_THAT([&] { check(raw); },
                throws_error("invalid category metadata JSON"));
  }
}

TEST_F(global_metadata_test, check_options_subsecond_resolution) {
  {
    auto raw = make_valid_metadata();
    raw.options()->subsecond_resolution_nsec_multiplier() = 0;
    EXPECT_THAT(
        [&] { check(raw); },
        throws_error("subsecond_resolution_nsec_multiplier out of range"));
  }

  {
    auto raw = make_valid_metadata();
    raw.options()->subsecond_resolution_nsec_multiplier() = 1'000'000'000;
    EXPECT_THAT(
        [&] { check(raw); },
        throws_error("subsecond_resolution_nsec_multiplier out of range"));
  }

  {
    auto raw = make_valid_metadata();
    raw.options()->subsecond_resolution_nsec_multiplier() = 1'000'000;
    EXPECT_NO_THROW(check(raw));
  }
}

TEST_F(global_metadata_test, check_options_in_history) {
  auto raw = make_valid_metadata();
  auto& hist = raw.metadata_version_history().emplace().emplace_back();
  hist.options().emplace().time_resolution_sec() = 0;
  EXPECT_THAT([&] { check(raw); },
              throws_error("time_resolution_sec out of range"));
}

TEST_F(global_metadata_test, check_self_entry) {
  {
    auto raw = make_valid_metadata_with_subdir();
    raw.options()->packed_directories() = true;
    EXPECT_THAT([&] { check(raw); },
                throws_error("self_entry set in packed directory"));
  }

  {
    auto raw = make_valid_metadata_with_subdir();
    raw.directories()->at(1).self_entry() = 4; // > dir_entries.size()
    EXPECT_THAT([&] { check(raw); },
                throws_error("[1] self_entry out of range"));
  }

  {
    auto raw = make_valid_metadata_with_subdir();
    raw.directories()->at(0).self_entry() = 1;
    EXPECT_THAT([&] { check(raw); },
                throws_error("[0] self_entry 1 != 0 for root directory"));
  }

  {
    auto raw = make_valid_metadata_with_subdir();
    raw.directories()->at(1).self_entry() =
        raw.directories()->at(1).first_entry().value();
    EXPECT_THAT([&] { check(raw); },
                throws_error("[1] first_entry == self_entry"));
  }

  {
    auto raw = make_valid_metadata_with_subdir();
    // keep self_entry bits allocated via the sentinel (which only warns)
    raw.directories()->at(2).self_entry() = 1;
    raw.directories()->at(1).self_entry() = 0;
    raw.directories()->at(1).parent_entry() = 0;
    EXPECT_THAT([&] { check(raw); },
                throws_error("[1] self_entry == parent_entry"));
  }

  {
    // sentinel self_entry is only a warning
    auto raw = make_valid_metadata_with_subdir();
    raw.directories()->at(2).self_entry() = 2;
    EXPECT_NO_THROW(check(raw));
  }
}

TEST_F(global_metadata_test, check_root_parent_entry) {
  auto raw = make_valid_metadata_with_subdir();
  auto& ds = *raw.directories();
  ds[0].first_entry() = 2; // must differ from parent_entry
  ds[0].parent_entry() = 1;
  EXPECT_THAT([&] { check(raw); },
              throws_error("[0] parent_entry 1 != 0 for root directory"));
}

TEST_F(global_metadata_test, check_unpacked_shared_files_table) {
  auto raw = make_valid_metadata();
  // num_reg_unique is 1, so a shared file group index of 1 is one too many
  raw.shared_files_table().emplace().push_back(1);
  EXPECT_THAT([&] { check(raw); },
              throws_error("too many shared files in shared_files_table"));
}

TEST_F(global_metadata_test, check_inode_v2_2_out_of_range) {
  metadata raw;
  raw.directories()->resize(2);
  raw.chunk_table()->push_back(1);
  raw.chunk_table()->push_back(2);
  raw.chunks()->emplace_back().size() = 1;
  raw.chunks()->emplace_back().size() = 1;
  raw.inodes()->resize(2);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.block_size() = 1024;

  auto& ds = *raw.directories();
  ds[0].first_entry() = 1;
  ds[1].first_entry() = 2; // sentinel covers all entries

  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 1;
  raw.inodes()->at(0).inode_v2_2() = 0;
  raw.inodes()->at(1).inode_v2_2() = 1;

  raw.entry_table_v2_2()->push_back(0);
  raw.entry_table_v2_2()->push_back(1);

  EXPECT_NO_THROW(check(raw));

  raw.inodes()->at(1).inode_v2_2() = 5;
  EXPECT_THAT([&] { check(raw); }, throws_error("inode_v2_2 out of range"));
}

TEST_F(global_metadata_test, check_compact_names_invalid_dictionary) {
  auto raw = make_valid_metadata();
  raw.names()->clear();

  auto& cn = raw.compact_names().emplace();
  cn.index()->push_back(0);
  cn.index()->push_back(1);
  cn.buffer() = "a";
  cn.symtab() = std::string(16, '\0'); // not a valid fsst dictionary

  EXPECT_THAT([&] { check(raw); },
              throws_error("invalid dictionary for compact names"));
}

TEST_F(global_metadata_test, check_sentinel_first_entry) {
  auto raw = make_valid_metadata();
  raw.directories()->at(1).first_entry() = 1; // < dir_entries.size()
  EXPECT_THAT([&] { check(raw); },
              throws_error("sentinel first_entry mismatch"));
}

TEST_F(global_metadata_test, check_hole_block_index_aliases_real_block) {
  using dwarfs::internal::feature;
  using dwarfs::internal::feature_set;

  auto raw = make_valid_metadata();
  feature_set fs;
  fs.add(feature::sparsefiles);
  raw.features() = fs.get();
  raw.hole_block_index() = 0; // block 0 is a real block

  EXPECT_THAT([&] { check(raw); },
              throws_error("hole_block_index 0 references a real block"));
}

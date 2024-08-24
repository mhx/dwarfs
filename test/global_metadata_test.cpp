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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/reader/internal/metadata_types.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>

#include "test_logger.h"

using namespace dwarfs::reader::internal;
using namespace dwarfs::thrift::metadata;
using namespace apache::thrift::frozen;
using namespace dwarfs::test;

class global_metadata_test : public ::testing::Test {
 public:
  void check(metadata const& raw) {
    auto meta = freeze(raw);
    global_metadata::check_consistency(lgr, meta);
  }

  static auto throws_error(std::string_view msg) {
    return testing::ThrowsMessage<dwarfs::error>(testing::StartsWith(msg));
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
  auto& ino = raw.inodes()->emplace_back();
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
  raw.dir_entries().emplace();
  auto& de = raw.dir_entries()->emplace_back();

  raw.compact_names().emplace();
  EXPECT_THAT([&] { check(raw); }, throws_error("empty compact_names index"));
  raw.compact_names().reset();

  de.name_index() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("name_index out of range"));
  de.name_index() = 0;

  de.inode_num() = 1;
  EXPECT_THAT([&] { check(raw); }, throws_error("inode_num out of range"));
}

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

  ds[0].first_entry() = 0;
  ds[1].first_entry() = 3; // sentinel value may be equal to entry count
  EXPECT_THAT([&] { check(raw); }, throws_error("first_entry out of range"));

  ds[1].first_entry() = 2;
  ds[1].parent_entry() = 2;
  EXPECT_THAT([&] { check(raw); }, throws_error("parent_entry out of range"));
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
  ds[1].first_entry() = 2;

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
  auto& c = raw.chunks()->emplace_back();

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
  raw.chunks()->resize(1);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.block_size() = 1024;

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
  raw.chunks()->resize(1);
  raw.inodes()->resize(2);
  raw.uids()->resize(1);
  raw.gids()->resize(1);
  raw.names()->resize(1);
  raw.dir_entries().emplace();
  raw.dir_entries()->resize(2);
  raw.block_size() = 1024;

  raw.modes()->push_back(dwarfs::posix_file_type::directory);
  raw.modes()->push_back(dwarfs::posix_file_type::regular);
  raw.inodes()->resize(2);
  raw.inodes()->at(0).mode_index() = 0;
  raw.inodes()->at(1).mode_index() = 0;

  raw.shared_files_table().emplace();
  raw.shared_files_table()->push_back(1);
  raw.shared_files_table()->push_back(0);
  EXPECT_THAT([&] { check(raw); },
              throws_error("unpacked shared_files_table is not sorted"));
  raw.shared_files_table().reset();

  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of directories"));
  raw.inodes()->at(1).mode_index() = 1;

  raw.symlink_table()->resize(1);
  raw.symlinks()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of links"));
  raw.symlink_table()->clear();
  raw.symlinks()->clear();

  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of files"));
  raw.chunk_table()->push_back(2);
  raw.chunks()->resize(2);

  raw.devices().emplace();
  raw.devices()->resize(1);
  EXPECT_THAT([&] { check(raw); }, throws_error("wrong number of devices"));
  raw.devices().reset();

  EXPECT_NO_THROW(check(raw));
}

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

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/entry.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "loremipsum.h"
#include "mmap_mock.h"

namespace dwarfs {
namespace test {

class dir_reader_mock : public dir_reader {
 public:
  dir_reader_mock(std::vector<std::string>&& files)
      : m_files(files)
      , m_index(0) {}

  bool read(std::string& name) const override {
    if (m_index < m_files.size()) {
      name = m_files[m_index++];
      return true;
    }

    return false;
  }

 private:
  std::vector<std::string> m_files;
  mutable size_t m_index;
};

namespace {

struct simplestat {
  ::ino_t st_ino;
  ::mode_t st_mode;
  ::nlink_t st_nlink;
  ::uid_t st_uid;
  ::gid_t st_gid;
  ::off_t st_size;
  ::dev_t st_rdev;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
};

std::map<std::string, simplestat> statmap{
    {"", {1, S_IFDIR | 0777, 1, 1000, 100, 0, 0, 1, 2, 3}},
    {"/test.pl", {3, S_IFREG | 0644, 2, 1000, 100, 0, 0, 1001, 1002, 1003}},
    {"/somelink", {4, S_IFLNK | 0777, 1, 1000, 100, 16, 0, 2001, 2002, 2003}},
    {"/somedir", {5, S_IFDIR | 0777, 1, 1000, 100, 0, 0, 3001, 3002, 3003}},
    {"/foo.pl", {6, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 4001, 4002, 4003}},
    {"/bar.pl", {6, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 4001, 4002, 4003}},
    {"/baz.pl", {16, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 8001, 8002, 8003}},
    {"/ipsum.txt",
     {7, S_IFREG | 0644, 1, 1000, 100, 2000000, 0, 5001, 5002, 5003}},
    {"/somedir/ipsum.py",
     {9, S_IFREG | 0644, 1, 1000, 100, 10000, 0, 6001, 6002, 6003}},
    {"/somedir/bad",
     {10, S_IFLNK | 0777, 1, 1000, 100, 6, 0, 7001, 7002, 7003}},
    {"/somedir/pipe",
     {12, S_IFIFO | 0644, 1, 1000, 100, 0, 0, 8001, 8002, 8003}},
    {"/somedir/null", {13, S_IFCHR | 0666, 1, 0, 0, 0, 259, 9001, 9002, 9003}},
    {"/somedir/zero",
     {14, S_IFCHR | 0666, 1, 0, 0, 0, 261, 4000010001, 4000020002, 4000030003}},
};
} // namespace

class os_access_mock : public os_access {
 public:
  std::shared_ptr<dir_reader> opendir(const std::string& path) const override {
    if (path.empty()) {
      std::vector<std::string> files{
          ".",      "..",     "test.pl", "somelink",  "somedir",
          "foo.pl", "bar.pl", "baz.pl",  "ipsum.txt",
      };

      return std::make_shared<dir_reader_mock>(std::move(files));
    } else if (path == "/somedir") {
      std::vector<std::string> files{
          ".", "..", "ipsum.py", "bad", "pipe", "null", "zero",
      };

      return std::make_shared<dir_reader_mock>(std::move(files));
    }

    throw std::runtime_error("oops");
  }

  void lstat(const std::string& path, struct ::stat* st) const override {
    const simplestat& sst = statmap[path];
    std::memset(st, 0, sizeof(*st));
    st->st_ino = sst.st_ino;
    st->st_mode = sst.st_mode;
    st->st_nlink = sst.st_nlink;
    st->st_uid = sst.st_uid;
    st->st_gid = sst.st_gid;
    st->st_size = sst.st_size;
    st->st_atime = sst.atime;
    st->st_mtime = sst.mtime;
    st->st_ctime = sst.ctime;
    st->st_rdev = sst.st_rdev;
  }

  std::string readlink(const std::string& path, size_t size) const override {
    if (path == "/somelink" && size == 16) {
      return "somedir/ipsum.py";
    } else if (path == "/somedir/bad" && size == 6) {
      return "../foo";
    }

    throw std::runtime_error("oops");
  }

  std::shared_ptr<mmif>
  map_file(const std::string& path, size_t size) const override {
    const simplestat& sst = statmap[path];

    if (size == static_cast<size_t>(sst.st_size)) {
      return std::make_shared<mmap_mock>(loremipsum(size));
    }

    throw std::runtime_error("oops");
  }

  int access(const std::string&, int) const override { return 0; }
};

class script_mock : public script {
 public:
  bool has_configure() const override { return true; }
  bool has_filter() const override { return true; }
  bool has_transform() const override { return true; }
  bool has_order() const override { return true; }

  void configure(options_interface const& /*oi*/) override {}

  bool filter(entry_interface const& /*ei*/) override { return true; }

  void transform(entry_interface& /*ei*/) override {
    // do nothing
  }

  void order(inode_vector& /*iv*/) override {
    // do nothing
  }
};

} // namespace test
} // namespace dwarfs

using namespace dwarfs;

namespace {

void basic_end_to_end_test(std::string const& compressor,
                           unsigned block_size_bits, file_order_mode file_order,
                           bool with_devices, bool with_specials, bool set_uid,
                           bool set_gid, bool set_time, bool keep_all_times,
                           bool enable_nlink) {
  block_manager::config cfg;
  scanner_options options;

  cfg.blockhash_window_size = 10;
  cfg.block_size_bits = block_size_bits;

  options.file_order.mode = file_order;
  options.with_devices = with_devices;
  options.with_specials = with_specials;
  options.inode.with_similarity = file_order == file_order_mode::SIMILARITY;
  options.inode.with_nilsimsa = file_order == file_order_mode::NILSIMSA;
  options.keep_all_times = keep_all_times;

  if (set_uid) {
    options.uid = 0;
  }

  if (set_gid) {
    options.gid = 0;
  }

  if (set_time) {
    options.timestamp = 4711;
  }

  // force multithreading
  worker_group wg("writer", 4);

  std::ostringstream logss;
  stream_logger lgr(logss); // TODO: mock
  lgr.set_policy<prod_logger_policy>();

  scanner s(lgr, wg, cfg, entry_factory::create(),
            std::make_shared<test::os_access_mock>(),
            std::make_shared<test::script_mock>(), options);

  std::ostringstream oss;
  progress prog([](const progress&, bool) {}, 1000);

  block_compressor bc(compressor);
  filesystem_writer fsw(oss, lgr, wg, prog, bc, 64 << 20);

  s.scan(fsw, "", prog);

  auto mm = std::make_shared<test::mmap_mock>(oss.str());

  filesystem_options opts;
  opts.block_cache.max_bytes = 1 << 20;
  opts.metadata.enable_nlink = enable_nlink;

  filesystem_v2 fs(lgr, mm, opts);

  auto entry = fs.find("/foo.pl");
  struct ::stat st;

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 23456);
  EXPECT_EQ(st.st_uid, set_uid ? 0 : 1337);
  EXPECT_EQ(st.st_gid, 0);
  EXPECT_EQ(st.st_atime, set_time ? 4711 : keep_all_times ? 4001 : 4002);
  EXPECT_EQ(st.st_mtime, set_time ? 4711 : keep_all_times ? 4002 : 4002);
  EXPECT_EQ(st.st_ctime, set_time ? 4711 : keep_all_times ? 4003 : 4002);

  int inode = fs.open(*entry);
  EXPECT_GE(inode, 0);

  std::vector<char> buf(st.st_size);
  ssize_t rv = fs.read(inode, &buf[0], st.st_size, 0);
  EXPECT_EQ(rv, st.st_size);
  EXPECT_EQ(std::string(buf.begin(), buf.end()), test::loremipsum(st.st_size));

  entry = fs.find("/somelink");

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 16);
  EXPECT_EQ(st.st_uid, set_uid ? 0 : 1000);
  EXPECT_EQ(st.st_gid, set_gid ? 0 : 100);
  EXPECT_EQ(st.st_rdev, 0);
  EXPECT_EQ(st.st_atime, set_time ? 4711 : keep_all_times ? 2001 : 2002);
  EXPECT_EQ(st.st_mtime, set_time ? 4711 : keep_all_times ? 2002 : 2002);
  EXPECT_EQ(st.st_ctime, set_time ? 4711 : keep_all_times ? 2003 : 2002);

  std::string link;
  EXPECT_EQ(fs.readlink(*entry, &link), 0);
  EXPECT_EQ(link, "somedir/ipsum.py");

  EXPECT_FALSE(fs.find("/somedir/nope"));

  entry = fs.find("/somedir/bad");

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 6);

  EXPECT_EQ(fs.readlink(*entry, &link), 0);
  EXPECT_EQ(link, "../foo");

  entry = fs.find("/somedir/pipe");

  if (with_specials) {
    ASSERT_TRUE(entry);
    EXPECT_EQ(fs.getattr(*entry, &st), 0);
    EXPECT_EQ(st.st_size, 0);
    EXPECT_EQ(st.st_uid, set_uid ? 0 : 1000);
    EXPECT_EQ(st.st_gid, set_gid ? 0 : 100);
    EXPECT_TRUE(S_ISFIFO(st.st_mode));
    EXPECT_EQ(st.st_rdev, 0);
    EXPECT_EQ(st.st_atime, set_time ? 4711 : keep_all_times ? 8001 : 8002);
    EXPECT_EQ(st.st_mtime, set_time ? 4711 : keep_all_times ? 8002 : 8002);
    EXPECT_EQ(st.st_ctime, set_time ? 4711 : keep_all_times ? 8003 : 8002);
  } else {
    EXPECT_FALSE(entry);
  }

  entry = fs.find("/somedir/null");

  if (with_devices) {
    ASSERT_TRUE(entry);
    EXPECT_EQ(fs.getattr(*entry, &st), 0);
    EXPECT_EQ(st.st_size, 0);
    EXPECT_EQ(st.st_uid, 0);
    EXPECT_EQ(st.st_gid, 0);
    EXPECT_TRUE(S_ISCHR(st.st_mode));
    EXPECT_EQ(st.st_rdev, 259);
  } else {
    EXPECT_FALSE(entry);
  }

  entry = fs.find("/somedir/zero");

  if (with_devices) {
    ASSERT_TRUE(entry);
    EXPECT_EQ(fs.getattr(*entry, &st), 0);
    EXPECT_EQ(st.st_size, 0);
    EXPECT_EQ(st.st_uid, 0);
    EXPECT_EQ(st.st_gid, 0);
    EXPECT_TRUE(S_ISCHR(st.st_mode));
    EXPECT_EQ(st.st_rdev, 261);
    EXPECT_EQ(st.st_atime, set_time         ? 4711
                           : keep_all_times ? 4000010001
                                            : 4000020002);
    EXPECT_EQ(st.st_mtime, set_time         ? 4711
                           : keep_all_times ? 4000020002
                                            : 4000020002);
    EXPECT_EQ(st.st_ctime, set_time         ? 4711
                           : keep_all_times ? 4000030003
                                            : 4000020002);
  } else {
    EXPECT_FALSE(entry);
  }

  entry = fs.find("/");

  ASSERT_TRUE(entry);
  auto dir = fs.opendir(*entry);
  ASSERT_TRUE(dir);
  EXPECT_EQ(9, fs.dirsize(*dir));

  entry = fs.find("/somedir");

  ASSERT_TRUE(entry);
  dir = fs.opendir(*entry);
  ASSERT_TRUE(dir);
  EXPECT_EQ(4 + 2 * with_devices + with_specials, fs.dirsize(*dir));

  std::vector<std::string> names;
  for (size_t i = 0; i < fs.dirsize(*dir); ++i) {
    auto r = fs.readdir(*dir, i);
    ASSERT_TRUE(r);
    auto [view, name] = *r;
    names.emplace_back(name);
  }

  std::vector<std::string> expected{
      ".",
      "..",
      "bad",
      "ipsum.py",
  };

  if (with_devices) {
    expected.emplace_back("null");
  }

  if (with_specials) {
    expected.emplace_back("pipe");
  }

  if (with_devices) {
    expected.emplace_back("zero");
  }

  EXPECT_EQ(expected, names);

  entry = fs.find("/foo.pl");
  ASSERT_TRUE(entry);

  auto e2 = fs.find("/bar.pl");
  ASSERT_TRUE(e2);

  EXPECT_EQ(entry->inode(), e2->inode());

  struct ::stat st1, st2;
  ASSERT_EQ(0, fs.getattr(*entry, &st1));
  ASSERT_EQ(0, fs.getattr(*e2, &st2));

  EXPECT_EQ(st1.st_ino, st2.st_ino);
  if (enable_nlink) {
    EXPECT_EQ(3, st1.st_nlink); // TODO: this should be 2
    EXPECT_EQ(3, st2.st_nlink); // TODO: this should be 2
  }

  entry = fs.find("/");
  ASSERT_TRUE(entry);
  EXPECT_EQ(0, entry->inode());
  e2 = fs.find(0);
  ASSERT_TRUE(e2);
  EXPECT_EQ(e2->inode(), 0);
  e2 = fs.find(0, "baz.pl");
  ASSERT_TRUE(e2);
  EXPECT_GT(e2->inode(), 0);
  entry = fs.find(e2->inode());
  ASSERT_TRUE(entry);
  ASSERT_EQ(0, fs.getattr(*entry, &st1));
  EXPECT_EQ(23456, st1.st_size);
  e2 = fs.find(0, "somedir");
  ASSERT_TRUE(e2);
  ASSERT_EQ(0, fs.getattr(*e2, &st2));
  entry = fs.find(st2.st_ino, "ipsum.py");
  ASSERT_TRUE(entry);
  ASSERT_EQ(0, fs.getattr(*entry, &st1));
  EXPECT_EQ(10000, st1.st_size);
  EXPECT_EQ(0, fs.access(*entry, R_OK, 1000, 100));
  entry = fs.find(0, "baz.pl");
  ASSERT_TRUE(entry);
  EXPECT_EQ(set_uid ? EACCES : 0, fs.access(*entry, R_OK, 1337, 0));

  using mptype = void (filesystem_v2::*)(
      std::function<void(entry_view, directory_view)> const&) const;

  for (auto mp : {static_cast<mptype>(&filesystem_v2::walk),
                  static_cast<mptype>(&filesystem_v2::walk_inode_order)}) {
    std::map<std::string, struct ::stat> entries;
    std::vector<int> inodes;

    (fs.*mp)([&](entry_view e, directory_view d) {
      struct ::stat stbuf;
      ASSERT_EQ(0, fs.getattr(e, &stbuf));
      inodes.push_back(stbuf.st_ino);
      std::string path;
      if (e.inode() > 0) {
        if (auto dp = d.path(); !dp.empty()) {
          path += "/" + dp;
        }
        path += "/" + std::string(e.name());
      }
      EXPECT_TRUE(entries.emplace(path, stbuf).second);
    });

    EXPECT_EQ(entries.size(), dwarfs::test::statmap.size() + 2 * with_devices +
                                  with_specials - 3);

    for (auto const& [p, st] : entries) {
      auto const& stref = dwarfs::test::statmap.at(p);
      EXPECT_EQ(stref.st_mode, st.st_mode) << p;
      EXPECT_EQ(set_uid ? 0 : stref.st_uid, st.st_uid) << p;
      EXPECT_EQ(set_gid ? 0 : stref.st_gid, st.st_gid) << p;
      if (!S_ISDIR(st.st_mode)) {
        EXPECT_EQ(stref.st_size, st.st_size) << p;
      }
    }

    if (mp == static_cast<mptype>(&filesystem_v2::walk_inode_order)) {
      EXPECT_TRUE(std::is_sorted(inodes.begin(), inodes.end()));
    }
  }

  std::ostringstream dumpss;

  fs.dump(dumpss, 9);

  EXPECT_GT(dumpss.str().size(), 1000) << dumpss.str();

  auto dyn = fs.metadata_as_dynamic();

  EXPECT_TRUE(dyn.isObject());

  auto json = fs.serialize_metadata_as_json(true);

  EXPECT_GT(json.size(), 1000) << json;

  json = fs.serialize_metadata_as_json(false);

  EXPECT_GT(json.size(), 1000) << json;
}

std::vector<std::string> const compressions{"null",
#ifdef DWARFS_HAVE_LIBLZ4
                                            "lz4", "lz4hc:level=4",
#endif
#ifdef DWARFS_HAVE_LIBZSTD
                                            "zstd:level=1",
#endif
#ifdef DWARFS_HAVE_LIBLZMA
                                            "lzma:level=1"
#endif
};

} // namespace

class compression_test
    : public testing::TestWithParam<
          std::tuple<std::string, unsigned, file_order_mode>> {};

class scanner_test : public testing::TestWithParam<
                         std::tuple<bool, bool, bool, bool, bool, bool, bool>> {
};

TEST_P(compression_test, end_to_end) {
  auto [compressor, block_size_bits, file_order] = GetParam();

  if (compressor.find("lzma") == 0 && block_size_bits < 16) {
    // these are notoriously slow, so just skip them
    return;
  }

  basic_end_to_end_test(compressor, block_size_bits, file_order, true, true,
                        false, false, false, false, false);
}

TEST_P(scanner_test, end_to_end) {
  auto [with_devices, with_specials, set_uid, set_gid, set_time, keep_all_times,
        enable_nlink] = GetParam();

  basic_end_to_end_test(compressions[0], 15, file_order_mode::NONE,
                        with_devices, with_specials, set_uid, set_gid, set_time,
                        keep_all_times, enable_nlink);
}

INSTANTIATE_TEST_SUITE_P(
    dwarfs, compression_test,
    ::testing::Combine(
        ::testing::ValuesIn(compressions), ::testing::Values(12, 15, 20, 28),
        ::testing::Values(file_order_mode::NONE, file_order_mode::PATH,
                          file_order_mode::SCRIPT, file_order_mode::NILSIMSA,
                          file_order_mode::SIMILARITY)));

INSTANTIATE_TEST_SUITE_P(
    dwarfs, scanner_test,
    ::testing::Combine(::testing::Bool(), ::testing::Bool(), ::testing::Bool(),
                       ::testing::Bool(), ::testing::Bool(), ::testing::Bool(),
                       ::testing::Bool()));

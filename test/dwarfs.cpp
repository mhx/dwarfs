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

#include <map>
#include <sstream>

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
  ::mode_t st_mode;
  ::uid_t st_uid;
  ::gid_t st_gid;
  ::off_t st_size;
  ::dev_t st_rdev;
};

std::map<std::string, simplestat> statmap{
    {"", {S_IFDIR | 0777, 1000, 100, 0, 0}},
    {"/test.pl", {S_IFREG | 0644, 1000, 100, 0, 0}},
    {"/somelink", {S_IFLNK | 0777, 1000, 100, 16, 0}},
    {"/somedir", {S_IFDIR | 0777, 1000, 100, 0, 0}},
    {"/foo.pl", {S_IFREG | 0600, 1337, 0, 23456, 0}},
    {"/ipsum.txt", {S_IFREG | 0644, 1000, 100, 2000000, 0}},
    {"/somedir/ipsum.py", {S_IFREG | 0644, 1000, 100, 10000, 0}},
    {"/somedir/bad", {S_IFLNK | 0777, 1000, 100, 6, 0}},
    {"/somedir/pipe", {S_IFIFO | 0644, 1000, 100, 0, 0}},
    {"/somedir/null", {S_IFCHR | 0666, 0, 0, 0, 259}},
    {"/somedir/zero", {S_IFCHR | 0666, 0, 0, 0, 261}},
};
} // namespace

class mmap_mock : public mmif {
 public:
  mmap_mock(const std::string& data)
      : m_data(data) {
    assign(m_data.data(), m_data.size());
  }

 private:
  const std::string m_data;
};

class os_access_mock : public os_access {
 public:
  std::shared_ptr<dir_reader> opendir(const std::string& path) const override {
    if (path.empty()) {
      std::vector<std::string> files{
          ".", "..", "test.pl", "somelink", "somedir", "foo.pl", "ipsum.txt",
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
    ::memset(st, 0, sizeof(*st));
    st->st_mode = sst.st_mode;
    st->st_uid = sst.st_uid;
    st->st_gid = sst.st_gid;
    st->st_size = sst.st_size;
    st->st_atime = 123;
    st->st_mtime = 234;
    st->st_ctime = 345;
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
  bool filter(file_interface const& /*fi*/) const override { return true; }

  void order(file_vector& /*fvi*/) const override {
    // do nothing
  }
};

} // namespace test
} // namespace dwarfs

using namespace dwarfs;

namespace {

void basic_end_to_end_test(const std::string& compressor,
                           unsigned block_size_bits,
                           file_order_mode file_order) {
  block_manager::config cfg;
  scanner_options options;

  cfg.blockhash_window_size.push_back(1 << 10);
  cfg.block_size_bits = block_size_bits;

  options.file_order = file_order;

  // force multithreading
  worker_group wg("writer", 4);

  std::ostringstream logss;
  stream_logger lgr(logss); // TODO: mock
  lgr.set_policy<prod_logger_policy>();

  scanner s(lgr, wg, cfg,
            entry_factory::create(file_order == file_order_mode::SIMILARITY),
            std::make_shared<test::os_access_mock>(),
            std::make_shared<test::script_mock>(), options);

  std::ostringstream oss;
  progress prog([](const progress&, bool) {});

  block_compressor bc(compressor);
  filesystem_writer fsw(oss, lgr, wg, prog, bc, 64 << 20);

  s.scan(fsw, "", prog);

  auto mm = std::make_shared<test::mmap_mock>(oss.str());

  filesystem_options opts;
  opts.block_cache.max_bytes = 1 << 20;

  filesystem_v2 fs(lgr, mm, opts);

  auto entry = fs.find("/foo.pl");
  struct ::stat st;

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 23456);
  EXPECT_EQ(st.st_uid, 1337);
  EXPECT_EQ(st.st_gid, 0);

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
  EXPECT_EQ(st.st_uid, 1000);
  EXPECT_EQ(st.st_gid, 100);
  EXPECT_EQ(st.st_rdev, 0);

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

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 0);
  EXPECT_EQ(st.st_uid, 1000);
  EXPECT_EQ(st.st_gid, 100);
  EXPECT_TRUE(S_ISFIFO(st.st_mode));
  EXPECT_EQ(st.st_rdev, 0);

  entry = fs.find("/somedir/null");

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 0);
  EXPECT_EQ(st.st_uid, 0);
  EXPECT_EQ(st.st_gid, 0);
  EXPECT_TRUE(S_ISCHR(st.st_mode));
  EXPECT_EQ(st.st_rdev, 259);

  entry = fs.find("/somedir/zero");

  ASSERT_TRUE(entry);
  EXPECT_EQ(fs.getattr(*entry, &st), 0);
  EXPECT_EQ(st.st_size, 0);
  EXPECT_EQ(st.st_uid, 0);
  EXPECT_EQ(st.st_gid, 0);
  EXPECT_TRUE(S_ISCHR(st.st_mode));
  EXPECT_EQ(st.st_rdev, 261);
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

class basic : public testing::TestWithParam<
                  std::tuple<std::string, unsigned, file_order_mode>> {};

TEST_P(basic, end_to_end) {
  basic_end_to_end_test(std::get<0>(GetParam()), std::get<1>(GetParam()),
                        std::get<2>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    dwarfs, basic,
    ::testing::Combine(::testing::ValuesIn(compressions),
                       ::testing::Values(12, 15, 20, 28),
                       ::testing::Values(file_order_mode::NONE,
                                         file_order_mode::PATH,
                                         file_order_mode::SCRIPT,
                                         file_order_mode::SIMILARITY)));

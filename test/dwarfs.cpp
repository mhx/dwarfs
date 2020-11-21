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

#include <sstream>
#include <unordered_map>

#include <gtest/gtest.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/entry.h"
#include "dwarfs/filesystem.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
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
};

std::unordered_map<std::string, simplestat> statmap{
    {"/", {S_IFDIR | 0777, 1000, 1000, 0}},
    {"//test.pl", {S_IFREG | 0644, 1000, 1000, 0}},
    {"//somelink", {S_IFLNK | 0777, 1000, 1000, 16}},
    {"//somedir", {S_IFDIR | 0777, 1000, 1000, 0}},
    {"//foo.pl", {S_IFREG | 0600, 1337, 0, 23456}},
    {"//ipsum.txt", {S_IFREG | 0644, 1000, 1000, 2000000}},
    {"//somedir/ipsum.py", {S_IFREG | 0644, 1000, 1000, 10000}},
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
    if (path == "/") {
      std::vector<std::string> files{
          ".", "..", "test.pl", "somelink", "somedir", "foo.pl", "ipsum.txt",
      };

      return std::make_shared<dir_reader_mock>(std::move(files));
    } else if (path == "//somedir") {
      std::vector<std::string> files{
          ".",
          "..",
          "ipsum.py",
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
  }

  std::string readlink(const std::string& path, size_t size) const override {
    if (path == "//somelink" && size == 16) {
      return "somedir/ipsum.py";
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
                           unsigned block_size_bits, file_order_mode file_order,
                           bool no_owner, bool no_time) {
  block_manager::config cfg;
  scanner_options options;

  cfg.blockhash_window_size.push_back(1 << 10);
  cfg.block_size_bits = block_size_bits;

  options.file_order = file_order;

  // force multithreading
  worker_group wg("writer", 4);

  std::ostringstream logss;
  stream_logger lgr(logss); // TODO: mock
  lgr.set_policy<debug_logger_policy>();

  scanner s(lgr, wg, cfg,
            entry_factory::create(no_owner, no_time,
                                  file_order == file_order_mode::SIMILARITY),
            std::make_shared<test::os_access_mock>(),
            std::make_shared<test::script_mock>(), options);

  std::ostringstream oss;
  progress prog([](const progress&, bool) {});

  block_compressor bc(compressor);
  filesystem_writer fsw(oss, lgr, wg, prog, bc, 64 << 20);

  s.scan(fsw, "/", prog);

  block_cache_options bco;
  bco.max_bytes = 1 << 20;
  filesystem fs(lgr, std::make_shared<test::mmap_mock>(oss.str()), bco);

  auto de = fs.find("/foo.pl");
  struct ::stat st;

  ASSERT_NE(de, nullptr);
  EXPECT_EQ(fs.getattr(de, &st), 0);
  EXPECT_EQ(st.st_size, 23456);

  int inode = fs.open(de);
  EXPECT_GE(inode, 0);

  std::vector<char> buf(st.st_size);
  ssize_t rv = fs.read(inode, &buf[0], st.st_size, 0);
  EXPECT_EQ(rv, st.st_size);
  EXPECT_EQ(std::string(buf.begin(), buf.end()), test::loremipsum(st.st_size));
}
} // namespace

TEST(dwarfs, basic_test) {
  std::vector<std::string> compressions{"null",
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
  std::vector<unsigned> block_sizes{12, 15, 20, 28};
  std::vector<file_order_mode> file_orders{
      file_order_mode::NONE, file_order_mode::PATH, file_order_mode::SCRIPT,
      file_order_mode::SIMILARITY};

  for (const auto& comp : compressions) {
    for (const auto& bs : block_sizes) {
      for (const auto& order : file_orders) {
        basic_end_to_end_test(comp, bs, order, false, false);
        basic_end_to_end_test(comp, bs, order, false, true);
        basic_end_to_end_test(comp, bs, order, true, true);
      }
    }
  }
}

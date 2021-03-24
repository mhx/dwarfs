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

#include <cstring>
#include <map>

#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_helpers.h"

namespace dwarfs {
namespace test {

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

dir_reader_mock::dir_reader_mock(std::vector<std::string>&& files)
    : m_files(files)
    , m_index(0) {}

bool dir_reader_mock::read(std::string& name) const {
  if (m_index < m_files.size()) {
    name = m_files[m_index++];
    return true;
  }

  return false;
}

std::shared_ptr<dir_reader>
os_access_mock::opendir(const std::string& path) const {
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

void os_access_mock::lstat(const std::string& path, struct ::stat* st) const {
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

std::string
os_access_mock::readlink(const std::string& path, size_t size) const {
  if (path == "/somelink" && size == 16) {
    return "somedir/ipsum.py";
  } else if (path == "/somedir/bad" && size == 6) {
    return "../foo";
  }

  throw std::runtime_error("oops");
}

std::shared_ptr<mmif>
os_access_mock::map_file(const std::string& path, size_t size) const {
  const simplestat& sst = statmap[path];

  if (size == static_cast<size_t>(sst.st_size)) {
    return std::make_shared<mmap_mock>(loremipsum(size));
  }

  throw std::runtime_error("oops");
}

int os_access_mock::access(const std::string&, int) const { return 0; }

} // namespace test
} // namespace dwarfs

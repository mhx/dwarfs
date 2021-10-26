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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>

#include <sys/stat.h>
#include <unistd.h>

#include <folly/String.h>

#include "dwarfs/overloaded.h"
#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_helpers.h"

namespace dwarfs {
namespace test {

struct os_access_mock::mock_dirent {
  std::string name;
  struct ::stat stat;
  std::variant<std::monostate, std::string, std::function<std::string()>,
               std::unique_ptr<mock_directory>>
      v;

  size_t size() const;

  mock_dirent* find(std::string const& name);

  void
  add(std::string const& name, struct ::stat const& st,
      std::variant<std::monostate, std::string, std::function<std::string()>,
                   std::unique_ptr<mock_directory>>
          var);
};

struct os_access_mock::mock_directory {
  std::vector<mock_dirent> ent;

  size_t size() const;

  mock_dirent* find(std::string const& name);

  void
  add(std::string const& name, struct ::stat const& st,
      std::variant<std::monostate, std::string, std::function<std::string()>,
                   std::unique_ptr<mock_directory>>
          var);
};

size_t os_access_mock::mock_dirent::size() const {
  size_t s = 1;
  if (auto p = std::get_if<std::unique_ptr<mock_directory>>(&v)) {
    s += (*p)->size();
  }
  return s;
}

auto os_access_mock::mock_dirent::find(std::string const& name)
    -> mock_dirent* {
  return std::get<std::unique_ptr<mock_directory>>(v)->find(name);
}

void os_access_mock::mock_dirent::add(
    std::string const& name, struct ::stat const& st,
    std::variant<std::monostate, std::string, std::function<std::string()>,
                 std::unique_ptr<mock_directory>>
        var) {
  return std::get<std::unique_ptr<mock_directory>>(v)->add(name, st,
                                                           std::move(var));
}

size_t os_access_mock::mock_directory::size() const {
  size_t s = 0;
  for (auto const& e : ent) {
    s += e.size();
  }
  return s;
}

auto os_access_mock::mock_directory::find(std::string const& name)
    -> mock_dirent* {
  auto it = std::find_if(ent.begin(), ent.end(),
                         [&](auto& de) { return de.name == name; });
  return it != ent.end() ? &*it : nullptr;
}

void os_access_mock::mock_directory::add(
    std::string const& name, struct ::stat const& st,
    std::variant<std::monostate, std::string, std::function<std::string()>,
                 std::unique_ptr<mock_directory>>
        var) {
  assert(!find(name));

  if (S_ISDIR(st.st_mode)) {
    assert(std::holds_alternative<std::unique_ptr<mock_directory>>(var));
  } else {
    assert(!std::holds_alternative<std::unique_ptr<mock_directory>>(var));
  }

  auto& de = ent.emplace_back();
  de.name = name;
  de.stat = st;
  de.v = std::move(var);
}

class dir_reader_mock : public dir_reader {
 public:
  explicit dir_reader_mock(std::vector<std::string>&& files)
      : files_(files)
      , index_(0) {}

  bool read(std::string& name) const override {
    if (index_ < files_.size()) {
      name = files_[index_++];
      return true;
    }

    return false;
  }

 private:
  std::vector<std::string> files_;
  mutable size_t index_;
};

os_access_mock::os_access_mock() = default;
os_access_mock::~os_access_mock() = default;

std::shared_ptr<os_access_mock> os_access_mock::create_test_instance() {
  static const std::vector<std::pair<std::string, simplestat>> statmap{
      {"", {1, S_IFDIR | 0777, 1, 1000, 100, 0, 0, 1, 2, 3}},
      {"test.pl", {3, S_IFREG | 0644, 2, 1000, 100, 0, 0, 1001, 1002, 1003}},
      {"somelink", {4, S_IFLNK | 0777, 1, 1000, 100, 16, 0, 2001, 2002, 2003}},
      {"somedir", {5, S_IFDIR | 0777, 1, 1000, 100, 0, 0, 3001, 3002, 3003}},
      {"foo.pl", {6, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 4001, 4002, 4003}},
      {"bar.pl", {6, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 4001, 4002, 4003}},
      {"baz.pl", {16, S_IFREG | 0600, 2, 1337, 0, 23456, 0, 8001, 8002, 8003}},
      {"ipsum.txt",
       {7, S_IFREG | 0644, 1, 1000, 100, 2000000, 0, 5001, 5002, 5003}},
      {"somedir/ipsum.py",
       {9, S_IFREG | 0644, 1, 1000, 100, 10000, 0, 6001, 6002, 6003}},
      {"somedir/bad",
       {10, S_IFLNK | 0777, 1, 1000, 100, 6, 0, 7001, 7002, 7003}},
      {"somedir/pipe",
       {12, S_IFIFO | 0644, 1, 1000, 100, 0, 0, 8001, 8002, 8003}},
      {"somedir/null", {13, S_IFCHR | 0666, 1, 0, 0, 0, 259, 9001, 9002, 9003}},
      {"somedir/zero",
       {14, S_IFCHR | 0666, 1, 0, 0, 0, 261, 4000010001, 4000020002,
        4000030003}},
  };

  static std::map<std::string, std::string> linkmap{
      {"somelink", "somedir/ipsum.py"},
      {"somedir/bad", "../foo"},
  };

  auto m = std::make_shared<os_access_mock>();

  for (auto const& kv : statmap) {
    const auto& sst = kv.second;
    struct ::stat st;

    std::memset(&st, 0, sizeof(st));

    st.st_ino = sst.st_ino;
    st.st_mode = sst.st_mode;
    st.st_nlink = sst.st_nlink;
    st.st_uid = sst.st_uid;
    st.st_gid = sst.st_gid;
    st.st_size = sst.st_size;
    st.st_atime = sst.atime;
    st.st_mtime = sst.mtime;
    st.st_ctime = sst.ctime;
    st.st_rdev = sst.st_rdev;

    if (S_ISREG(st.st_mode)) {
      m->add(kv.first, st, [size = st.st_size] { return loremipsum(size); });
    } else if (S_ISLNK(st.st_mode)) {
      m->add(kv.first, st, linkmap.at(kv.first));
    } else {
      m->add(kv.first, st);
    }
  }

  return m;
}

void os_access_mock::add(std::filesystem::path const& path,
                         struct ::stat const& st) {
  add_internal(path, st, std::monostate{});
}

void os_access_mock::add(std::filesystem::path const& path,
                         struct ::stat const& st, std::string const& contents) {
  add_internal(path, st, contents);
}

void os_access_mock::add(std::filesystem::path const& path,
                         struct ::stat const& st,
                         std::function<std::string()> generator) {
  add_internal(path, st, generator);
}

void os_access_mock::add_dir(std::filesystem::path const& path) {
  struct ::stat st;
  std::memset(&st, 0, sizeof(st));
  st.st_ino = ino_++;
  st.st_mode = S_IFDIR | 0755;
  st.st_uid = 1000;
  st.st_gid = 100;
  add(path, st);
}

void os_access_mock::add_file(std::filesystem::path const& path, size_t size) {
  struct ::stat st;
  std::memset(&st, 0, sizeof(st));
  st.st_ino = ino_++;
  st.st_mode = S_IFREG | 0644;
  st.st_uid = 1000;
  st.st_gid = 100;
  st.st_size = size;
  add(path, st, [size] { return loremipsum(size); });
}

void os_access_mock::add_file(std::filesystem::path const& path,
                              std::string const& contents) {
  struct ::stat st;
  std::memset(&st, 0, sizeof(st));
  st.st_ino = ino_++;
  st.st_mode = S_IFREG | 0644;
  st.st_uid = 1000;
  st.st_gid = 100;
  st.st_size = contents.size();
  add(path, st, contents);
}

size_t os_access_mock::size() const { return root_ ? root_->size() : 0; }

std::vector<std::string>
os_access_mock::splitpath(std::filesystem::path const& path) {
  std::vector<std::string> parts;
  folly::split('/', path.native(), parts);
  while (!parts.empty() && parts.front().empty()) {
    parts.erase(parts.begin());
  }
  return parts;
}

auto os_access_mock::find(std::filesystem::path const& path) const
    -> mock_dirent* {
  return find(splitpath(path));
}

auto os_access_mock::find(std::vector<std::string> parts) const
    -> mock_dirent* {
  assert(root_);
  auto* de = root_.get();
  while (!parts.empty()) {
    if (!S_ISDIR(de->stat.st_mode)) {
      return nullptr;
    }
    de = de->find(parts.front());
    if (!de) {
      return nullptr;
    }
    parts.erase(parts.begin());
  }
  return de;
}

void os_access_mock::add_internal(
    std::filesystem::path const& path, struct ::stat const& st,
    std::variant<std::monostate, std::string, std::function<std::string()>,
                 std::unique_ptr<mock_directory>>
        var) {
  auto parts = splitpath(path);

  if (S_ISDIR(st.st_mode) && std::holds_alternative<std::monostate>(var)) {
    var = std::make_unique<mock_directory>();
  }

  if (parts.empty()) {
    assert(!root_);
    assert(S_ISDIR(st.st_mode));
    assert(std::holds_alternative<std::unique_ptr<mock_directory>>(var));
    root_ = std::make_unique<mock_dirent>();
    root_->stat = st;
    root_->v = std::move(var);
  } else {
    auto name = parts.back();
    parts.pop_back();
    auto* de = find(std::move(parts));
    assert(de);
    de->add(name, st, std::move(var));
  }
}

std::shared_ptr<dir_reader>
os_access_mock::opendir(const std::string& path) const {
  if (auto de = find(path); de && S_ISDIR(de->stat.st_mode)) {
    std::vector<std::string> files{".", ".."};
    for (auto const& e :
         std::get<std::unique_ptr<mock_directory>>(de->v)->ent) {
      files.push_back(e.name);
    }
    return std::make_shared<dir_reader_mock>(std::move(files));
  }

  throw std::runtime_error("oops");
}

void os_access_mock::lstat(const std::string& path, struct ::stat* st) const {
  if (auto de = find(path)) {
    std::memcpy(st, &de->stat, sizeof(*st));
  }
}

std::string
os_access_mock::readlink(const std::string& path, size_t size) const {
  if (auto de = find(path); de && S_ISLNK(de->stat.st_mode)) {
    return std::get<std::string>(de->v);
  }

  throw std::runtime_error("oops");
}

std::shared_ptr<mmif>
os_access_mock::map_file(const std::string& path, size_t size) const {
  if (auto de = find(path); de && S_ISREG(de->stat.st_mode)) {
    return std::make_shared<mmap_mock>(std::visit(
        overloaded{
            [this](std::string const& str) { return str; },
            [this](std::function<std::string()> const& fun) { return fun(); },
            [this](auto const&) -> std::string {
              throw std::runtime_error("oops");
            },
        },
        de->v));
  }

  throw std::runtime_error("oops");
}

int os_access_mock::access(const std::string&, int) const { return 0; }

std::optional<std::filesystem::path> find_binary(std::string_view name) {
  auto path_str = std::getenv("PATH");
  if (!path_str) {
    return std::nullopt;
  }

  std::vector<std::string> path;
  folly::split(':', path_str, path);

  for (auto dir : path) {
    auto cand = std::filesystem::path(dir) / name;
    if (std::filesystem::exists(cand) and ::access(cand.c_str(), X_OK) == 0) {
      return cand;
    }
  }

  return std::nullopt;
}

} // namespace test
} // namespace dwarfs

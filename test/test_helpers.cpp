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
#include <random>
#include <regex>

#include <fmt/format.h>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/portability/Unistd.h>

#include "dwarfs/os_access_generic.h"
#include "dwarfs/overloaded.h"
#include "dwarfs/util.h"
#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_helpers.h"

namespace dwarfs::test {

namespace fs = std::filesystem;

namespace {

file_stat make_file_stat(simplestat const& ss) {
  file_stat rv;
  ::memset(&rv, 0, sizeof(rv));
  rv.ino = ss.ino;
  rv.nlink = ss.nlink;
  rv.mode = ss.mode;
  rv.uid = ss.uid;
  rv.gid = ss.gid;
  rv.rdev = ss.rdev;
  rv.size = ss.size;
  rv.atime = ss.atime;
  rv.mtime = ss.mtime;
  rv.ctime = ss.ctime;
  return rv;
}

} // namespace

struct os_access_mock::mock_dirent {
  std::string name;
  simplestat status;
  value_variant_type v;

  size_t size() const;

  mock_dirent* find(std::string const& name);

  void
  add(std::string const& name, simplestat const& st, value_variant_type var);
};

struct os_access_mock::mock_directory {
  std::vector<mock_dirent> ent;
  std::unordered_map<std::string, size_t> cache;

  size_t size() const;

  mock_dirent* find(std::string const& name);

  void
  add(std::string const& name, simplestat const& st, value_variant_type var);
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

void os_access_mock::mock_dirent::add(std::string const& name,
                                      simplestat const& st,
                                      value_variant_type var) {
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
  auto it = cache.find(name);
  return it != cache.end() ? &ent[it->second] : nullptr;
}

void os_access_mock::mock_directory::add(std::string const& name,
                                         simplestat const& st,
                                         value_variant_type var) {
  assert(!find(name));

  if (st.type() == posix_file_type::directory) {
    assert(std::holds_alternative<std::unique_ptr<mock_directory>>(var));
  } else {
    assert(!std::holds_alternative<std::unique_ptr<mock_directory>>(var));
  }

  cache.emplace(name, ent.size());
  auto& de = ent.emplace_back();
  de.name = name;
  de.status = st;
  de.v = std::move(var);
}

class dir_reader_mock : public dir_reader {
 public:
  explicit dir_reader_mock(std::vector<fs::path>&& files)
      : files_(files)
      , index_(0) {}

  bool read(fs::path& name) override {
    if (index_ < files_.size()) {
      name = files_[index_++];
      return true;
    }

    return false;
  }

 private:
  std::vector<fs::path> files_;
  size_t index_;
};

os_access_mock::os_access_mock()
    : real_os_{std::make_shared<os_access_generic>()} {}

os_access_mock::~os_access_mock() = default;

std::shared_ptr<os_access_mock> os_access_mock::create_test_instance() {
  static const std::vector<std::pair<std::string, simplestat>> statmap{
      {"", {1, posix_file_type::directory | 0777, 1, 1000, 100, 0, 0, 1, 2, 3}},
      {"test.pl",
       {3, posix_file_type::regular | 0644, 2, 1000, 100, 0, 0, 1001, 1002,
        1003}},
      {"somelink",
       {4, posix_file_type::symlink | 0777, 1, 1000, 100, 16, 0, 2001, 2002,
        2003}},
      {"somedir",
       {5, posix_file_type::directory | 0777, 1, 1000, 100, 0, 0, 3001, 3002,
        3003}},
      {"foo.pl",
       {6, posix_file_type::regular | 0600, 2, 1337, 0, 23456, 0, 4001, 4002,
        4003}},
      {"bar.pl",
       {6, posix_file_type::regular | 0600, 2, 1337, 0, 23456, 0, 4001, 4002,
        4003}},
      {"baz.pl",
       {16, posix_file_type::regular | 0600, 2, 1337, 0, 23456, 0, 8001, 8002,
        8003}},
      {"ipsum.txt",
       {7, posix_file_type::regular | 0644, 1, 1000, 100, 2000000, 0, 5001,
        5002, 5003}},
      {"somedir/ipsum.py",
       {9, posix_file_type::regular | 0644, 1, 1000, 100, 10000, 0, 6001, 6002,
        6003}},
      {"somedir/bad",
       {10, posix_file_type::symlink | 0777, 1, 1000, 100, 6, 0, 7001, 7002,
        7003}},
      {"somedir/pipe",
       {12, posix_file_type::fifo | 0644, 1, 1000, 100, 0, 0, 8001, 8002,
        8003}},
      {"somedir/null",
       {13, posix_file_type::character | 0666, 1, 0, 0, 0, 259, 9001, 9002,
        9003}},
      {"somedir/zero",
       {14, posix_file_type::character | 0666, 1, 0, 0, 0, 261, 4000010001,
        4000020002, 4000030003}},
      {"somedir/empty",
       {212, posix_file_type::regular | 0644, 1, 1000, 100, 0, 0, 8101, 8102,
        8103}},
      {"empty",
       {210, posix_file_type::regular | 0644, 3, 1337, 0, 0, 0, 8201, 8202,
        8203}},
  };

  static std::map<std::string, std::string> linkmap{
      {"somelink", "somedir/ipsum.py"},
      {"somedir/bad", "../foo"},
  };

  auto m = std::make_shared<os_access_mock>();

  for (auto const& kv : statmap) {
    const auto& stat = kv.second;

    switch (stat.type()) {
    case posix_file_type::regular:
      m->add(kv.first, stat, [size = stat.size] { return loremipsum(size); });
      break;
    case posix_file_type::symlink:
      m->add(kv.first, stat, linkmap.at(kv.first));
      break;
    default:
      m->add(kv.first, stat);
      break;
    }
  }

  return m;
}

void os_access_mock::add(fs::path const& path, simplestat const& st) {
  add_internal(path, st, std::monostate{});
}

void os_access_mock::add(fs::path const& path, simplestat const& st,
                         std::string const& contents) {
  add_internal(path, st, contents);
}

void os_access_mock::add(fs::path const& path, simplestat const& st,
                         std::function<std::string()> generator) {
  add_internal(path, st, generator);
}

void os_access_mock::add_dir(fs::path const& path) {
  simplestat st;
  std::memset(&st, 0, sizeof(st));
  st.ino = ino_++;
  st.mode = posix_file_type::directory | 0755;
  st.uid = 1000;
  st.gid = 100;
  add(path, st);
}

void os_access_mock::add_file(fs::path const& path, size_t size, bool random) {
  simplestat st;
  std::memset(&st, 0, sizeof(st));
  st.ino = ino_++;
  st.mode = posix_file_type::regular | 0644;
  st.uid = 1000;
  st.gid = 100;
  st.size = size;

  if (random) {
    thread_local std::mt19937_64 rng{42};

    std::uniform_int_distribution<> choice_dist{0, 3};

    switch (choice_dist(rng)) {
    default:
      break;

    case 0:
      add(path, st,
          [size, seed = rng()] { return create_random_string(size, seed); });
      return;
    }
  }

  add(path, st, [size] { return loremipsum(size); });
}

void os_access_mock::add_file(fs::path const& path,
                              std::string const& contents) {
  simplestat st;
  std::memset(&st, 0, sizeof(st));
  st.ino = ino_++;
  st.mode = posix_file_type::regular | 0644;
  st.uid = 1000;
  st.gid = 100;
  st.size = contents.size();
  add(path, st, contents);
}

void os_access_mock::add_local_files(fs::path const& base_path) {
  for (auto const& p : fs::recursive_directory_iterator(base_path)) {
    if (p.is_directory()) {
      add_dir(fs::relative(p.path(), base_path));
    } else if (p.is_regular_file()) {
      auto relpath = fs::relative(p.path(), base_path);
      simplestat st;
      std::memset(&st, 0, sizeof(st));
      st.ino = ino_++;
      st.mode = posix_file_type::regular | 0644;
      st.uid = 1000;
      st.gid = 100;
      st.size = p.file_size();
      add(relpath, st, [path = p.path().string()] {
        std::string rv;
        if (!folly::readFile(path.c_str(), rv)) {
          throw std::runtime_error(fmt::format("failed to read file {}", path));
        }
        return rv;
      });
    }
  }
}

void os_access_mock::set_access_fail(fs::path const& path) {
  access_fail_set_.emplace(path);
}

void os_access_mock::set_map_file_error(std::filesystem::path const& path,
                                        std::exception_ptr ep,
                                        int after_n_attempts) {
  auto& e = map_file_errors_[path];
  e.ep = std::move(ep);
  e.remaining_successful_attempts = after_n_attempts;
}

size_t os_access_mock::size() const { return root_ ? root_->size() : 0; }

std::vector<std::string> os_access_mock::splitpath(fs::path const& path) {
  std::vector<std::string> parts;
  for (auto const& p : path) {
    parts.emplace_back(u8string_to_string(p.u8string()));
  }
  while (!parts.empty() && (parts.front().empty() ||
                            (parts.front() == "/" || parts.front() == "\\"))) {
    parts.erase(parts.begin());
  }
  return parts;
}

auto os_access_mock::find(fs::path const& path) const -> mock_dirent* {
  return find(splitpath(path));
}

auto os_access_mock::find(std::vector<std::string> parts) const
    -> mock_dirent* {
  assert(root_);
  auto* de = root_.get();
  while (!parts.empty()) {
    if (de->status.type() != posix_file_type::directory) {
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

void os_access_mock::add_internal(fs::path const& path, simplestat const& st,
                                  value_variant_type var) {
  auto parts = splitpath(path);

  if (st.type() == posix_file_type::directory &&
      std::holds_alternative<std::monostate>(var)) {
    var = std::make_unique<mock_directory>();
  }

  if (parts.empty()) {
    assert(!root_);
    assert(st.type() == posix_file_type::directory);
    assert(std::holds_alternative<std::unique_ptr<mock_directory>>(var));
    root_ = std::make_unique<mock_dirent>();
    root_->status = st;
    root_->v = std::move(var);
  } else {
    auto name = parts.back();
    parts.pop_back();
    auto* de = find(std::move(parts));
    assert(de);
    de->add(name, st, std::move(var));
  }
}

std::unique_ptr<dir_reader>
os_access_mock::opendir(fs::path const& path) const {
  if (auto de = find(path);
      de && de->status.type() == posix_file_type::directory) {
    std::vector<fs::path> files;
    for (auto const& e :
         std::get<std::unique_ptr<mock_directory>>(de->v)->ent) {
      files.push_back(path / e.name);
    }
    return std::make_unique<dir_reader_mock>(std::move(files));
  }

  throw std::runtime_error(fmt::format("oops in opendir: {}", path.string()));
}

file_stat os_access_mock::symlink_info(fs::path const& path) const {
  if (auto de = find(path)) {
    return make_file_stat(de->status);
  }

  throw std::runtime_error(
      fmt::format("oops in symlink_info: {}", path.string()));
}

fs::path os_access_mock::read_symlink(fs::path const& path) const {
  if (auto de = find(path);
      de && de->status.type() == posix_file_type::symlink) {
    return std::get<std::string>(de->v);
  }

  throw std::runtime_error(
      fmt::format("oops in read_symlink: {}", path.string()));
}

std::unique_ptr<mmif>
os_access_mock::map_file(fs::path const& path, size_t size) const {
  if (auto de = find(path);
      de && de->status.type() == posix_file_type::regular) {
    if (auto it = map_file_errors_.find(path); it != map_file_errors_.end()) {
      int remaining = it->second.remaining_successful_attempts.load();
      while (!it->second.remaining_successful_attempts.compare_exchange_weak(
          remaining, remaining - 1)) {
      }
      if (remaining <= 0) {
        std::rethrow_exception(it->second.ep);
      }
    }

    return std::make_unique<mmap_mock>(
        std::visit(overloaded{
                       [this](std::string const& str) { return str; },
                       [this](std::function<std::string()> const& fun) {
                         return fun();
                       },
                       [this](auto const&) -> std::string {
                         throw std::runtime_error("oops in overloaded");
                       },
                   },
                   de->v),
        size);
  }

  throw std::runtime_error(fmt::format("oops in map_file: {}", path.string()));
}

std::set<std::filesystem::path> os_access_mock::get_failed_paths() const {
  std::set<std::filesystem::path> rv = access_fail_set_;
  for (auto const& [path, error] : map_file_errors_) {
    if (error.remaining_successful_attempts.load() < 0) {
      rv.emplace(path);
    }
  }
  return rv;
}

std::unique_ptr<mmif> os_access_mock::map_file(fs::path const& path) const {
  return map_file(path, std::numeric_limits<size_t>::max());
}

int os_access_mock::access(fs::path const& path, int) const {
  return access_fail_set_.count(path) ? -1 : 0;
}

std::filesystem::path
os_access_mock::canonical(std::filesystem::path const& path) const {
  return path;
}

std::filesystem::path os_access_mock::current_path() const {
  return root_->name;
}

void os_access_mock::setenv(std::string name, std::string value) {
  env_[std::move(name)] = std::move(value);
}

std::optional<std::string> os_access_mock::getenv(std::string_view name) const {
  if (auto it = env_.find(std::string(name)); it != env_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void os_access_mock::thread_set_affinity(std::thread::id tid,
                                         std::span<int const> cpus,
                                         std::error_code& /*ec*/) const {
  std::lock_guard<std::mutex> lock{mx_};
  set_affinity_calls.emplace_back(tid,
                                  std::vector<int>(cpus.begin(), cpus.end()));
}

std::chrono::nanoseconds
os_access_mock::thread_get_cpu_time(std::thread::id tid,
                                    std::error_code& ec) const {
  return real_os_->thread_get_cpu_time(tid, ec);
}

std::filesystem::path
os_access_mock::find_executable(std::filesystem::path const& name) const {
  if (executable_resolver_) {
    return executable_resolver_(name);
  }

  return real_os_->find_executable(name);
}

void os_access_mock::set_executable_resolver(
    executable_resolver_type resolver) {
  executable_resolver_ = std::move(resolver);
}

std::optional<fs::path> find_binary(std::string_view name) {
  os_access_generic os;

  if (auto path = os.find_executable(name); !path.empty()) {
    return path;
  }

  return std::nullopt;
}

std::vector<std::string> parse_args(std::string_view args) {
  std::vector<std::string> rv;
  folly::split(' ', args, rv);
  return rv;
}

std::string create_random_string(size_t size, uint8_t min, uint8_t max,
                                 std::mt19937_64& gen) {
  std::string rv;
  rv.resize(size);
  std::uniform_int_distribution<> byte_dist{min, max};
  std::generate(rv.begin(), rv.end(), [&] { return byte_dist(gen); });
  return rv;
}

std::string create_random_string(size_t size, std::mt19937_64& gen) {
  return create_random_string(size, 0, 255, gen);
}

std::string create_random_string(size_t size, size_t seed) {
  std::mt19937_64 tmprng{seed};
  return create_random_string(size, tmprng);
}

std::string fix_regex(std::string regex) {
#ifndef _WIN32
  regex = std::regex_replace(regex, std::regex("\\\\d"), "[0-9]");
#endif
  return regex;
}

} // namespace dwarfs::test

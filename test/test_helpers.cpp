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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <regex>

#ifdef _WIN32
#include <winerror.h>
#endif

#include <fmt/format.h>

#include <dwarfs/file_util.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

#include "loremipsum.h"
#include "lz_synthetic_generator.h"
#include "mmap_mock.h"
#include "test_helpers.h"

namespace dwarfs::test {

std::error_code const kMlockQuotaError =
#ifdef _WIN32
    {ERROR_WORKING_SET_QUOTA, std::system_category()}
#else
    std::make_error_code(std::errc::not_enough_memory)
#endif
;

namespace fs = std::filesystem;

namespace {

file_stat make_file_stat(simplestat const& ss) {
  file_stat rv;
  rv.set_dev(0);
  rv.set_ino(ss.ino);
  rv.set_nlink(ss.nlink);
  rv.set_mode(ss.mode);
  rv.set_uid(ss.uid);
  rv.set_gid(ss.gid);
  rv.set_rdev(ss.rdev);
  rv.set_size(ss.size);
  rv.set_allocated_size(ss.size);
  rv.set_blocks((ss.size + 511) / 512);
  rv.set_blksize(0);
  rv.set_atimespec(ss.atim.ts);
  rv.set_mtimespec(ss.mtim.ts);
  rv.set_ctimespec(ss.ctim.ts);
  return rv;
}

constexpr file_stat::uid_type kUid1{1000};
constexpr file_stat::uid_type kUid2{1337};
constexpr file_stat::uid_type kUid3{0};
constexpr file_stat::gid_type kGid1{100};
constexpr file_stat::gid_type kGid2{0};
constexpr file_stat::dev_type kDev1{0};
constexpr file_stat::dev_type kDev2{259};
constexpr file_stat::dev_type kDev3{261};

constexpr std::array<std::pair<std::string_view, test::simplestat>, 15>
    kTestEntries{{
        // clang-format off
    {"",                 {  1, posix_file_type::directory | 0777, 1, kUid1, kGid1,       0, kDev1,    1,    2,    3}},
    {"test.pl",          {  3, posix_file_type::regular   | 0644, 2, kUid1, kGid1,       0, kDev1, 1001, 1002, 1003}},
    {"somelink",         {  4, posix_file_type::symlink   | 0777, 1, kUid1, kGid1,      16, kDev1, 2001, 2002, 2003}},
    {"somedir",          {  5, posix_file_type::directory | 0777, 1, kUid1, kGid1,       0, kDev1, 3001, 3002, 3003}},
    {"foo.pl",           {  6, posix_file_type::regular   | 0600, 2, kUid2, kGid2,   23456, kDev1, 4001, 4002, 4003}},
    {"bar.pl",           {  6, posix_file_type::regular   | 0600, 2, kUid2, kGid2,   23456, kDev1, 4001, 4002, 4003}},
    {"baz.pl",           { 16, posix_file_type::regular   | 0600, 2, kUid2, kGid2,   23456, kDev1, 8001, 8002, 8003}},
    {"ipsum.txt",        {  7, posix_file_type::regular   | 0644, 1, kUid1, kGid1, 2000000, kDev1, 5001, 5002, 5003}},
    {"somedir/ipsum.py", {  9, posix_file_type::regular   | 0644, 1, kUid1, kGid1,   10000, kDev1, 6001, 6002, 6003}},
    {"somedir/bad",      { 10, posix_file_type::symlink   | 0777, 1, kUid1, kGid1,       6, kDev1, 7001, 7002, 7003}},
    {"somedir/pipe",     { 12, posix_file_type::fifo      | 0644, 1, kUid1, kGid1,       0, kDev1, 8001, 8002, 8003}},
    {"somedir/null",     { 13, posix_file_type::character | 0666, 1, kUid3, kGid2,       0, kDev2, 9001, 9002, 9003}},
    {"somedir/zero",     { 14, posix_file_type::character | 0666, 1, kUid3, kGid2,       0, kDev3, 4000010001, 4000020002, 4000030003}},
    {"somedir/empty",    {212, posix_file_type::regular   | 0644, 1, kUid1, kGid1,       0, kDev1, 8101, 8102, 8103}},
    {"empty",            {210, posix_file_type::regular   | 0644, 3, kUid2, kGid2,       0, kDev1, 8201, 8202, 8203}},
        // clang-format on
    }};

std::unordered_map<std::string_view, std::string_view> const kTestLinks{
    {"somelink", "somedir/ipsum.py"},
    {"somedir/bad", "../foo"},
};

} // namespace

struct os_access_mock::mock_dirent {
  std::string name;
  simplestat status;
  value_variant_type v;

  size_t size() const;

  mock_dirent* find(std::string const& n);

  void add(std::string const& n, simplestat const& st, value_variant_type var);
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

auto os_access_mock::mock_dirent::find(std::string const& n) -> mock_dirent* {
  return std::get<std::unique_ptr<mock_directory>>(v)->find(n);
}

void os_access_mock::mock_dirent::add(std::string const& n,
                                      simplestat const& st,
                                      value_variant_type var) {
  return std::get<std::unique_ptr<mock_directory>>(v)->add(n, st,
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
  explicit dir_reader_mock(std::vector<fs::path>&& files,
                           std::chrono::nanoseconds delay)
      : files_(files)
      , index_(0)
      , delay_{delay} {}

  bool read(fs::path& name) override {
    if (delay_ > std::chrono::nanoseconds::zero()) {
      std::this_thread::sleep_for(delay_);
    }

    if (index_ < files_.size()) {
      name = files_[index_++];
      return true;
    }

    return false;
  }

 private:
  std::vector<fs::path> files_;
  size_t index_;
  std::chrono::nanoseconds const delay_;
};

os_access_mock::os_access_mock()
    : real_os_{std::make_shared<os_access_generic>()} {}

os_access_mock::~os_access_mock() = default;

std::shared_ptr<os_access_mock> os_access_mock::create_test_instance() {
  auto m = std::make_shared<os_access_mock>();

  m->add_entries(kTestEntries, [&](std::string_view name) {
    return std::string(kTestLinks.at(name));
  });

  return m;
}

void os_access_mock::add_entries(
    std::span<std::pair<std::string_view, simplestat> const> entries,
    std::function<std::string(std::string_view)> link_resolver) {
  for (auto const& [name, stat] : entries) {
    switch (stat.type()) {
    case posix_file_type::regular:
      add(name, stat, [size = stat.size] { return loremipsum(size); });
      break;
    case posix_file_type::symlink:
      add(name, stat, link_resolver(name));
      break;
    default:
      add(name, stat);
      break;
    }
  }
}

void os_access_mock::add(fs::path const& path, simplestat const& st) {
  add_internal(path, st, std::monostate{});
}

void os_access_mock::add(fs::path const& path, simplestat const& st,
                         std::string const& contents) {
  add_internal(path, st, contents);
}

void os_access_mock::add(fs::path const& path, simplestat const& st,
                         test_file_data data) {
  add_internal(path, st, std::move(data));
}

void os_access_mock::add(fs::path const& path, simplestat const& st,
                         std::function<std::string()> generator) {
  add_internal(path, st, generator);
}

void os_access_mock::add_dir(fs::path const& path) {
  simplestat st;
  st.ino = ino_++;
  st.mode = posix_file_type::directory | 0755;
  st.uid = 1000;
  st.gid = 100;
  add(path, st);
}

simplestat os_access_mock::make_reg_file_stat(add_file_options const& opts) {
  simplestat st;
  if (auto const ino = opts.ino) {
    st.ino = *ino;
  } else {
    st.ino = ino_++;
  }
  st.nlink = opts.nlink.value_or(1);
  st.mode = posix_file_type::regular | 0644;
  st.uid = 1000;
  st.gid = 100;
  return st;
}

simplestat os_access_mock::add_file(fs::path const& path, file_size_t size,
                                    bool random, add_file_options const& opts) {
  auto st = make_reg_file_stat(opts);

  st.size = size;

  if (random) {
    thread_local std::mt19937_64 rng{42};

    std::uniform_int_distribution<> choice_dist{0, 4};
    auto choice = choice_dist(rng);

    switch (choice) {
    default:
      break;

    case 0:
      add(path, st,
          [size, seed = rng()] { return create_random_string(size, seed); });
      return st;

    case 1:
    case 2: {
      add(path, st, [size, seed = rng(), text_mode = choice == 1] {
        lz_params lzp{};
        lzp.text_mode = text_mode;
        lzp.seed = seed;
        lz_synthetic_generator gen{lzp};
        return gen.generate(size);
      });
      return st;
    }
    }
  }

  add(path, st, [size] { return loremipsum(size); });

  return st;
}

simplestat
os_access_mock::add_file(fs::path const& path, std::string const& contents,
                         add_file_options const& opts) {
  auto st = make_reg_file_stat(opts);
  st.size = contents.size();
  add(path, st, contents);
  return st;
}

simplestat os_access_mock::add_file(fs::path const& path, test_file_data data,
                                    add_file_options const& opts) {
  auto st = make_reg_file_stat(opts);
  st.size = data.size();
  add(path, st, data);
  return st;
}

void os_access_mock::add_local_files(fs::path const& base_path) {
  for (auto const& p : fs::recursive_directory_iterator(base_path)) {
    if (p.is_directory()) {
      add_dir(fs::relative(p.path(), base_path));
    } else if (p.is_regular_file()) {
      auto relpath = fs::relative(p.path(), base_path);
      simplestat st;
      st.ino = ino_++;
      st.mode = posix_file_type::regular | 0644;
      st.uid = 1000;
      st.gid = 100;
      st.size = p.file_size();
      add(relpath, st, [path = p.path().string()] {
        std::error_code ec;
        auto rv = read_file(path, ec);
        if (ec) {
          throw std::runtime_error(
              fmt::format("failed to read file {}: {}", path, ec.message()));
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

void os_access_mock::set_map_file_delay(std::filesystem::path const& path,
                                        std::chrono::nanoseconds delay) {
  map_file_delays_[path] = delay;
}

file_size_t os_access_mock::size() const { return root_ ? root_->size() : 0; }

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
      files.push_back(path / string_to_u8string(e.name));
    }
    return std::make_unique<dir_reader_mock>(std::move(files),
                                             dir_reader_delay_);
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

file_view os_access_mock::open_file(fs::path const& path) const {
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

    if (std::holds_alternative<test_file_data>(de->v)) {
      return make_mock_file_view(std::get<test_file_data>(de->v), path);
    }

    auto data = de->v | match{
                            [](std::string const& str) { return str; },
                            [](std::function<std::string()> const& fun) {
                              return fun();
                            },
                            [](auto const&) -> std::string {
                              throw std::runtime_error("oops in match");
                            },
                        };

    if (std::cmp_greater_equal(data.size(), map_file_delay_min_size_)) {
      if (auto it = map_file_delays_.find(path); it != map_file_delays_.end()) {
        std::this_thread::sleep_for(it->second);
      }
    }

    return make_mock_file_view(std::move(data), path);
  }

  throw std::runtime_error(fmt::format("oops in open_file: {}", path.string()));
}

readonly_memory_mapping os_access_mock::map_empty_readonly(size_t size) const {
  return real_os_->map_empty_readonly(size);
}

memory_mapping os_access_mock::map_empty(size_t size) const {
  return real_os_->map_empty(size);
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
  return split_to<std::vector<std::string>>(args, ' ');
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

std::vector<
    std::pair<std::string, std::unordered_map<std::string, std::string>>>
parse_mtree(std::string_view mtree) {
  std::vector<
      std::pair<std::string, std::unordered_map<std::string, std::string>>>
      rv;
  std::istringstream iss{std::string{mtree}};
  std::string line;

  while (std::getline(iss, line, '\n')) {
    if (line == "#mtree") {
      continue;
    }

    auto parts = split_to<std::vector<std::string>>(line, ' ');
    auto path = parts.front();
    parts.erase(parts.begin());

    std::unordered_map<std::string, std::string> attrs;

    for (auto const& p : parts) {
      auto pos = p.find('=');
      if (pos == std::string::npos) {
        throw std::runtime_error("unexpected mtree line: " + line);
      }
      attrs[p.substr(0, pos)] = p.substr(pos + 1);
    }

    rv.emplace_back(std::move(path), std::move(attrs));
  }

  return rv;
}

file_view make_real_file_view(std::filesystem::path const& path) {
  os_access_generic os;
  return os.open_file(path);
}

bool skip_slow_tests() {
  static bool skip = getenv_is_enabled("DWARFS_SKIP_SLOW_TESTS");
  return skip;
}

} // namespace dwarfs::test

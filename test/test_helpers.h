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

#pragma once

#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dwarfs/entry_interface.h"
#include "dwarfs/file_access.h"
#include "dwarfs/file_stat.h"
#include "dwarfs/iolayer.h"
#include "dwarfs/os_access.h"
#include "dwarfs/script.h"
#include "dwarfs/terminal.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define DWARFS_TEST_RUNNING_ON_ASAN 1
#endif
#if __has_feature(address_sanitizer)
#define DWARFS_TEST_RUNNING_ON_TSAN 1
#endif
#endif

namespace dwarfs::test {

struct simplestat {
  file_stat::ino_type ino;
  file_stat::mode_type mode;
  file_stat::nlink_type nlink{1};
  file_stat::uid_type uid{0};
  file_stat::gid_type gid{0};
  file_stat::off_type size{0};
  file_stat::dev_type rdev{0};
  file_stat::time_type atime{0};
  file_stat::time_type mtime{0};
  file_stat::time_type ctime{0};

  posix_file_type::value type() const {
    return static_cast<posix_file_type::value>(mode & posix_file_type::mask);
  }
};

class os_access_mock : public os_access {
 private:
  struct mock_directory;
  struct mock_dirent;

 public:
  using value_variant_type =
      std::variant<std::monostate, std::string, std::function<std::string()>,
                   std::unique_ptr<mock_directory>>;

  using executable_resolver_type =
      std::function<std::filesystem::path(std::filesystem::path const&)>;

  os_access_mock();
  ~os_access_mock();

  static std::shared_ptr<os_access_mock> create_test_instance();

  size_t size() const;

  void add(std::filesystem::path const& path, simplestat const& st);
  void add(std::filesystem::path const& path, simplestat const& st,
           std::string const& contents);
  void add(std::filesystem::path const& path, simplestat const& st,
           std::function<std::string()> generator);

  void add_dir(std::filesystem::path const& path);
  void
  add_file(std::filesystem::path const& path, size_t size, bool random = false);
  void add_file(std::filesystem::path const& path, std::string const& contents);

  void add_local_files(std::filesystem::path const& path);

  void set_access_fail(std::filesystem::path const& path);
  void set_map_file_error(std::filesystem::path const& path,
                          std::exception_ptr ep, int after_n_attempts = 0);

  void setenv(std::string name, std::string value);

  std::unique_ptr<dir_reader>
  opendir(std::filesystem::path const& path) const override;

  file_stat symlink_info(std::filesystem::path const& path) const override;
  std::filesystem::path
  read_symlink(std::filesystem::path const& path) const override;

  std::unique_ptr<mmif>
  map_file(std::filesystem::path const& path) const override;
  std::unique_ptr<mmif>
  map_file(std::filesystem::path const& path, size_t size) const override;

  int access(std::filesystem::path const&, int) const override;

  std::filesystem::path
  canonical(std::filesystem::path const& path) const override;

  std::filesystem::path current_path() const override;

  std::optional<std::string> getenv(std::string_view name) const override;

  void thread_set_affinity(std::thread::id tid, std::span<int const> cpus,
                           std::error_code& ec) const override;

  std::chrono::nanoseconds
  thread_get_cpu_time(std::thread::id tid, std::error_code& ec) const override;

  std::filesystem::path
  find_executable(std::filesystem::path const& name) const override;

  void set_executable_resolver(executable_resolver_type resolver);

  std::set<std::filesystem::path> get_failed_paths() const;

  std::vector<
      std::tuple<std::thread::id, std::vector<int>>> mutable set_affinity_calls;

 private:
  struct error_info {
    std::exception_ptr ep{};
    std::atomic<int> mutable remaining_successful_attempts{0};
  };

  static std::vector<std::string> splitpath(std::filesystem::path const& path);
  struct mock_dirent* find(std::filesystem::path const& path) const;
  struct mock_dirent* find(std::vector<std::string> parts) const;
  void add_internal(std::filesystem::path const& path, simplestat const& st,
                    value_variant_type var);

  std::mutex mutable mx_;
  std::unique_ptr<mock_dirent> root_;
  size_t ino_{1000000};
  std::set<std::filesystem::path> access_fail_set_;
  std::map<std::filesystem::path, error_info> map_file_errors_;
  std::map<std::string, std::string> env_;
  std::shared_ptr<os_access> real_os_;
  executable_resolver_type executable_resolver_;
};

class script_mock : public script {
 public:
  bool has_configure() const override { return true; }
  bool has_filter() const override { return true; }
  bool has_transform() const override { return true; }
  bool has_order() const override { return true; }

  void configure(options_interface const& /*oi*/) override {}

  bool filter(entry_interface const& ei) override {
    filter_calls.push_back({ei.unix_dpath(), ei.name(), ei.size(),
                            ei.is_directory(), ei.get_permissions(),
                            ei.get_uid(), ei.get_gid(), ei.get_atime(),
                            ei.get_mtime(), ei.get_ctime()});
    return true;
  }

  void transform(entry_interface& ei) override {
    transform_calls.push_back({ei.unix_dpath(), ei.name(), ei.size(),
                               ei.is_directory(), ei.get_permissions(),
                               ei.get_uid(), ei.get_gid(), ei.get_atime(),
                               ei.get_mtime(), ei.get_ctime()});
  }

  void order(inode_vector& /*iv*/) override {
    // do nothing
  }

  struct entry_data {
    std::string path;
    std::string name;
    size_t size;
    bool is_directory;
    file_stat::mode_type mode;
    file_stat::uid_type uid;
    file_stat::gid_type gid;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
  };

  std::vector<entry_data> filter_calls;
  std::vector<entry_data> transform_calls;
};

class test_terminal : public terminal {
 public:
  test_terminal(std::ostream& out, std::ostream& err);

  void set_fancy(bool fancy) { fancy_ = fancy; }
  void set_is_tty(bool is_tty) { is_tty_ = is_tty; }
  void set_width(size_t width) { width_ = width; }

  size_t width() const override;
  bool is_tty(std::ostream& os) const override;
  bool is_fancy() const override;
  std::string_view color(termcolor color, termstyle style) const override;
  std::string colored(std::string text, termcolor color, bool enable,
                      termstyle style) const override;
  std::string_view carriage_return() const override;
  std::string_view rewind_line() const override;
  std::string_view clear_line() const override;

 private:
  std::ostream* out_;
  std::ostream* err_;
  bool fancy_{false};
  bool is_tty_{false};
  size_t width_{80};
};

class test_file_access : public file_access {
 public:
  bool exists(std::filesystem::path const& path) const override;

  std::unique_ptr<input_stream> open_input(std::filesystem::path const& path,
                                           std::error_code& ec) const override;
  std::unique_ptr<input_stream>
  open_input(std::filesystem::path const& path) const override;

  std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path,
                    std::error_code& ec) const override;
  std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path) const override;

  std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path,
              std::error_code& ec) const override;
  std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path) const override;

  std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path,
                     std::error_code& ec) const override;
  std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path) const override;

  void set_file(std::filesystem::path const& path, std::string contents) const;
  std::optional<std::string> get_file(std::filesystem::path const& path) const;

  void
  set_open_error(std::filesystem::path const& path, std::error_code ec) const;
  void
  set_close_error(std::filesystem::path const& path, std::error_code ec) const;

  std::optional<std::error_code>
  get_open_error(std::filesystem::path const& path) const;
  std::optional<std::error_code>
  get_close_error(std::filesystem::path const& path) const;

 private:
  std::map<std::filesystem::path, std::string> mutable files_;
  std::map<std::filesystem::path, std::error_code> mutable open_errors_;
  std::map<std::filesystem::path, std::error_code> mutable close_errors_;
};

class test_iolayer {
 public:
  test_iolayer();
  test_iolayer(std::shared_ptr<os_access const> os);
  test_iolayer(std::shared_ptr<os_access const> os,
               std::shared_ptr<file_access const> fa);
  ~test_iolayer();

  iolayer const& get();

  std::string out() const;
  std::string err() const;

  void use_real_terminal(bool use);
  void set_in(std::string in);
  void set_terminal_is_tty(bool is_tty);
  void set_terminal_fancy(bool fancy);
  void set_terminal_width(size_t width);

  void set_os_access(std::shared_ptr<os_access_mock> os);
  void set_file_access(std::shared_ptr<file_access const> fa);

  std::istream& in_stream() { return in_; }
  std::ostream& out_stream() { return out_; }
  std::ostream& err_stream() { return err_; }

 private:
  std::shared_ptr<os_access const> os_;
  std::shared_ptr<test_terminal> term_;
  std::shared_ptr<file_access const> fa_;
  std::istringstream in_;
  std::ostringstream out_;
  std::ostringstream err_;
  std::unique_ptr<iolayer> iol_;
  std::shared_ptr<terminal const> real_term_;
};

extern std::map<std::string, simplestat> statmap;

std::optional<std::filesystem::path> find_binary(std::string_view name);

std::span<std::pair<simplestat, std::string_view> const> test_dirtree();

std::vector<std::string> parse_args(std::string_view args);

std::string create_random_string(size_t size, uint8_t min, uint8_t max,
                                 std::mt19937_64& gen);
std::string create_random_string(size_t size, std::mt19937_64& gen);
std::string create_random_string(size_t size, size_t seed = 0);

std::string fix_regex(std::string regex);

} // namespace dwarfs::test

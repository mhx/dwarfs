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

#include <chrono>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#endif

#include <folly/FileUtil.h>
#include <folly/portability/Unistd.h>

#include <boost/asio/io_service.hpp>
#include <boost/process.hpp>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/experimental/TestUtil.h>
#include <folly/json.h>

#include <fmt/format.h>

#include "dwarfs/file_stat.h"
#include "dwarfs/xattr.h"

#include "test_helpers.h"

namespace {

namespace bp = boost::process;
namespace fs = std::filesystem;

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto test_data_dwarfs = test_dir / "data.dwarfs";
auto test_catdata_dwarfs = test_dir / "catdata.dwarfs";

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

auto tools_dir = fs::path(TOOLS_BIN_DIR).make_preferred();
auto mkdwarfs_bin = tools_dir / "mkdwarfs" EXE_EXT;
auto fuse3_bin = tools_dir / "dwarfs" EXE_EXT;
auto fuse2_bin = tools_dir / "dwarfs2" EXE_EXT;
auto dwarfsextract_bin = tools_dir / "dwarfsextract" EXE_EXT;
auto dwarfsck_bin = tools_dir / "dwarfsck" EXE_EXT;
auto universal_bin = tools_dir / "universal" / "dwarfs-universal" EXE_EXT;

class scoped_no_leak_check {
 public:
#ifdef DWARFS_TEST_RUNNING_ON_ASAN
  static constexpr auto const kEnvVar = "ASAN_OPTIONS";
  static constexpr auto const kNoLeakCheck = "detect_leaks=0";

  scoped_no_leak_check() {
    std::string new_asan_options;

    if (auto const* asan_options = ::getenv(kEnvVar)) {
      old_asan_options_.emplace(asan_options);
      new_asan_options = *old_asan_options_ + ":" + std::string(kNoLeakCheck);
    } else {
      new_asan_options.assign(kNoLeakCheck);
    }
    ::setenv(kEnvVar, new_asan_options.c_str(), 1);
    unset_asan_ = true;
  }

  ~scoped_no_leak_check() {
    if (unset_asan_) {
      if (old_asan_options_) {
        ::setenv(kEnvVar, old_asan_options_->c_str(), 1);
      } else {
        ::unsetenv(kEnvVar);
      }
    }
  }

 private:
  std::optional<std::string> old_asan_options_;
  bool unset_asan_{false};
#else
  // suppress unused variable warning
  ~scoped_no_leak_check() {}
#endif
};

#if !(defined(_WIN32) || defined(__APPLE__))
pid_t get_dwarfs_pid(fs::path const& path) {
  return folly::to<pid_t>(dwarfs::getxattr(path, "user.dwarfs.driver.pid"));
}
#endif

bool wait_until_file_ready(fs::path const& path,
                           std::chrono::milliseconds timeout) {
  auto end = std::chrono::steady_clock::now() + timeout;
  std::error_code ec;
  while (!fs::exists(path, ec)) {
#ifdef _WIN32
    if (ec && ec.value() != ERROR_OPERATION_ABORTED) {
#else
    if (ec) {
#endif
      std::cerr << "*** exists: " << ec.message() << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
  }
  return true;
}

bool read_file(fs::path const& path, std::string& out) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return false;
  }
  std::stringstream tmp;
  tmp << ifs.rdbuf();
  out = tmp.str();
  return true;
}

bool read_file(fs::path const& path, std::string& out, std::error_code& ec) {
  auto res = folly::readFile(path.string().c_str(), out);
  if (!res) {
#ifdef _WIN32
    ec = std::error_code(::GetLastError(), std::system_category());
#else
    ec = std::error_code(errno, std::generic_category());
#endif
  }
  return res;
}

struct compare_directories_result {
  std::set<fs::path> mismatched;
  std::set<fs::path> directories;
  std::set<fs::path> symlinks;
  std::set<fs::path> regular_files;
  size_t total_regular_file_size{0};
};

std::ostream&
operator<<(std::ostream& os, compare_directories_result const& cdr) {
  for (auto const& m : cdr.mismatched) {
    os << "*** mismatched: " << m << "\n";
  }
  for (auto const& m : cdr.regular_files) {
    os << "*** regular: " << m << "\n";
  }
  for (auto const& m : cdr.directories) {
    os << "*** directory: " << m << "\n";
  }
  for (auto const& m : cdr.symlinks) {
    os << "*** symlink: " << m << "\n";
  }
  return os;
}

template <typename T>
void find_all(fs::path const& root, T const& func) {
  std::deque<fs::path> q;
  q.push_back(root);
  while (!q.empty()) {
    auto p = q.front();
    q.pop_front();

    for (auto const& e : fs::directory_iterator(p)) {
      func(e);
      if (e.symlink_status().type() == fs::file_type::directory) {
        q.push_back(e.path());
      }
    }
  }
}

bool compare_directories(fs::path const& p1, fs::path const& p2,
                         compare_directories_result* res = nullptr) {
  std::map<fs::path, fs::directory_entry> m1, m2;
  std::set<fs::path> s1, s2;

  find_all(p1, [&](auto const& e) {
    auto rp = e.path().lexically_relative(p1);
    m1.emplace(rp, e);
    s1.insert(rp);
  });

  find_all(p2, [&](auto const& e) {
    auto rp = e.path().lexically_relative(p2);
    m2.emplace(rp, e);
    s2.insert(rp);
  });

  if (res) {
    res->mismatched.clear();
    res->directories.clear();
    res->symlinks.clear();
    res->regular_files.clear();
    res->total_regular_file_size = 0;
  }

  bool rv = true;

  std::set<fs::path> common;
  std::set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(),
                        std::inserter(common, common.end()));

  if (s1.size() != common.size() || s2.size() != common.size()) {
    if (res) {
      std::set_symmetric_difference(
          s1.begin(), s1.end(), s2.begin(), s2.end(),
          std::inserter(res->mismatched, res->mismatched.end()));
    }
    rv = false;
  }

  for (auto const& p : common) {
    auto const& e1 = m1[p];
    auto const& e2 = m2[p];

    if (e1.symlink_status().type() != e2.symlink_status().type() ||
        (e1.symlink_status().type() != fs::file_type::directory &&
         e1.file_size() != e2.file_size())) {
      if (res) {
        res->mismatched.insert(p);
      }
      rv = false;
      continue;
    }

    switch (e1.symlink_status().type()) {
    case fs::file_type::regular: {
      std::string c1, c2;
      if (!read_file(e1.path(), c1) || !read_file(e2.path(), c2) || c1 != c2) {
        if (res) {
          res->mismatched.insert(p);
        }
        rv = false;
      }
    }
      if (res) {
        res->regular_files.insert(p);
        res->total_regular_file_size += e1.file_size();
      }
      break;

    case fs::file_type::directory:
      if (res) {
        res->directories.insert(p);
      }
      break;

    case fs::file_type::symlink:
      if (fs::read_symlink(e1.path()) != fs::read_symlink(e2.path())) {
        if (res) {
          res->mismatched.insert(p);
        }
        rv = false;
      }
      if (res) {
        res->symlinks.insert(p);
      }
      break;

    default:
      break;
    }
  }

  return rv;
}

#ifdef _WIN32
struct new_process_group : public ::boost::process::detail::handler_base {
  template <class WindowsExecutor>
  void on_setup(WindowsExecutor& e [[maybe_unused]]) const {
    e.creation_flags |= CREATE_NEW_PROCESS_GROUP;
  }
};
#endif

void ignore_sigpipe() {
#ifdef __APPLE__
  static bool already_ignoring{false};

  if (!already_ignoring) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    int res = ::sigaction(SIGPIPE, &sa, NULL);
    if (res != 0) {
      std::cerr << "sigaction(SIGPIPE, SIG_IGN): " << std::strerror(errno)
                << "\n";
      std::abort();
    }
    already_ignoring = true;
  }
#endif
}

template <typename T>
concept subprocess_arg = std::convertible_to<T, std::string> ||
                         std::convertible_to<T, std::vector<std::string>> ||
                         std::convertible_to<T, fs::path>;

class subprocess {
 public:
  template <subprocess_arg... Args>
  subprocess(std::filesystem::path const& prog, Args&&... args)
      : prog_{prog} {
    (append_arg(cmdline_, std::forward<Args>(args)), ...);

    ignore_sigpipe();

    try {
      // std::cerr << "running: " << cmdline() << "\n";
      c_ = bp::child(prog.string(), bp::args(cmdline_), bp::std_in.close(),
                     bp::std_out > out_, bp::std_err > err_, ios_
#ifdef _WIN32
                     ,
                     new_process_group()
#endif
      );
    } catch (...) {
      std::cerr << "failed to create subprocess: " << cmdline() << "\n";
      throw;
    }
  }

  ~subprocess() {
    if (pt_) {
      std::cerr << "subprocess still running in destructor: " << cmdline()
                << "\n";
      pt_->join();
    }
  }

  std::string cmdline() const {
    std::string cmd = prog_.string();
    if (!cmdline_.empty()) {
      cmd += ' ';
      cmd += folly::join(' ', cmdline_);
    }
    return cmd;
  }

  void run() {
    ios_.run();
    c_.wait();
    outs_ = out_.get();
    errs_ = err_.get();
  }

  void run_background() {
    if (pt_) {
      throw std::runtime_error("already running in background");
    }
    pt_ = std::make_unique<std::thread>([this] { run(); });
  }

  void wait() {
    if (!pt_) {
      throw std::runtime_error("no process running in background");
    }
    pt_->join();
    pt_.reset();
  }

  void interrupt() {
    std::cerr << "interrupting: " << cmdline() << "\n";
#ifdef _WIN32
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid());
#else
    if (auto rv = ::kill(pid(), SIGINT); rv != 0) {
      std::cerr << "kill(" << pid() << ", SIGINT) = " << rv << "\n";
    }
#endif
  }

  std::string const& out() const { return outs_; }

  std::string const& err() const { return errs_; }

  bp::pid_t pid() const { return c_.id(); }

  int exit_code() const { return c_.exit_code(); }

  template <typename... Args>
  static std::tuple<std::string, std::string, int> run(Args&&... args) {
    auto p = subprocess(std::forward<Args>(args)...);
    p.run();
    return {p.out(), p.err(), p.exit_code()};
  }

  template <typename... Args>
  static std::optional<std::string> check_run(Args&&... args) {
    auto const [out, err, ec] = run(std::forward<Args>(args)...);

    if (ec != 0) {
      std::cerr << "stdout:\n" << out << "\nstderr:\n" << err << "\n";
      return std::nullopt;
    }

    return out;
  }

 private:
  static void append_arg(std::vector<std::string>& args, fs::path const& arg) {
    args.emplace_back(arg.string());
  }

  static void append_arg(std::vector<std::string>& args,
                         std::vector<std::string> const& more) {
    args.insert(args.end(), more.begin(), more.end());
  }

  template <std::convertible_to<std::string> T>
  static void append_arg(std::vector<std::string>& args, T const& arg) {
    args.emplace_back(arg);
  }

  bp::child c_;
  boost::asio::io_service ios_;
  std::future<std::string> out_;
  std::future<std::string> err_;
  std::string outs_;
  std::string errs_;
  std::unique_ptr<std::thread> pt_;
  std::filesystem::path const prog_;
  std::vector<std::string> cmdline_;
};

#if !(defined(_WIN32) || defined(__APPLE__))
class process_guard {
 public:
  process_guard() = default;

  explicit process_guard(pid_t pid)
      : pid_{pid} {
    auto proc_dir = fs::path("/proc") / folly::to<std::string>(pid);
    proc_dir_fd_ = ::open(proc_dir.c_str(), O_DIRECTORY);

    if (proc_dir_fd_ < 0) {
      throw std::runtime_error("could not open " + proc_dir.string());
    }
  }

  ~process_guard() {
    if (proc_dir_fd_ >= 0) {
      ::close(proc_dir_fd_);
    }
  }

  bool check_exit(std::chrono::milliseconds timeout) {
    auto end = std::chrono::steady_clock::now() + timeout;
    while (::faccessat(proc_dir_fd_, "fd", F_OK, 0) == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (std::chrono::steady_clock::now() >= end) {
        ::kill(pid_, SIGTERM);
        return false;
      }
    }
    return true;
  }

 private:
  pid_t pid_{-1};
  int proc_dir_fd_{-1};
};
#endif

class driver_runner {
 public:
  struct foreground_t {};
  static constexpr foreground_t foreground = foreground_t();

  driver_runner() = default;

  template <typename... Args>
  driver_runner(fs::path const& driver, bool tool_arg, fs::path const& image,
                fs::path const& mountpoint, Args&&... args)
      : mountpoint_{mountpoint} {
    setup_mountpoint(mountpoint);
#ifdef _WIN32
    process_ =
        std::make_unique<subprocess>(driver, make_tool_arg(tool_arg), image,
                                     mountpoint, std::forward<Args>(args)...);
    process_->run_background();

    wait_until_file_ready(mountpoint, std::chrono::seconds(5));
#else
    std::vector<std::string> options;
    if (!subprocess::check_run(driver, make_tool_arg(tool_arg), image,
                               mountpoint, options,
                               std::forward<Args>(args)...)) {
      throw std::runtime_error("error running " + driver.string());
    }
#ifdef __APPLE__
    wait_until_file_ready(mountpoint, std::chrono::seconds(5));
#else
    dwarfs_guard_ = process_guard(get_dwarfs_pid(mountpoint));
#endif
#endif
  }

  template <typename... Args>
  driver_runner(foreground_t, fs::path const& driver, bool tool_arg,
                fs::path const& image, fs::path const& mountpoint,
                Args&&... args)
      : mountpoint_{mountpoint} {
    setup_mountpoint(mountpoint);
    process_ = std::make_unique<subprocess>(driver, make_tool_arg(tool_arg),
                                            image, mountpoint,
#ifndef _WIN32
                                            "-f",
#endif
                                            std::forward<Args>(args)...);
    process_->run_background();
#if !(defined(_WIN32) || defined(__APPLE__))
    dwarfs_guard_ = process_guard(process_->pid());
#endif
  }

  bool unmount() {
#ifdef _WIN32
    static constexpr int const kSigIntExitCode{-1073741510};
#elif !defined(__APPLE__)
    static constexpr int const kSigIntExitCode{SIGINT};
#endif

    if (!mountpoint_.empty()) {
#ifdef __APPLE__
      auto umount = dwarfs::test::find_binary("umount");
      if (!umount) {
        throw std::runtime_error("no umount binary found");
      }
      auto t0 = std::chrono::steady_clock::now();
      for (;;) {
        auto [out, err, ec] = subprocess::run(umount.value(), mountpoint_);
        if (ec == 0) {
          break;
        }
        std::cerr << "driver failed to unmount:\nout:\n"
                  << out << "err:\n"
                  << err << "exit code: " << ec << "\n";
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(5)) {
          throw std::runtime_error(
              "driver still failed to unmount after 5 seconds");
        }
        std::cerr << "retrying...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      bool rv{true};
      if (process_) {
        process_->wait();
        auto ec = process_->exit_code();
        if (ec != 0) {
          std::cerr << "driver failed to unmount:\nout:\n"
                    << process_->out() << "err:\n"
                    << process_->err() << "exit code: " << ec << "\n";
          rv = false;
        }
      }
      process_.reset();
      mountpoint_.clear();
      return rv;
#else
#ifndef _WIN32
      if (process_) {
#endif
        process_->interrupt();
        process_->wait();
        auto ec = process_->exit_code();
        bool is_expected_exit_code = ec == 0 || ec == kSigIntExitCode;
        if (!is_expected_exit_code) {
          std::cerr << "driver failed to unmount:\nout:\n"
                    << process_->out() << "err:\n"
                    << process_->err() << "exit code: " << ec << "\n";
        }
        process_.reset();
        mountpoint_.clear();
        return is_expected_exit_code;
#ifndef _WIN32
      } else {
        auto fusermount = find_fusermount();
        for (int i = 0; i < 5; ++i) {
          if (subprocess::check_run(fusermount, "-u", mountpoint_)) {
            break;
          }
          std::cerr << "retrying fusermount...\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        mountpoint_.clear();
        return dwarfs_guard_.check_exit(std::chrono::seconds(5));
      }
#endif
#endif
    }
    return false;
  }

  std::string cmdline() const {
    std::string rv;
    if (process_) {
      rv = process_->cmdline();
    }
    return rv;
  }

  ~driver_runner() {
    if (!mountpoint_.empty()) {
      if (!unmount()) {
        ::abort();
      }
    }
  }

  static std::vector<std::string> make_tool_arg(bool tool_arg) {
    std::vector<std::string> rv;
    if (tool_arg) {
      rv.push_back("--tool=dwarfs");
    }
    return rv;
  }

 private:
#if !(defined(_WIN32) || defined(__APPLE__))
  static fs::path find_fusermount() {
    auto fusermount_bin = dwarfs::test::find_binary("fusermount");
    if (!fusermount_bin) {
      fusermount_bin = dwarfs::test::find_binary("fusermount3");
    }
    if (!fusermount_bin) {
      throw std::runtime_error("no fusermount binary found");
    }
    return *fusermount_bin;
  }
#endif

  static void setup_mountpoint(fs::path const& mp) {
    if (fs::exists(mp)) {
      fs::remove(mp);
    }
#ifndef _WIN32
    fs::create_directory(mp);
#endif
  }

  fs::path mountpoint_;
  std::unique_ptr<subprocess> process_;
#if !(defined(_WIN32) || defined(__APPLE__))
  process_guard dwarfs_guard_;
#endif
};

bool check_readonly(fs::path const& p, bool readonly) {
  auto st = fs::status(p);
  bool is_writable =
      (st.permissions() & fs::perms::owner_write) != fs::perms::none;

  if (is_writable == readonly) {
    std::cerr << "readonly=" << readonly << ", st_mode="
              << fmt::format("{0:o}", uint16_t(st.permissions())) << "\n";
    return false;
  }

#ifndef _WIN32
  {
    auto r = ::access(p.string().c_str(), W_OK);

    if (readonly) {
      if (r != -1 || errno != EACCES) {
        std::cerr << "access(" << p << ", W_OK) = " << r << " (errno=" << errno
                  << ") [readonly]\n";
        return false;
      }
    } else {
      if (r != 0) {
        std::cerr << "access(" << p << ", W_OK) = " << r << "\n";
        return false;
      }
    }
  }
#endif

  return true;
}

size_t num_hardlinks(fs::path const& p) {
#ifdef _WIN32
  auto stat = dwarfs::make_file_stat(p);
  return stat.nlink;
#else
  return fs::hard_link_count(p);
#endif
}

enum class binary_mode {
  standalone,
  universal_tool,
  universal_symlink,
};

std::ostream& operator<<(std::ostream& os, binary_mode m) {
  switch (m) {
  case binary_mode::standalone:
    os << "standalone";
    break;
  case binary_mode::universal_tool:
    os << "universal-tool";
    break;
  case binary_mode::universal_symlink:
    os << "universal-symlink";
    break;
  }
  return os;
}

std::vector<binary_mode> tools_test_modes{
    binary_mode::standalone,
#ifdef DWARFS_HAVE_UNIVERSAL_BINARY
    binary_mode::universal_tool,
    binary_mode::universal_symlink,
#endif
};

class tools_test : public ::testing::TestWithParam<binary_mode> {};

} // namespace

TEST_P(tools_test, end_to_end) {
  auto mode = GetParam();

  std::chrono::seconds const timeout{5};
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto image = td / "test.dwarfs";
  auto image_hdr = td / "test_hdr.dwarfs";
  auto fsdata_dir = td / "fsdata";
  auto header_data = fsdata_dir / "format.sh";
  auto universal_symlink_dwarfs_bin = td / "dwarfs" EXE_EXT;
  auto universal_symlink_mkdwarfs_bin = td / "mkdwarfs" EXE_EXT;
  auto universal_symlink_dwarfsck_bin = td / "dwarfsck" EXE_EXT;
  auto universal_symlink_dwarfsextract_bin = td / "dwarfsextract" EXE_EXT;
  std::vector<std::string> dwarfs_tool_arg;
  std::vector<std::string> mkdwarfs_tool_arg;
  std::vector<std::string> dwarfsck_tool_arg;
  std::vector<std::string> dwarfsextract_tool_arg;
  fs::path const* mkdwarfs_test_bin = &mkdwarfs_bin;
  fs::path const* dwarfsck_test_bin = &dwarfsck_bin;
  fs::path const* dwarfsextract_test_bin = &dwarfsextract_bin;

  if (mode == binary_mode::universal_symlink) {
    fs::create_symlink(universal_bin, universal_symlink_dwarfs_bin);
    fs::create_symlink(universal_bin, universal_symlink_mkdwarfs_bin);
    fs::create_symlink(universal_bin, universal_symlink_dwarfsck_bin);
    fs::create_symlink(universal_bin, universal_symlink_dwarfsextract_bin);
  }

  if (mode == binary_mode::universal_tool) {
    mkdwarfs_test_bin = &universal_bin;
    dwarfsck_test_bin = &universal_bin;
    dwarfsextract_test_bin = &universal_bin;
    dwarfs_tool_arg.push_back("--tool=dwarfs");
    mkdwarfs_tool_arg.push_back("--tool=mkdwarfs");
    dwarfsck_tool_arg.push_back("--tool=dwarfsck");
    dwarfsextract_tool_arg.push_back("--tool=dwarfsextract");
  }

  {
    auto out = subprocess::check_run(*mkdwarfs_test_bin, mkdwarfs_tool_arg);
    ASSERT_TRUE(out);
    EXPECT_THAT(*out, ::testing::HasSubstr("Usage:"));
    EXPECT_THAT(*out, ::testing::HasSubstr("--long-help"));
  }

  if (mode == binary_mode::universal_tool) {
    auto out = subprocess::check_run(universal_bin);
    ASSERT_TRUE(out);
    EXPECT_THAT(*out, ::testing::HasSubstr("--tool="));
  }

  ASSERT_TRUE(fs::create_directory(fsdata_dir));
  ASSERT_TRUE(subprocess::check_run(*dwarfsextract_test_bin,
                                    dwarfsextract_tool_arg, "-i",
                                    test_data_dwarfs, "-o", fsdata_dir));

  EXPECT_EQ(num_hardlinks(fsdata_dir / "format.sh"), 3);
  EXPECT_TRUE(fs::is_symlink(fsdata_dir / "foobar"));
  EXPECT_EQ(fs::read_symlink(fsdata_dir / "foobar"), fs::path("foo") / "bar");

  auto unicode_symlink_name = u8"יוניקוד";
  auto unicode_symlink = fsdata_dir / unicode_symlink_name;
  auto unicode_symlink_target = fs::path("unicode") / u8"我爱你" / u8"☀️ Sun" /
                                u8"Γειά σας" / u8"مرحبًا" / u8"⚽️" /
                                u8"Карибського";
  std::string unicode_file_contents;

  EXPECT_TRUE(fs::is_symlink(unicode_symlink));
  EXPECT_EQ(fs::read_symlink(unicode_symlink), unicode_symlink_target);
  EXPECT_TRUE(read_file(unicode_symlink, unicode_file_contents));
  EXPECT_EQ(unicode_file_contents, "unicode\n");
  EXPECT_TRUE(
      read_file(fsdata_dir / unicode_symlink_target, unicode_file_contents));
  EXPECT_EQ(unicode_file_contents, "unicode\n");

  ASSERT_TRUE(subprocess::check_run(*mkdwarfs_test_bin, mkdwarfs_tool_arg, "-i",
                                    fsdata_dir, "-o", image, "--no-progress",
                                    "--no-history", "--no-create-timestamp"));

  ASSERT_TRUE(fs::exists(image));
  ASSERT_GT(fs::file_size(image), 1000);

  {
    auto out = subprocess::check_run(
        *mkdwarfs_test_bin, mkdwarfs_tool_arg, "-i", fsdata_dir, "-o", "-",
        "--no-progress", "--no-history", "--no-create-timestamp");
    ASSERT_TRUE(out);
    std::string ref;
    ASSERT_TRUE(read_file(image, ref));
    EXPECT_EQ(ref.size(), out->size());
    EXPECT_EQ(ref, *out);
  }

  ASSERT_TRUE(subprocess::check_run(
      *mkdwarfs_test_bin, mkdwarfs_tool_arg, "-i", image, "-o", image_hdr,
      "--no-progress", "--recompress=none", "--header", header_data));

  ASSERT_TRUE(fs::exists(image_hdr));
  ASSERT_GT(fs::file_size(image_hdr), 1000);

  auto mountpoint = td / "mnt";
  auto extracted = td / "extracted";
  auto untared = td / "untared";

  std::vector<fs::path> drivers;

  switch (mode) {
  case binary_mode::standalone:
    drivers.push_back(fuse3_bin);

    if (fs::exists(fuse2_bin)) {
      drivers.push_back(fuse2_bin);
    }
    break;

  case binary_mode::universal_tool:
    drivers.push_back(universal_bin);
    break;

  case binary_mode::universal_symlink:
    drivers.push_back(universal_symlink_dwarfs_bin);
    break;
  }

  std::vector<std::string> all_options{
      "-s",
#ifndef _WIN32
      "-oenable_nlink",
      "-oreadonly",
#endif
      "-omlock=try",
      "-ono_cache_image",
      "-ocache_files",
      "-otidy_strategy=time",
  };

  unicode_symlink = mountpoint / unicode_symlink_name;

  for (auto const& driver : drivers) {
    {
      scoped_no_leak_check no_leak_check;
      auto const [out, err, ec] =
          subprocess::run(driver, dwarfs_tool_arg, "--help");
      EXPECT_THAT(out, ::testing::HasSubstr("Usage:"));
    }

    {
      scoped_no_leak_check no_leak_check;
      std::vector<std::string> args;

#if DWARFS_PERFMON_ENABLED
      args.push_back("-operfmon=fuse+inode_reader_v2+block_cache");
#endif

      driver_runner runner(driver_runner::foreground, driver,
                           mode == binary_mode::universal_tool, image,
                           mountpoint, args);

      ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout))
          << runner.cmdline();
      compare_directories_result cdr;
      ASSERT_TRUE(compare_directories(fsdata_dir, mountpoint, &cdr))
          << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.regular_files.size(), 26)
          << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.directories.size(), 19) << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.symlinks.size(), 2) << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh")) << runner.cmdline();

      EXPECT_TRUE(fs::is_symlink(unicode_symlink)) << runner.cmdline();
      EXPECT_EQ(fs::read_symlink(unicode_symlink), unicode_symlink_target)
          << runner.cmdline();
      EXPECT_TRUE(read_file(unicode_symlink, unicode_file_contents))
          << runner.cmdline();
      EXPECT_EQ(unicode_file_contents, "unicode\n") << runner.cmdline();
      EXPECT_TRUE(
          read_file(mountpoint / unicode_symlink_target, unicode_file_contents))
          << runner.cmdline();
      EXPECT_EQ(unicode_file_contents, "unicode\n") << runner.cmdline();

#ifndef _WIN32
      {
        struct statfs stfs;
        ASSERT_EQ(0, ::statfs(mountpoint.c_str(), &stfs)) << runner.cmdline();
        EXPECT_EQ(stfs.f_files, 44) << runner.cmdline();
      }
#endif

      {
        static constexpr auto kInodeInfoXattr{"user.dwarfs.inodeinfo"};
        std::vector<std::pair<fs::path, std::vector<std::string>>> xattr_tests{
            {mountpoint,
             {"user.dwarfs.driver.pid", "user.dwarfs.driver.perfmon",
              kInodeInfoXattr}},
            {mountpoint / "format.sh", {kInodeInfoXattr}},
            {mountpoint / "empty", {kInodeInfoXattr}},
        };

        for (auto const& [path, ref] : xattr_tests) {
          EXPECT_EQ(dwarfs::listxattr(path), ref) << runner.cmdline();

          auto info = folly::parseJson(dwarfs::getxattr(path, kInodeInfoXattr));
          EXPECT_TRUE(info.count("uid"));
          EXPECT_TRUE(info.count("gid"));
          EXPECT_TRUE(info.count("mode"));
        }
      }

      EXPECT_TRUE(runner.unmount()) << runner.cmdline();
    }

    {
      auto const [out, err, ec] = subprocess::run(
          driver,
          driver_runner::make_tool_arg(mode == binary_mode::universal_tool),
          image_hdr, mountpoint);

      EXPECT_NE(0, ec) << driver << "\n"
                       << "stdout:\n"
                       << out << "\nstderr:\n"
                       << err;
    }

    unsigned const combinations = 1 << all_options.size();

    for (unsigned bitmask = 0; bitmask < combinations; ++bitmask) {
      std::vector<std::string> args;
#ifndef _WIN32
      bool enable_nlink{false};
      bool readonly{false};

      for (size_t i = 0; i < all_options.size(); ++i) {
        if ((1 << i) & bitmask) {
          auto const& opt = all_options[i];
          if (opt == "-oreadonly") {
            readonly = true;
          }
          if (opt == "-oenable_nlink") {
            enable_nlink = true;
          }
          args.push_back(opt);
        }
      }
#endif

      args.push_back("-otidy_interval=1s");
      args.push_back("-otidy_max_age=2s");

      {
        driver_runner runner(driver, mode == binary_mode::universal_tool, image,
                             mountpoint, args);

        ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout))
            << runner.cmdline();
        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar")) << runner.cmdline();
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar")
            << runner.cmdline();
        compare_directories_result cdr;
        ASSERT_TRUE(compare_directories(fsdata_dir, mountpoint, &cdr))
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.regular_files.size(), 26)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.directories.size(), 19)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.symlinks.size(), 2) << runner.cmdline() << ": " << cdr;
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(enable_nlink ? 3 : 1, num_hardlinks(mountpoint / "format.sh"))
            << runner.cmdline();
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly))
            << runner.cmdline();
#endif
      }

      args.push_back("-ooffset=auto");

      {
        driver_runner runner(driver, mode == binary_mode::universal_tool,
                             image_hdr, mountpoint, args);

        ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout))
            << runner.cmdline();
        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar")) << runner.cmdline();
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar")
            << runner.cmdline();
        compare_directories_result cdr;
        ASSERT_TRUE(compare_directories(fsdata_dir, mountpoint, &cdr))
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.regular_files.size(), 26)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.directories.size(), 19)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.symlinks.size(), 2) << runner.cmdline() << ": " << cdr;
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(enable_nlink ? 3 : 1, num_hardlinks(mountpoint / "format.sh"))
            << runner.cmdline();
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly))
            << runner.cmdline();
#endif
      }
    }
  }

  auto meta_export = td / "test.meta";

  ASSERT_TRUE(
      subprocess::check_run(*dwarfsck_test_bin, dwarfsck_tool_arg, image));
  ASSERT_TRUE(subprocess::check_run(*dwarfsck_test_bin, dwarfsck_tool_arg,
                                    image, "--check-integrity"));
  ASSERT_TRUE(subprocess::check_run(*dwarfsck_test_bin, dwarfsck_tool_arg,
                                    image, "--export-metadata", meta_export));

  {
    std::string header;

    EXPECT_TRUE(read_file(header_data, header));

    auto output = subprocess::check_run(*dwarfsck_test_bin, dwarfsck_tool_arg,
                                        image_hdr, "-H");

    ASSERT_TRUE(output);

    EXPECT_EQ(header, *output);
  }

  EXPECT_GT(fs::file_size(meta_export), 1000);

  ASSERT_TRUE(fs::create_directory(extracted));

  ASSERT_TRUE(subprocess::check_run(*dwarfsextract_test_bin,
                                    dwarfsextract_tool_arg, "-i", image, "-o",
                                    extracted));
  EXPECT_EQ(3, num_hardlinks(extracted / "format.sh"));
  EXPECT_TRUE(fs::is_symlink(extracted / "foobar"));
  EXPECT_EQ(fs::read_symlink(extracted / "foobar"), fs::path("foo") / "bar");
  compare_directories_result cdr;
  ASSERT_TRUE(compare_directories(fsdata_dir, extracted, &cdr)) << cdr;
  EXPECT_EQ(cdr.regular_files.size(), 26) << cdr;
  EXPECT_EQ(cdr.directories.size(), 19) << cdr;
  EXPECT_EQ(cdr.symlinks.size(), 2) << cdr;
}

#define EXPECT_EC_IMPL(ec, cat, val)                                           \
  EXPECT_TRUE(ec) << runner.cmdline();                                         \
  EXPECT_EQ(cat, (ec).category()) << runner.cmdline();                         \
  EXPECT_EQ(val, (ec).value()) << runner.cmdline() << ": " << (ec).message()

#ifdef _WIN32
#define EXPECT_EC_UNIX_MAC_WIN(ec, unix, mac, windows)                         \
  EXPECT_EC_IMPL(ec, std::system_category(), windows)
#elif defined(__APPLE__)
#define EXPECT_EC_UNIX_MAC_WIN(ec, unix, mac, windows)                         \
  EXPECT_EC_IMPL(ec, std::generic_category(), mac)
#else
#define EXPECT_EC_UNIX_MAC_WIN(ec, unix, mac, windows)                         \
  EXPECT_EC_IMPL(ec, std::generic_category(), unix)
#endif

#define EXPECT_EC_UNIX_WIN(ec, unix, windows)                                  \
  EXPECT_EC_UNIX_MAC_WIN(ec, unix, unix, windows)

TEST_P(tools_test, mutating_and_error_ops) {
  auto mode = GetParam();

  std::chrono::seconds const timeout{5};
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto mountpoint = td / "mnt";
  auto file = mountpoint / "bench.sh";
  auto empty_dir = mountpoint / "empty";
  auto non_empty_dir = mountpoint / "foo";
  auto name_inside_fs = mountpoint / "some_random_name";
  auto name_outside_fs = td / "some_random_name";
  auto universal_symlink_dwarfs_bin = td / "dwarfs" EXE_EXT;

  if (mode == binary_mode::universal_symlink) {
    fs::create_symlink(universal_bin, universal_symlink_dwarfs_bin);
  }

  std::vector<fs::path> drivers;

  switch (mode) {
  case binary_mode::standalone:
    drivers.push_back(fuse3_bin);

    if (fs::exists(fuse2_bin)) {
      drivers.push_back(fuse2_bin);
    }
    break;

  case binary_mode::universal_tool:
    drivers.push_back(universal_bin);
    break;

  case binary_mode::universal_symlink:
    drivers.push_back(universal_symlink_dwarfs_bin);
    break;
  }

  for (auto const& driver : drivers) {
    driver_runner runner(driver_runner::foreground, driver,
                         mode == binary_mode::universal_tool, test_data_dwarfs,
                         mountpoint);

    ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout))
        << runner.cmdline();

    // remove (unlink)

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(file, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_WIN(ec, ENOSYS, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(empty_dir, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_WIN(ec, ENOSYS, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(non_empty_dir, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_WIN(ec, ENOSYS, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      EXPECT_EQ(static_cast<std::uintmax_t>(-1),
                fs::remove_all(non_empty_dir, ec))
          << runner.cmdline();
      EXPECT_EC_UNIX_WIN(ec, ENOSYS, ERROR_ACCESS_DENIED);
    }

    // rename

    {
      std::error_code ec;
      fs::rename(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::rename(file, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, EXDEV, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::rename(empty_dir, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::rename(empty_dir, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, EXDEV, ERROR_ACCESS_DENIED);
    }

    // hard link

    {
      std::error_code ec;
      fs::create_hard_link(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::create_hard_link(file, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, EXDEV, ERROR_ACCESS_DENIED);
    }

    // symbolic link

    {
      std::error_code ec;
      fs::create_symlink(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::create_symlink(file, name_outside_fs, ec);
      EXPECT_FALSE(ec) << runner.cmdline(); // this actually works :)
      EXPECT_TRUE(fs::remove(name_outside_fs, ec)) << runner.cmdline();
    }

    {
      std::error_code ec;
      fs::create_directory_symlink(empty_dir, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    {
      std::error_code ec;
      fs::create_directory_symlink(empty_dir, name_outside_fs, ec);
      EXPECT_FALSE(ec) << runner.cmdline(); // this actually works :)
      EXPECT_TRUE(fs::remove(name_outside_fs, ec)) << runner.cmdline();
    }

    // truncate

    {
      std::error_code ec;
      fs::resize_file(file, 1, ec);
      EXPECT_EC_UNIX_WIN(ec, ENOSYS, ERROR_ACCESS_DENIED);
    }

    // create directory

    {
      std::error_code ec;
      fs::create_directory(name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, ENOSYS, EACCES, ERROR_ACCESS_DENIED);
    }

    // read directory as file (non-mutating)

    {
      std::error_code ec;
      std::string tmp;
      EXPECT_FALSE(read_file(mountpoint / "empty", tmp, ec));
      EXPECT_EC_UNIX_WIN(ec, EISDIR, ERROR_ACCESS_DENIED);
    }

    // open file as directory (non-mutating)

    {
      std::error_code ec;
      fs::directory_iterator it{mountpoint / "format.sh", ec};
      EXPECT_EC_UNIX_WIN(ec, ENOTDIR, ERROR_DIRECTORY);
    }

    // try open non-existing symlink

    {
      std::error_code ec;
      auto tmp = fs::read_symlink(mountpoint / "doesnotexist", ec);
      EXPECT_EC_UNIX_WIN(ec, ENOENT, ERROR_FILE_NOT_FOUND);
    }

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
}

TEST_P(tools_test, categorize) {
  auto mode = GetParam();

  std::chrono::seconds const timeout{5};
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto image = td / "test.dwarfs";
  auto image_recompressed = td / "test2.dwarfs";
  auto fsdata_dir = td / "fsdata";
  auto universal_symlink_dwarfs_bin = td / "dwarfs" EXE_EXT;
  auto universal_symlink_mkdwarfs_bin = td / "mkdwarfs" EXE_EXT;
  auto universal_symlink_dwarfsck_bin = td / "dwarfsck" EXE_EXT;
  auto universal_symlink_dwarfsextract_bin = td / "dwarfsextract" EXE_EXT;
  std::vector<std::string> mkdwarfs_tool_arg;
  std::vector<std::string> dwarfsck_tool_arg;
  std::vector<std::string> dwarfsextract_tool_arg;
  fs::path const* mkdwarfs_test_bin = &mkdwarfs_bin;
  fs::path const* dwarfsck_test_bin = &dwarfsck_bin;
  fs::path const* dwarfsextract_test_bin = &dwarfsextract_bin;

  if (mode == binary_mode::universal_symlink) {
    fs::create_symlink(universal_bin, universal_symlink_dwarfs_bin);
    fs::create_symlink(universal_bin, universal_symlink_mkdwarfs_bin);
    fs::create_symlink(universal_bin, universal_symlink_dwarfsck_bin);
    fs::create_symlink(universal_bin, universal_symlink_dwarfsextract_bin);
  }

  if (mode == binary_mode::universal_tool) {
    mkdwarfs_test_bin = &universal_bin;
    dwarfsck_test_bin = &universal_bin;
    dwarfsextract_test_bin = &universal_bin;
    mkdwarfs_tool_arg.push_back("--tool=mkdwarfs");
    dwarfsck_tool_arg.push_back("--tool=dwarfsck");
    dwarfsextract_tool_arg.push_back("--tool=dwarfsextract");
  }

  ASSERT_TRUE(fs::create_directory(fsdata_dir));
  ASSERT_TRUE(subprocess::check_run(*dwarfsextract_test_bin,
                                    dwarfsextract_tool_arg, "-i",
                                    test_catdata_dwarfs, "-o", fsdata_dir));

  ASSERT_TRUE(fs::exists(fsdata_dir / "random"));
  EXPECT_EQ(4096, fs::file_size(fsdata_dir / "random"));

  std::vector<std::string> mkdwarfs_args{"-i",
                                         fsdata_dir.string(),
                                         "-o",
                                         image.string(),
                                         "--no-progress",
                                         "--categorize",
                                         "-S",
                                         "16",
                                         "-W",
                                         "pcmaudio/waveform::8"};

  ASSERT_TRUE(subprocess::check_run(*mkdwarfs_test_bin, mkdwarfs_tool_arg,
                                    mkdwarfs_args));

  ASSERT_TRUE(fs::exists(image));
  auto const image_size = fs::file_size(image);
  EXPECT_GT(image_size, 150000);
  EXPECT_LT(image_size, 300000);

  std::vector<std::string> mkdwarfs_args_recompress{
      "-i",
      image.string(),
      "-o",
      image_recompressed.string(),
      "--no-progress",
      "--recompress=block",
      "--recompress-categories=pcmaudio/waveform",
      "-C",
#ifdef DWARFS_HAVE_FLAC
      "pcmaudio/waveform::flac:level=8"
#else
      "pcmaudio/waveform::zstd:level=19"
#endif
  };

  ASSERT_TRUE(subprocess::check_run(*mkdwarfs_test_bin, mkdwarfs_tool_arg,
                                    mkdwarfs_args_recompress));

  ASSERT_TRUE(fs::exists(image_recompressed));
  {
    auto const image_size_recompressed = fs::file_size(image_recompressed);
    EXPECT_GT(image_size_recompressed, 100000);
    EXPECT_LT(image_size_recompressed, image_size);
  }

  {
    auto mountpoint = td / "mnt";
    fs::path driver;

    switch (mode) {
    case binary_mode::standalone:
      driver = fuse3_bin;
      break;

    case binary_mode::universal_tool:
      driver = universal_bin;
      break;

    case binary_mode::universal_symlink:
      driver = universal_symlink_dwarfs_bin;
      break;
    }

    driver_runner runner(driver_runner::foreground, driver,
                         mode == binary_mode::universal_tool, image,
                         mountpoint);

    ASSERT_TRUE(wait_until_file_ready(mountpoint / "random", timeout))
        << runner.cmdline();
    compare_directories_result cdr;
    ASSERT_TRUE(compare_directories(fsdata_dir, mountpoint, &cdr))
        << runner.cmdline() << ": " << cdr;
    EXPECT_EQ(cdr.regular_files.size(), 151) << runner.cmdline() << ": " << cdr;
    EXPECT_EQ(cdr.total_regular_file_size, 56'741'701)
        << runner.cmdline() << ": " << cdr;

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }

  auto json_info = subprocess::check_run(*dwarfsck_test_bin, dwarfsck_tool_arg,
                                         image_recompressed, "--json");
  ASSERT_TRUE(json_info);

  auto info = folly::parseJson(*json_info);

  EXPECT_EQ(info["block_size"], 65'536);
  EXPECT_EQ(info["image_offset"], 0);
  EXPECT_EQ(info["inode_count"], 154);
  EXPECT_EQ(info["original_filesystem_size"], 56'741'701);
  EXPECT_EQ(info["categories"].size(), 4);

  EXPECT_TRUE(info.count("created_by"));
  EXPECT_TRUE(info.count("created_on"));

  {
    auto it = info["categories"].find("<default>");
    ASSERT_NE(it, info["categories"].items().end());
    EXPECT_EQ(it->second["block_count"].asInt(), 1);
  }

  {
    auto it = info["categories"].find("incompressible");
    ASSERT_NE(it, info["categories"].items().end());
    EXPECT_EQ(it->second["block_count"].asInt(), 1);
    EXPECT_EQ(it->second["compressed_size"].asInt(), 4'096);
    EXPECT_EQ(it->second["uncompressed_size"].asInt(), 4'096);
  }

  {
    auto it = info["categories"].find("pcmaudio/metadata");
    ASSERT_NE(it, info["categories"].items().end());
    EXPECT_EQ(it->second["block_count"].asInt(), 3);
  }

  {
    auto it = info["categories"].find("pcmaudio/waveform");
    ASSERT_NE(it, info["categories"].items().end());
    EXPECT_EQ(it->second["block_count"].asInt(), 48);
  }

  ASSERT_EQ(info["history"].size(), 2);
  for (auto const& entry : info["history"]) {
    ASSERT_TRUE(entry.count("arguments"));
    EXPECT_TRUE(entry.count("compiler_id"));
    EXPECT_TRUE(entry.count("libdwarfs_version"));
    EXPECT_TRUE(entry.count("system_id"));
    EXPECT_TRUE(entry.count("timestamp"));
  }

  folly::dynamic ref_args = folly::dynamic::array();
  ref_args.push_back(mkdwarfs_test_bin->string());
  ref_args.insert(ref_args.end(), mkdwarfs_args.begin(), mkdwarfs_args.end());
  EXPECT_EQ(ref_args, info["history"][0]["arguments"]);

  ref_args.resize(1);
  ref_args.insert(ref_args.end(), mkdwarfs_args_recompress.begin(),
                  mkdwarfs_args_recompress.end());
  EXPECT_EQ(ref_args, info["history"][1]["arguments"]);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, tools_test,
                         ::testing::ValuesIn(tools_test_modes));

#ifdef DWARFS_BUILTIN_MANPAGE
class manpage_test
    : public ::testing::TestWithParam<std::tuple<binary_mode, std::string>> {};

TEST_P(manpage_test, manpage) {
  auto [mode, tool] = GetParam();

  std::map<std::string, fs::path> tools{
      {"dwarfs", fuse3_bin},
      {"mkdwarfs", mkdwarfs_bin},
      {"dwarfsck", dwarfsck_bin},
      {"dwarfsextract", dwarfsextract_bin},
  };

  std::vector<std::string> args;
  fs::path const* test_bin{nullptr};

  if (mode == binary_mode::universal_tool) {
    test_bin = &universal_bin;
    args.push_back("--tool=" + tool);
  } else {
    test_bin = &tools.at(tool);
  }

  scoped_no_leak_check no_leak_check;

  auto out = subprocess::check_run(*test_bin, args, "--man");

  ASSERT_TRUE(out);
  EXPECT_GT(out->size(), 1000) << *out;
  EXPECT_THAT(*out, ::testing::HasSubstr(tool));
  EXPECT_THAT(*out, ::testing::HasSubstr("SYNOPSIS"));
  EXPECT_THAT(*out, ::testing::HasSubstr("DESCRIPTION"));
  EXPECT_THAT(*out, ::testing::HasSubstr("AUTHOR"));
  EXPECT_THAT(*out, ::testing::HasSubstr("COPYRIGHT"));
}

namespace {

std::vector<binary_mode> manpage_test_modes{
    binary_mode::standalone,
#ifdef DWARFS_HAVE_UNIVERSAL_BINARY
    binary_mode::universal_tool,
#endif
};

} // namespace

INSTANTIATE_TEST_SUITE_P(
    dwarfs, manpage_test,
    ::testing::Combine(::testing::ValuesIn(manpage_test_modes),
                       ::testing::Values("dwarfs", "mkdwarfs", "dwarfsck",
                                         "dwarfsextract")));
#endif

TEST(tools_test, dwarfsextract_progress) {
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto tarfile = td / "output.tar";

  auto out =
      subprocess::check_run(dwarfsextract_bin, "-i", test_catdata_dwarfs, "-o",
                            tarfile, "-f", "gnutar", "--stdout-progress");
  ASSERT_TRUE(out);
  EXPECT_TRUE(fs::exists(tarfile));

  EXPECT_GT(out->size(), 100) << *out;
#ifdef _WIN32
  EXPECT_THAT(*out, ::testing::EndsWith("100%\r\n"));
#else
  EXPECT_THAT(*out, ::testing::EndsWith("100%\n"));
  EXPECT_THAT(*out, ::testing::MatchesRegex("^\r([0-9][0-9]*%\r)*100%\n"));
#endif
}

TEST(tools_test, dwarfsextract_stdout) {
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());

  auto out = subprocess::check_run(dwarfsextract_bin, "-i", test_catdata_dwarfs,
                                   "-f", "mtree");
  ASSERT_TRUE(out);

  EXPECT_GT(out->size(), 1000) << *out;
  EXPECT_THAT(*out, ::testing::StartsWith("#mtree\n"));
  EXPECT_THAT(*out, ::testing::HasSubstr("type=file"));
}

TEST(tools_test, dwarfsextract_file_out) {
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto outfile = td / "output.mtree";

  auto out = subprocess::check_run(dwarfsextract_bin, "-i", test_catdata_dwarfs,
                                   "-f", "mtree", "-o", outfile);
  ASSERT_TRUE(out);
  EXPECT_TRUE(out->empty());

  ASSERT_TRUE(fs::exists(outfile));

  std::string mtree;
  ASSERT_TRUE(read_file(outfile, mtree));

  EXPECT_GT(mtree.size(), 1000) << *out;
  EXPECT_THAT(mtree, ::testing::StartsWith("#mtree\n"));
  EXPECT_THAT(mtree, ::testing::HasSubstr("type=file"));
}

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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <concepts>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <sys/mount.h>
#elif defined(__FreeBSD__)
#include <sys/mount.h>
#include <sys/param.h>
#else
#include <sys/vfs.h>
#endif
#endif

#include <boost/asio/io_context.hpp>
#if __has_include(<boost/process/v1/args.hpp>)
#define BOOST_PROCESS_VERSION 1
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process.hpp>
#endif

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <nlohmann/json.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/config.h>
#include <dwarfs/conv.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_util.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>
#include <dwarfs/xattr.h>

#include "compare_directories.h"
#include "loremipsum.h"
#include "sparse_file_builder.h"
#include "test_helpers.h"

namespace {

namespace bp = boost::process;
namespace fs = std::filesystem;

using namespace std::chrono_literals;
using namespace std::string_view_literals;
using namespace dwarfs::binary_literals;

using dwarfs::test::compare_directories;

#ifdef DWARFS_WITH_FUSE_DRIVER
#ifdef __linux__
auto constexpr kFuseTimeout{10s};
#else
auto constexpr kFuseTimeout{30s};
#endif
#endif

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto test_data_dwarfs = test_dir / "data.dwarfs";
auto test_catdata_dwarfs = test_dir / "catdata.dwarfs";

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

#ifndef MKDWARFS_BINARY
#define MKDWARFS_BINARY tools_dir / "mkdwarfs" EXE_EXT
#endif

#ifndef DWARFSCK_BINARY
#define DWARFSCK_BINARY tools_dir / "dwarfsck" EXE_EXT
#endif

#ifndef DWARFSEXTRACT_BINARY
#define DWARFSEXTRACT_BINARY tools_dir / "dwarfsextract" EXE_EXT
#endif

#ifdef DWARFS_CROSSCOMPILING_EMULATOR
#define DWARFS_ARG_EMULATOR_ DWARFS_CROSSCOMPILING_EMULATOR,
#else
#define DWARFS_ARG_EMULATOR_
#endif

auto tools_dir = fs::path(TOOLS_BIN_DIR).make_preferred();
auto mkdwarfs_bin = fs::path{MKDWARFS_BINARY};
auto fuse3_bin = tools_dir / "dwarfs" EXE_EXT;
auto fuse2_bin = tools_dir / "dwarfs2" EXE_EXT;
auto dwarfsextract_bin = fs::path{DWARFSEXTRACT_BINARY};
auto dwarfsck_bin = fs::path{DWARFSCK_BINARY};
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

#ifdef DWARFS_WITH_FUSE_DRIVER

bool skip_fuse_tests() {
  return dwarfs::getenv_is_enabled("DWARFS_SKIP_FUSE_TESTS");
}

#ifndef _WIN32
pid_t get_dwarfs_pid(fs::path const& path) {
  return dwarfs::to<pid_t>(dwarfs::getxattr(path, "user.dwarfs.driver.pid"));
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
    std::this_thread::sleep_for(1ms);
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
  }
  return true;
}

#endif

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

#ifdef DWARFS_WITH_FUSE_DRIVER
bool read_lines(fs::path const& path, std::vector<std::string>& out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  std::string line;
  while (std::getline(ifs, line)) {
    out.push_back(line);
  }
  return true;
}
#endif

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
      : subprocess(nullptr, prog, std::forward<Args>(args)...) {}

  template <subprocess_arg... Args>
  subprocess(boost::asio::io_context& ios, std::filesystem::path const& prog,
             Args&&... args)
      : subprocess(&ios, prog, std::forward<Args>(args)...) {}

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
      cmd += fmt::format(" {}", fmt::join(cmdline_, " "));
    }
    return cmd;
  }

  void run() {
    if (!ios_) {
      throw std::runtime_error("processes with external io_context must be run "
                               "externally and then waited for");
    }
    ios_->run();
    wait();
  }

  void wait() {
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

  void wait_background() {
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
  template <subprocess_arg... Args>
  subprocess(boost::asio::io_context* ios, std::filesystem::path const& prog,
             Args&&... args)
      : prog_{prog} {
    (append_arg(cmdline_, std::forward<Args>(args)), ...);

    ignore_sigpipe();

    if (!ios) {
      ios_ = std::make_unique<boost::asio::io_context>();
      ios = ios_.get();
    }

    try {
      // std::cerr << "running: " << cmdline() << "\n";
      c_ = bp::child(prog_.string(), bp::args(cmdline_), bp::std_in.close(),
                     bp::std_out > out_, bp::std_err > err_, *ios
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
  std::unique_ptr<boost::asio::io_context> ios_;
  std::future<std::string> out_;
  std::future<std::string> err_;
  std::string outs_;
  std::string errs_;
  std::unique_ptr<std::thread> pt_;
  std::filesystem::path const prog_;
  std::vector<std::string> cmdline_;
};

#ifndef _WIN32
namespace fs_guard_detail {

// TODO: can be replaced with boost::scope::unique_fd (Boost 1.85+)
struct unique_fd {
  int fd{-1};
  unique_fd() = default;
  explicit unique_fd(int f)
      : fd{f} {}
  unique_fd(unique_fd const&) = delete;
  unique_fd& operator=(unique_fd const&) = delete;
  unique_fd(unique_fd&& o) noexcept
      : fd{o.fd} {
    o.fd = -1;
  }
  unique_fd& operator=(unique_fd&& o) noexcept {
    if (this != &o) {
      if (fd >= 0) {
        ::close(fd);
      }
      fd = o.fd;
      o.fd = -1;
    }
    return *this;
  }
  ~unique_fd() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
  int get() const { return fd; }
  explicit operator bool() const { return fd >= 0; }
};

} // namespace fs_guard_detail

class process_guard {
 public:
  process_guard() = default;

  explicit process_guard(pid_t pid)
      : pid_{pid} {
#if defined(__FreeBSD__) || defined(__APPLE__)
    kq_ = fs_guard_detail::unique_fd(::kqueue());
    if (!kq_) {
      throw std::system_error(errno, std::generic_category(), "kqueue");
    }

    struct kevent kev{};
    EV_SET(&kev, static_cast<uintptr_t>(pid_), EVFILT_PROC, EV_ADD | EV_CLEAR,
           NOTE_EXIT, 0, nullptr);
    if (::kevent(kq_.get(), &kev, 1, nullptr, 0, nullptr) < 0) {
      // If the process is already gone, treat as non-existent.
      if (errno == ESRCH) {
        already_exited_ = true;
      } else {
        throw std::system_error(errno, std::generic_category(),
                                "kevent(EV_ADD)");
      }
    }
#else
    std::string proc_dir = "/proc/" + std::to_string(pid_);
    procfd_ = fs_guard_detail::unique_fd(
        ::open(proc_dir.c_str(), O_DIRECTORY | O_CLOEXEC));
    if (!procfd_) {
      throw std::system_error(errno, std::generic_category(),
                              "open(" + proc_dir + ")");
    }
#endif
  }

  bool check_exit(std::chrono::milliseconds timeout) {
#if defined(__FreeBSD__) || defined(__APPLE__)
    if (already_exited_) {
      return true;
    }

    struct kevent ev{};
    struct timespec ts{
        .tv_sec = static_cast<time_t>(timeout.count() / 1000),
        .tv_nsec = static_cast<long>((timeout.count() % 1000) * 1'000'000)};

    int n = ::kevent(kq_.get(), nullptr, 0, &ev, 1, &ts);
    if (n < 0) {
      // If PID vanished between registration and wait, consider it exited.
      if (errno == ESRCH) {
        return true;
      }
      // Other error: be conservative and signal timeout+terminate behavior
      // below.
      n = 0;
    }

    if (n == 0) {
      // timed out: gently ask it to stop
      ::kill(pid_, SIGTERM);
      return false;
    }

    // We got an event; NOTE_EXIT means it's gone.
    if (ev.filter == EVFILT_PROC && (ev.fflags & NOTE_EXIT)) {
      return true;
    }

    // Unexpected event; treat as not exited yet.
    return false;
#else
    // Poll for existence of a known child entry (like "fd") in /proc/<pid>.
    auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      int rc = ::faccessat(procfd_.get(), "fd", F_OK, 0);
      if (rc != 0) {
        // Directory no longer has 'fd' → process is gone.
        return true;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        ::kill(pid_, SIGTERM);
        return false;
      }
      std::this_thread::sleep_for(1ms);
    }
#endif
  }

 private:
  pid_t pid_{-1};
#if defined(__FreeBSD__) || defined(__APPLE__)
  fs_guard_detail::unique_fd kq_;
  bool already_exited_{false};
#else
  fs_guard_detail::unique_fd procfd_;
#endif
};
#endif

#ifdef DWARFS_WITH_FUSE_DRIVER

class driver_runner {
 public:
  struct foreground_t {};
  static constexpr foreground_t foreground{};

  struct automount_t {};
  static constexpr automount_t automount{};

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

    wait_until_file_ready(mountpoint, kFuseTimeout);
#else
    std::vector<std::string> options;
    if (!subprocess::check_run(DWARFS_ARG_EMULATOR_ driver,
                               make_tool_arg(tool_arg), image, mountpoint,
                               options, std::forward<Args>(args)...)) {
      throw std::runtime_error("error running " + driver.string());
    }
    dwarfs_guard_ = process_guard(get_dwarfs_pid(mountpoint));
#endif
  }

  template <typename... Args>
  driver_runner(foreground_t, fs::path const& driver, bool tool_arg,
                fs::path const& image, fs::path const& mountpoint,
                Args&&... args)
      : mountpoint_{mountpoint} {
    setup_mountpoint(mountpoint);
    process_ = std::make_unique<subprocess>(
        DWARFS_ARG_EMULATOR_ driver, make_tool_arg(tool_arg), image, mountpoint,
#ifndef _WIN32
        "-f",
#endif
        std::forward<Args>(args)...);
    process_->run_background();
#ifndef _WIN32
    dwarfs_guard_ = process_guard(process_->pid());
#endif
  }

  template <typename... Args>
  driver_runner(automount_t, fs::path const& driver, bool tool_arg,
                fs::path const& image, fs::path const& mountpoint,
                Args&&... args)
      : mountpoint_{mountpoint} {
    process_ = std::make_unique<subprocess>(DWARFS_ARG_EMULATOR_ driver,
                                            make_tool_arg(tool_arg),
                                            "--auto-mountpoint", image,
#ifndef _WIN32
                                            "-f",
#endif
                                            std::forward<Args>(args)...);
    process_->run_background();
#ifndef _WIN32
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
      auto diskutil = dwarfs::test::find_binary("diskutil");
      if (!diskutil) {
        throw std::runtime_error("no diskutil binary found");
      }
      auto t0 = std::chrono::steady_clock::now();
      for (;;) {
        auto [out, err, ec] =
            subprocess::run(diskutil.value(), "unmount", mountpoint_);
        if (ec == 0) {
          break;
        }
        std::cerr << "driver failed to unmount:\nout:\n"
                  << out << "err:\n"
                  << err << "exit code: " << ec << "\n";
        if (std::chrono::steady_clock::now() - t0 > kFuseTimeout) {
          throw std::runtime_error(
              "driver still failed to unmount after 5 seconds");
        }
        std::cerr << "retrying...\n";
        std::this_thread::sleep_for(10ms);
      }
      bool rv{true};
      if (process_) {
        process_->wait_background();
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
        process_->wait_background();
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
#ifdef __FreeBSD__
        auto umount = find_umount();
        for (int i = 0; i < 5; ++i) {
          if (subprocess::check_run(umount, mountpoint_)) {
            break;
          }
          std::cerr << "retrying umount...\n";
          std::this_thread::sleep_for(200ms);
        }
#else
        auto fusermount = find_fusermount();
        for (int i = 0; i < 5; ++i) {
          if (subprocess::check_run(fusermount, "-u", mountpoint_)) {
            break;
          }
          std::cerr << "retrying fusermount...\n";
          std::this_thread::sleep_for(200ms);
        }
#endif
        mountpoint_.clear();
        return dwarfs_guard_.check_exit(kFuseTimeout);
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
        std::abort();
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
#ifdef __FreeBSD__
  static fs::path find_umount() {
    auto umount_bin = dwarfs::test::find_binary("umount");
    if (!umount_bin) {
      throw std::runtime_error("no umount binary found");
    }
    return *umount_bin;
  }
#else
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
#ifndef _WIN32
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

  return true;
}

#endif

size_t num_hardlinks(fs::path const& p) {
#ifdef _WIN32
  dwarfs::file_stat stat(p);
  return stat.nlink();
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

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
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
    auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin,
                                     mkdwarfs_tool_arg);
    ASSERT_TRUE(out);
    EXPECT_THAT(*out, ::testing::HasSubstr("Usage:"));
    EXPECT_THAT(*out, ::testing::HasSubstr("--long-help"));
  }

  if (mode == binary_mode::universal_tool) {
    auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ universal_bin);
    ASSERT_TRUE(out);
    EXPECT_THAT(*out, ::testing::HasSubstr("--tool="));
  }

  ASSERT_TRUE(fs::create_directory(fsdata_dir));
  ASSERT_TRUE(subprocess::check_run(
      DWARFS_ARG_EMULATOR_ * dwarfsextract_test_bin, dwarfsextract_tool_arg,
      "-i", test_data_dwarfs, "-o", fsdata_dir));

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

  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin,
                                    mkdwarfs_tool_arg, "-i", fsdata_dir, "-o",
                                    image, "--no-progress", "--no-history",
                                    "--no-create-timestamp"));

  ASSERT_TRUE(fs::exists(image));
  ASSERT_GT(fs::file_size(image), 1000);

  {
    auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin,
                                     mkdwarfs_tool_arg, "-i", fsdata_dir, "-o",
                                     "-", "--no-progress", "--no-history",
                                     "--no-create-timestamp");
    ASSERT_TRUE(out);
    std::string ref;
    ASSERT_TRUE(read_file(image, ref));
    EXPECT_EQ(ref.size(), out->size());
    EXPECT_EQ(ref, *out);
  }

  ASSERT_TRUE(subprocess::check_run(
      DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin, mkdwarfs_tool_arg, "-i", image,
      "-o", image_hdr, "--no-progress", "--recompress=none", "--header",
      header_data));

  ASSERT_TRUE(fs::exists(image_hdr));
  ASSERT_GT(fs::file_size(image_hdr), 1000);

  auto mountpoint = td / "mnt";
  auto extracted = td / "extracted";
  auto untared = td / "untared";

#ifdef DWARFS_WITH_FUSE_DRIVER
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

  unicode_symlink = mountpoint / unicode_symlink_name;

  if (skip_fuse_tests()) {
    drivers.clear();
  }

  for (auto const& driver : drivers) {
    {
      scoped_no_leak_check no_leak_check;
      auto const [out, err, ec] = subprocess::run(DWARFS_ARG_EMULATOR_ driver,
                                                  dwarfs_tool_arg, "--help");
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

      ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", kFuseTimeout))
          << runner.cmdline();
      auto const cdr = compare_directories(fsdata_dir, mountpoint);
      ASSERT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.matching_regular_files.size(), 26)
          << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.matching_directories.size(), 19)
          << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.matching_symlinks.size(), 2)
          << runner.cmdline() << ": " << cdr;
#ifndef _WIN32
      // TODO: https://github.com/winfsp/winfsp/issues/511
      EXPECT_EQ(3, num_hardlinks(mountpoint / "format.sh")) << runner.cmdline();
#endif

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

          auto xattr = dwarfs::getxattr(path, kInodeInfoXattr);
          nlohmann::json info;
          EXPECT_NO_THROW(info = nlohmann::json::parse(xattr))
              << runner.cmdline() << ", " << xattr;
          EXPECT_TRUE(info.count("uid"));
          EXPECT_TRUE(info.count("gid"));
          EXPECT_TRUE(info.count("mode"));
        }

        auto perfmon =
            dwarfs::getxattr(mountpoint, "user.dwarfs.driver.perfmon");
#if DWARFS_PERFMON_ENABLED
        EXPECT_THAT(perfmon, ::testing::HasSubstr("[fuse.op_init]"));
        EXPECT_THAT(perfmon, ::testing::HasSubstr("p99 latency"));
#else
        EXPECT_THAT(perfmon,
                    ::testing::StartsWith("no performance monitor support"));
#endif

        EXPECT_THAT(
            [&] { dwarfs::getxattr(mountpoint, "user.something.nonexistent"); },
            ::testing::Throws<std::system_error>());

        std::error_code ec;
        dwarfs::getxattr(mountpoint, "user.something.nonexistent", ec);
        EXPECT_TRUE(ec);
#ifdef __APPLE__
        EXPECT_EQ(ec.value(), ENOATTR);
#elif defined(__FreeBSD__)
        EXPECT_EQ(ec.value(), ERANGE); // FIXME: this is weird...
#else
        EXPECT_EQ(ec.value(), ENODATA);
#endif
      }

      EXPECT_TRUE(runner.unmount()) << runner.cmdline();
    }

    {
      auto const [out, err, ec] = subprocess::run(
          DWARFS_ARG_EMULATOR_ driver,
          driver_runner::make_tool_arg(mode == binary_mode::universal_tool),
          image_hdr, mountpoint);

      EXPECT_NE(0, ec) << driver << "\n"
                       << "stdout:\n"
                       << out << "\nstderr:\n"
                       << err;
    }

    std::vector<std::string> all_options{
        "-s",
        "-ocase_insensitive,block_allocator=mmap",
#ifndef _WIN32
        "-opreload_all",
        "-oreadonly",
        "-ouid=2345,gid=3456",
#endif
    };

#ifndef __APPLE__
    // macFUSE is notoriously slow to start, so let's skip these tests
    if (!dwarfs::test::skip_slow_tests()) {
      all_options.push_back("-omlock=try");
      all_options.push_back("-otidy_strategy=time,cache_files");
    }
#endif

    unsigned const combinations = 1 << all_options.size();

    for (unsigned bitmask = 0; bitmask < combinations; ++bitmask) {
      std::vector<std::string> args;
      bool case_insensitive{false};
#ifndef _WIN32
      bool readonly{false};
      bool uid_gid_override{false};
#endif

      for (size_t i = 0; i < all_options.size(); ++i) {
        if ((1 << i) & bitmask) {
          auto const& opt = all_options[i];
          if (opt.find("-ocase_insensitive") != std::string::npos) {
            case_insensitive = true;
          }
#ifndef _WIN32
          if (opt.find("-oreadonly") != std::string::npos) {
            readonly = true;
          }
          if (opt.find("-ouid=") != std::string::npos) {
            uid_gid_override = true;
          }
#endif
          args.push_back(opt);
        }
      }

      args.push_back("-otidy_interval=1s");
      args.push_back("-otidy_max_age=2s");
      args.push_back("-odebuglevel=debug");

      {
        driver_runner runner(driver, mode == binary_mode::universal_tool, image,
                             mountpoint, args);

        ASSERT_TRUE(
            wait_until_file_ready(mountpoint / "format.sh", kFuseTimeout))
            << runner.cmdline();
        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar")) << runner.cmdline();
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar")
            << runner.cmdline();
        auto const cdr = compare_directories(fsdata_dir, mountpoint);
        ASSERT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_regular_files.size(), 26)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_directories.size(), 19)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_symlinks.size(), 2)
            << runner.cmdline() << ": " << cdr;
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(3, num_hardlinks(mountpoint / "format.sh"))
            << runner.cmdline();
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly))
            << runner.cmdline();
        if (uid_gid_override) {
          struct ::stat st;
          ASSERT_EQ(0, ::lstat(mountpoint.string().c_str(), &st))
              << runner.cmdline();
          EXPECT_EQ(st.st_uid, 2345) << runner.cmdline();
          EXPECT_EQ(st.st_gid, 3456) << runner.cmdline();
          ASSERT_EQ(0,
                    ::lstat((mountpoint / "format.sh").string().c_str(), &st))
              << runner.cmdline();
          EXPECT_EQ(st.st_uid, 2345) << runner.cmdline();
          EXPECT_EQ(st.st_gid, 3456) << runner.cmdline();
        }
#endif
        EXPECT_TRUE(fs::exists(mountpoint / "format.sh")) << runner.cmdline();
        EXPECT_EQ(case_insensitive, fs::exists(mountpoint / "FORMAT.SH"))
            << runner.cmdline();
        EXPECT_EQ(case_insensitive, fs::exists(mountpoint / "fOrMaT.Sh"))
            << runner.cmdline();

        auto perfmon =
            dwarfs::getxattr(mountpoint, "user.dwarfs.driver.perfmon");
#if DWARFS_PERFMON_ENABLED
        EXPECT_THAT(perfmon,
                    ::testing::StartsWith("performance monitor is disabled"));
#else
        EXPECT_THAT(perfmon,
                    ::testing::StartsWith("no performance monitor support"));
#endif
      }

      args.push_back("-ooffset=auto");

      {
        driver_runner runner(driver, mode == binary_mode::universal_tool,
                             image_hdr, mountpoint, args);

        ASSERT_TRUE(
            wait_until_file_ready(mountpoint / "format.sh", kFuseTimeout))
            << runner.cmdline();
        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar")) << runner.cmdline();
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar")
            << runner.cmdline();
        auto const cdr = compare_directories(fsdata_dir, mountpoint);
        ASSERT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_regular_files.size(), 26)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_directories.size(), 19)
            << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_symlinks.size(), 2)
            << runner.cmdline() << ": " << cdr;
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(3, num_hardlinks(mountpoint / "format.sh"))
            << runner.cmdline();
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly))
            << runner.cmdline();
#endif
      }
    }
  }
#endif

  auto meta_export = td / "test.meta";

  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * dwarfsck_test_bin,
                                    dwarfsck_tool_arg, image));
  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * dwarfsck_test_bin,
                                    dwarfsck_tool_arg, image,
                                    "--check-integrity"));
  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * dwarfsck_test_bin,
                                    dwarfsck_tool_arg, image,
                                    "--export-metadata", meta_export));

  {
    std::string header;

    EXPECT_TRUE(read_file(header_data, header));

    auto output =
        subprocess::check_run(DWARFS_ARG_EMULATOR_ * dwarfsck_test_bin,
                              dwarfsck_tool_arg, image_hdr, "-H");

    ASSERT_TRUE(output);

    EXPECT_EQ(header, *output);
  }

  EXPECT_GT(fs::file_size(meta_export), 1000);

  ASSERT_TRUE(fs::create_directory(extracted));

  ASSERT_TRUE(subprocess::check_run(
      DWARFS_ARG_EMULATOR_ * dwarfsextract_test_bin, dwarfsextract_tool_arg,
      "-i", image, "-o", extracted));
  EXPECT_EQ(3, num_hardlinks(extracted / "format.sh"));
  EXPECT_TRUE(fs::is_symlink(extracted / "foobar"));
  EXPECT_EQ(fs::read_symlink(extracted / "foobar"), fs::path("foo") / "bar");
  auto const cdr = compare_directories(fsdata_dir, extracted);
  ASSERT_TRUE(cdr.identical()) << cdr;
  EXPECT_EQ(cdr.matching_regular_files.size(), 26) << cdr;
  EXPECT_EQ(cdr.matching_directories.size(), 19) << cdr;
  EXPECT_EQ(cdr.matching_symlinks.size(), 2) << cdr;
}

#ifdef DWARFS_WITH_FUSE_DRIVER

#define EXPECT_EC_IMPL(ec, cat, val)                                           \
  EXPECT_TRUE(ec) << runner.cmdline();                                         \
  EXPECT_EQ(cat, (ec).category()) << runner.cmdline();                         \
  EXPECT_THAT((ec).value(), testing::AnyOf val)                                \
      << runner.cmdline() << ": " << (ec).message()

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

  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
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

    ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", kFuseTimeout))
        << runner.cmdline();

    // remove (unlink)

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(file, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(empty_dir, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      EXPECT_FALSE(fs::remove(non_empty_dir, ec)) << runner.cmdline();
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      EXPECT_EQ(static_cast<std::uintmax_t>(-1),
                fs::remove_all(non_empty_dir, ec))
          << runner.cmdline();
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    // rename

    {
      std::error_code ec;
      fs::rename(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      fs::rename(file, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, (EXDEV), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      fs::rename(empty_dir, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      fs::rename(empty_dir, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, (EXDEV), (ERROR_ACCESS_DENIED));
    }

    // hard link

    {
      std::error_code ec;
      fs::create_hard_link(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS, EPERM), (EACCES),
                             (ERROR_ACCESS_DENIED));
    }

    {
      std::error_code ec;
      fs::create_hard_link(file, name_outside_fs, ec);
      EXPECT_EC_UNIX_WIN(ec, (EXDEV), (ERROR_ACCESS_DENIED));
    }

    // symbolic link

    {
      std::error_code ec;
      fs::create_symlink(file, name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
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
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
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
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    // create directory

    {
      std::error_code ec;
      fs::create_directory(name_inside_fs, ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    // read directory as file (non-mutating)

    {
      std::error_code ec;
      auto tmp = dwarfs::read_file(mountpoint / "empty", ec);
      EXPECT_TRUE(ec);
      EXPECT_EC_UNIX_WIN(ec, (EISDIR), (ERROR_ACCESS_DENIED));
    }

    // open file as directory (non-mutating)

    {
      std::error_code ec;
      fs::directory_iterator it{mountpoint / "format.sh", ec};
      EXPECT_EC_UNIX_WIN(ec, (ENOTDIR), (ERROR_DIRECTORY));
    }

    // try open non-existing symlink

    {
      std::error_code ec;
      auto tmp = fs::read_symlink(mountpoint / "doesnotexist", ec);
      EXPECT_EC_UNIX_WIN(ec, (ENOENT), (ERROR_FILE_NOT_FOUND));
    }

    // Open non-existent file for writing
    {
      auto p = mountpoint / "nonexistent";
      EXPECT_FALSE(fs::exists(p));
      std::error_code ec;
      dwarfs::write_file(p, "hello", ec);
      EXPECT_TRUE(ec);
      EXPECT_EC_UNIX_MAC_WIN(ec, (ENOSYS), (EACCES), (ERROR_ACCESS_DENIED));
    }

    // Open existing file for writing
    {
      auto p = mountpoint / "format.sh";
      EXPECT_TRUE(fs::exists(p));
      std::error_code ec;
      dwarfs::write_file(p, "hello", ec);
      EXPECT_TRUE(ec);
      EXPECT_EC_UNIX_WIN(ec, (EACCES), (ERROR_ACCESS_DENIED));
    }

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
}

#endif

TEST_P(tools_test, categorize) {
  auto mode = GetParam();

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
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
  ASSERT_TRUE(subprocess::check_run(
      DWARFS_ARG_EMULATOR_ * dwarfsextract_test_bin, dwarfsextract_tool_arg,
      "-i", test_catdata_dwarfs, "-o", fsdata_dir));

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

  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin,
                                    mkdwarfs_tool_arg, mkdwarfs_args));

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

  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ * mkdwarfs_test_bin,
                                    mkdwarfs_tool_arg,
                                    mkdwarfs_args_recompress));

  ASSERT_TRUE(fs::exists(image_recompressed));
  {
    auto const image_size_recompressed = fs::file_size(image_recompressed);
    EXPECT_GT(image_size_recompressed, 100000);
    EXPECT_LT(image_size_recompressed, image_size);
  }

#ifdef DWARFS_WITH_FUSE_DRIVER

  if (!skip_fuse_tests()) {
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

    ASSERT_TRUE(wait_until_file_ready(mountpoint / "random", kFuseTimeout))
        << runner.cmdline();
    auto const cdr = compare_directories(fsdata_dir, mountpoint);
    ASSERT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 151)
        << runner.cmdline() << ": " << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 56'741'701)
        << runner.cmdline() << ": " << cdr;

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }

  if (!skip_fuse_tests()) {
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

    auto analysis_file = td / "analysis.dat";

    {
      scoped_no_leak_check no_leak_check;

      // TODO: WinFSP seems to mangle backslashes in driver options
      auto analysis_file_str = std::regex_replace(analysis_file.string(),
                                                  std::regex(R"(\\)"), R"(\\)");

      driver_runner runner(driver_runner::foreground, driver,
                           mode == binary_mode::universal_tool, image,
                           mountpoint, "-opreload_category=pcmaudio/waveform",
                           "-oanalysis_file=" + analysis_file_str);

      ASSERT_TRUE(wait_until_file_ready(mountpoint / "random", kFuseTimeout))
          << runner.cmdline();

      std::array const files_to_read{
          fs::path{"random"},
          fs::path{"audio"} / "test24-4.w64",
          fs::path{"pcmaudio"} / "test16.aiff",
          fs::path{"dwarfsextract.md"},
          fs::path{"audio"} / "test8-3.caf",
          fs::path{"random"},
          fs::path{"dwarfsextract.md"},
          fs::path{"audio"} / "test16-1.wav",
      };

      for (auto const& file : files_to_read) {
        std::string contents;
        EXPECT_TRUE(read_file(mountpoint / file, contents)) << runner.cmdline();
        EXPECT_GT(contents.size(), 0) << runner.cmdline();
      }

      EXPECT_TRUE(runner.unmount()) << runner.cmdline();
    }

    std::array const expected_files_accessed{
        fs::path{"random"},
        fs::path{"audio"} / "test24-4.w64",
        fs::path{"pcmaudio"} / "test16.aiff",
        fs::path{"dwarfsextract.md"},
        fs::path{"audio"} / "test8-3.caf",
        fs::path{"audio"} / "test16-1.wav",
    };

    ASSERT_TRUE(fs::exists(analysis_file));
    std::vector<std::string> analysis_results;
    ASSERT_TRUE(read_lines(analysis_file, analysis_results));
    std::vector<fs::path> files_accessed;
    for (auto const& line : analysis_results) {
      files_accessed.push_back(line);
    }

    EXPECT_THAT(files_accessed,
                ::testing::ElementsAreArray(expected_files_accessed));
  }

#endif

  auto json_info =
      subprocess::check_run(DWARFS_ARG_EMULATOR_ * dwarfsck_test_bin,
                            dwarfsck_tool_arg, image_recompressed, "--json");
  ASSERT_TRUE(json_info);

  nlohmann::json info;
  EXPECT_NO_THROW(info = nlohmann::json::parse(*json_info)) << *json_info;

  EXPECT_EQ(info["block_size"], 65'536);
  EXPECT_EQ(info["image_offset"], 0);
  EXPECT_EQ(info["inode_count"], 154);
  EXPECT_EQ(info["original_filesystem_size"], 56'741'701);
  EXPECT_EQ(info["categories"].size(), 4);

  EXPECT_TRUE(info.count("created_by"));
  EXPECT_TRUE(info.count("created_on"));

  {
    auto it = info["categories"].find("<default>");
    ASSERT_NE(it, info["categories"].end());
    EXPECT_EQ((*it)["block_count"].get<int>(), 1);
  }

  {
    auto it = info["categories"].find("incompressible");
    ASSERT_NE(it, info["categories"].end());
    EXPECT_EQ((*it)["block_count"].get<int>(), 1);
    EXPECT_EQ((*it)["compressed_size"].get<int>(), 4'096);
    EXPECT_EQ((*it)["uncompressed_size"].get<int>(), 4'096);
  }

  {
    auto it = info["categories"].find("pcmaudio/metadata");
    ASSERT_NE(it, info["categories"].end());
    EXPECT_EQ((*it)["block_count"].get<int>(), 3);
  }

  {
    auto it = info["categories"].find("pcmaudio/waveform");
    ASSERT_NE(it, info["categories"].end());
    EXPECT_EQ((*it)["block_count"].get<int>(), 48);
  }

  ASSERT_EQ(info["history"].size(), 2);
  for (auto const& entry : info["history"]) {
    ASSERT_TRUE(entry.count("arguments"));
    EXPECT_TRUE(entry.count("compiler_id"));
    EXPECT_TRUE(entry.count("libdwarfs_version"));
    EXPECT_TRUE(entry.count("system_id"));
    EXPECT_TRUE(entry.count("timestamp"));
  }

  {
    nlohmann::json ref_args;
    ref_args.push_back(mkdwarfs_test_bin->string());
    std::copy(mkdwarfs_args.begin(), mkdwarfs_args.end(),
              std::back_inserter(ref_args));
    EXPECT_EQ(ref_args, info["history"][0]["arguments"]);
  }

  {
    nlohmann::json ref_args;
    ref_args.push_back(mkdwarfs_test_bin->string());
    std::copy(mkdwarfs_args_recompress.begin(), mkdwarfs_args_recompress.end(),
              std::back_inserter(ref_args));
    EXPECT_EQ(ref_args, info["history"][1]["arguments"]);
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, tools_test,
                         ::testing::ValuesIn(tools_test_modes));

#ifdef DWARFS_BUILTIN_MANPAGE
class manpage_test
    : public ::testing::TestWithParam<std::tuple<binary_mode, std::string>> {};

std::vector<std::string> const manpage_test_tools{
    "mkdwarfs",
    "dwarfsck",
    "dwarfsextract",
#ifdef DWARFS_WITH_FUSE_DRIVER
    "dwarfs",
#endif
};

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

  auto out =
      subprocess::check_run(DWARFS_ARG_EMULATOR_ * test_bin, args, "--man");

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
                       ::testing::ValuesIn(manpage_test_tools)));
#endif

TEST(tools_test, dwarfsextract_progress) {
  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                                   test_catdata_dwarfs, "-o", td.string(),
                                   "--stdout-progress");
  EXPECT_TRUE(fs::exists(td / "pcmaudio" / "test12.aiff"));
#else
  auto tarfile = td / "output.tar";

  auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                                   test_catdata_dwarfs, "-o", tarfile, "-f",
                                   "gnutar", "--stdout-progress");
  EXPECT_TRUE(fs::exists(tarfile));
#endif

  ASSERT_TRUE(out);
  EXPECT_GT(out->size(), 100) << *out;
#ifdef _WIN32
  EXPECT_THAT(*out, ::testing::EndsWith("100%\r\n"));
#else
  EXPECT_THAT(*out, ::testing::EndsWith("100%\n"));
  EXPECT_THAT(*out, ::testing::MatchesRegex("^\r([0-9][0-9]*%\r)*100%\n"));
#endif
}

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
TEST(tools_test, dwarfsextract_stdout) {
  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();

  auto out = subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                                   test_catdata_dwarfs, "-f", "mtree");
  ASSERT_TRUE(out);

  EXPECT_GT(out->size(), 1000) << *out;
  EXPECT_THAT(*out, ::testing::StartsWith("#mtree\n"));
  EXPECT_THAT(*out, ::testing::HasSubstr("type=file"));
}

TEST(tools_test, dwarfsextract_file_out) {
  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
  auto outfile = td / "output.mtree";

  auto out =
      subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                            test_catdata_dwarfs, "-f", "mtree", "-o", outfile);
  ASSERT_TRUE(out);
  EXPECT_TRUE(out->empty()) << *out;

  ASSERT_TRUE(fs::exists(outfile));

  std::string mtree;
  ASSERT_TRUE(read_file(outfile, mtree));

  EXPECT_GT(mtree.size(), 1000) << *out;
  EXPECT_THAT(mtree, ::testing::StartsWith("#mtree\n"));
  EXPECT_THAT(mtree, ::testing::HasSubstr("type=file"));
}
#endif

#ifdef _WIN32
TEST(tools_test, mkdwarfs_invalid_utf8_filename) {
  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
  auto input = td / "input";

  ASSERT_TRUE(fs::create_directory(input));

  auto valid = input / "valid.txt";
  dwarfs::write_file(valid, "hello");

  auto invalid1 = input / L"invalid\xd800.txt";
  fs::copy_file(valid, invalid1);
  auto output1 = td / "test1.dwarfs";

  {
    auto [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ mkdwarfs_bin, "-i", input.string(),
                        "-o", output1.string());
    EXPECT_EQ(2, ec);
    EXPECT_THAT(err, ::testing::HasSubstr("storing as \"invalid\ufffd.txt\""));
  }

  auto invalid2 = input / L"invalid\xd801.txt";
  fs::copy_file(valid, invalid2);
  auto output2 = td / "test2.dwarfs";

  {
    auto [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ mkdwarfs_bin, "-i", input.string(),
                        "-o", output2.string());
    EXPECT_EQ(2, ec);
    EXPECT_THAT(err, ::testing::HasSubstr("storing as \"invalid\ufffd.txt\""));
    EXPECT_THAT(
        err,
        ::testing::HasSubstr(
            "cannot store \"invalid\ufffd.txt\" as the name already exists"));
  }

  auto ext1 = td / "ext1";
  ASSERT_TRUE(fs::create_directory(ext1));
  EXPECT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin,
                                    "-i", output1.string(), "-o",
                                    ext1.string()));
  EXPECT_TRUE(fs::exists(ext1 / "valid.txt"));
  EXPECT_TRUE(fs::exists(ext1 / L"invalid\ufffd.txt"));

  auto ext2 = td / "ext2";
  ASSERT_TRUE(fs::create_directory(ext2));
  EXPECT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin,
                                    "-i", output2.string(), "-o",
                                    ext2.string()));
  EXPECT_TRUE(fs::exists(ext2 / "valid.txt"));
  EXPECT_TRUE(fs::exists(ext2 / L"invalid\ufffd.txt"));
}
#endif

namespace {

enum class path_type {
  relative,
  absolute,
};

constexpr std::array path_types{path_type::relative, path_type::absolute};

std::ostream& operator<<(std::ostream& os, path_type m) {
  switch (m) {
  case path_type::relative:
    os << "relative";
    break;
  case path_type::absolute:
    os << "absolute";
    break;
  }
  return os;
}

} // namespace

class mkdwarfs_tool_input_list
    : public testing::TestWithParam<std::tuple<path_type, bool>> {};

TEST_P(mkdwarfs_tool_input_list, basic) {
  static constexpr std::string_view newline{
#ifdef _WIN32
      "\r\n"
#else
      "\n"
#endif
  };

  auto [type, explicit_input] = GetParam();

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
  auto input = td / "input";
  auto output = td / "test.dwarfs";

  auto cwd = fs::current_path();
  fs::current_path(td);
  dwarfs::scope_exit reset_cwd([&] { fs::current_path(cwd); });

  ASSERT_TRUE(fs::create_directory(input));

  ASSERT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin,
                                    "-i", test_data_dwarfs, "-o", input));

  std::ostringstream files;

  for (auto const& entry : fs::recursive_directory_iterator(input / "foo")) {
    if (entry.is_regular_file()) {
      auto const& p = entry.path();
      if (p.extension() == ".sh") {
        if (type == path_type::relative) {
          if (explicit_input) {
            files << p.lexically_relative(input).string() << newline;
          } else {
            files << p.lexically_relative(td).string() << newline;
          }
        } else {
          files << p.string() << newline;
        }
      }
    }
  }

  auto filelist = td / "filelist.txt";
  dwarfs::write_file(filelist, files.str());

  {
    std::vector<std::string> args{"--input-list", filelist.string(), "-o",
                                  output.string()};
    if (explicit_input) {
      args.push_back("-i");
      args.push_back(input.lexically_relative(td).string());
    }

    auto [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ mkdwarfs_bin, args);
    ASSERT_EQ(0, ec) << out << err;
  }

  auto extracted = td / "extracted";
  ASSERT_TRUE(fs::create_directory(extracted));
  EXPECT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin,
                                    "-i", output.string(), "-o",
                                    extracted.string()));

  std::set<fs::path> extracted_files;
  for (auto const& entry : fs::recursive_directory_iterator(extracted)) {
    if (entry.is_regular_file()) {
      extracted_files.insert(entry.path().lexically_relative(extracted));
    }
  }

  auto base = explicit_input ? fs::path{"foo"} : fs::path{"input"} / "foo";

  std::set<fs::path> const expected_files{
      base / "bla.sh",
      base / "1" / "fmt.sh",
      base / "1" / "2" / "xxx.sh",
      base / "1" / "2" / "3" / "copy.sh",
  };

  EXPECT_EQ(extracted_files, expected_files) << files.str();
}

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_tool_input_list,
                         ::testing::Combine(::testing::ValuesIn(path_types),
                                            ::testing::Bool()));

#ifdef __linux__
TEST_P(tools_test, fusermount_check) {
#ifndef DWARFS_WITH_FUSE_DRIVER

  GTEST_SKIP() << "FUSE driver not built";

#elif defined(DWARFS_CROSSCOMPILING_EMULATOR)

  GTEST_SKIP() << "skipping bubblewrap tests when cross-compiling";

#else

  auto const mode = GetParam();

  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  auto bwrap = dwarfs::test::find_binary("bwrap");

  if (!bwrap) {
    GTEST_SKIP() << "bubblewrap not found";
  }

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
  auto mountpoint = td / "mnt";
  auto universal_symlink_dwarfs_bin = td / "dwarfs" EXE_EXT;

  fs::create_directory(mountpoint);

  if (mode == binary_mode::universal_symlink) {
    fs::create_symlink(universal_bin, universal_symlink_dwarfs_bin);
  }

  std::vector<fs::path> drivers;
  std::vector<std::string> dwarfs_tool_arg;

  switch (mode) {
  case binary_mode::standalone:
    drivers.push_back(fuse3_bin);

    if (fs::exists(fuse2_bin)) {
      drivers.push_back(fuse2_bin);
    }
    break;

  case binary_mode::universal_tool:
    drivers.push_back(universal_bin);
    dwarfs_tool_arg.push_back("--tool=dwarfs");
    break;

  case binary_mode::universal_symlink:
    drivers.push_back(universal_symlink_dwarfs_bin);
    break;
  }

  std::vector<std::string> bwrap_args{
      "--unshare-user",
      "--unshare-pid",
      "--unshare-uts",
      "--unshare-net",
      "--unshare-ipc",
      "--tmpfs",
      "/",
  };

  std::vector<fs::path> ro_bind_paths{
      "/proc",    "/dev",       "/lib", "/lib64",
      "/usr/lib", "/usr/lib64", "/etc", DWARFS_SOURCE_DIR,
  };

  std::vector<fs::path> rw_bind_paths{
      tools_dir,
      td,
  };

#ifdef DWARFS_CMAKE_PREFIX_PATH
  for (auto const& p : dwarfs::split_to<std::vector<std::string>>(
           DWARFS_CMAKE_PREFIX_PATH, ':')) {
    ro_bind_paths.emplace_back(p);
  }
#endif

  for (auto const& p : ro_bind_paths) {
    if (fs::exists(p)) {
      bwrap_args.push_back("--ro-bind");
      bwrap_args.push_back(p.string());
      bwrap_args.push_back(p.string());
    }
  }

  for (auto const& p : rw_bind_paths) {
    bwrap_args.push_back("--bind");
    bwrap_args.push_back(p.string());
    bwrap_args.push_back(p.string());
  }

  for (auto const& driver : drivers) {
    scoped_no_leak_check no_leak_check;
    auto const [out, err, ec] =
        subprocess::run(bwrap.value(), bwrap_args, driver, dwarfs_tool_arg,
                        test_data_dwarfs, mountpoint, "-f");

    EXPECT_NE(0, ec) << out << err;

    std::string const package = driver == fuse2_bin ? "fuse/fuse2" : "fuse3";

    EXPECT_THAT(err, ::testing::HasSubstr("Do you need to install the `" +
                                          package + "' package?"));
  }
#endif
}
#endif

class sparse_files_test : public ::testing::Test {
 protected:
  struct config {
    double avg_extent_count{10};
    double avg_hole_size{256_KiB};
    double avg_data_size{25_KiB};
  };

  struct sparse_size_info {
    dwarfs::file_size_t total_size{0};
    dwarfs::file_size_t data_size{0};
  };

  struct sparse_file_info {
    fs::path path;
    sparse_size_info size;
    size_t extent_count{0};
  };

  struct sparse_info {
    std::vector<sparse_file_info> files;
    sparse_size_info total;
  };

  void SetUp() override {
    td.emplace();

    input = td->path() / "input";

    granularity =
        dwarfs::test::sparse_file_builder::hole_granularity(td->path());

    if (!granularity) {
      GTEST_SKIP() << "filesystem does not support sparse files";
    }

    std::cerr << "granularity: " << dwarfs::size_with_unit(granularity.value())
              << "\n";
  }

  void TearDown() override {
    td.reset();
    input.clear();
  }

  template <std::integral T>
  T align_up(T value) const {
    auto const gran = static_cast<T>(granularity.value());
    return ((value + gran - 1) / gran) * gran;
  }

  sparse_file_info
  create_random_sparse_file(fs::path const& path, config const& cfg) {
    using dwarfs::file_off_t;
    using dwarfs::file_range;
    using dwarfs::file_size_t;

    size_t total_extents;

    if (std::uniform_int_distribution<>(0, 1)(rng) == 0) {
      total_extents = std::uniform_int_distribution<size_t>(1, 2)(rng);
    } else {
      total_extents = static_cast<size_t>(
          1 + std::exponential_distribution<>(1 / cfg.avg_extent_count)(rng));
    }

    std::exponential_distribution<> hole_size_dist(1 / cfg.avg_hole_size);
    std::exponential_distribution<> data_size_dist(1 / cfg.avg_data_size);

    bool is_hole = std::uniform_int_distribution<>(0, 1)(rng) == 0;
    std::vector<dwarfs::detail::file_extent_info> extents;
    file_off_t offset{0};
    file_size_t data_size{0};

    for (size_t i = 0; i < total_extents; ++i) {
      file_size_t len;

      if (is_hole) {
        len = align_up(static_cast<file_size_t>(1 + hole_size_dist(rng)));
      } else {
        len = static_cast<file_size_t>(1 + data_size_dist(rng));
        if (i < total_extents - 1) {
          len = align_up(len);
        }
        data_size += len;
      }

      extents.emplace_back(is_hole ? dwarfs::extent_kind::hole
                                   : dwarfs::extent_kind::data,
                           file_range{offset, len});

      offset += len;
      is_hole = !is_hole;
    }

    sparse_file_info const info{.path = path,
                                .size =
                                    {
                                        .total_size = offset,
                                        .data_size = data_size,
                                    },
                                .extent_count = extents.size()};

    auto sfb = dwarfs::test::sparse_file_builder::create(path);

    sfb.truncate(extents.back().range.end());

    std::mt19937_64 data_rng(rng());

    for (auto const& e : extents) {
      if (e.kind == dwarfs::extent_kind::data) {
        bool const random_data =
            std::uniform_int_distribution<>(0, 4)(data_rng) != 0;
        sfb.write_data(e.range.offset(),
                       random_data ? dwarfs::test::create_random_string(
                                         e.range.size(), data_rng)
                                   : dwarfs::test::loremipsum(e.range.size()));
      }
    }

    for (auto const& e : extents) {
      if (e.kind == dwarfs::extent_kind::hole) {
        sfb.punch_hole(e.range.offset(), e.range.size());
      }
    }

    sfb.commit();

    return info;
  }

  sparse_info create_random_sparse_files(fs::path const& dir, size_t count,
                                         config const& cfg) {
    sparse_info info;
    fs::create_directory(dir);
    for (size_t i = 0; i < count; ++i) {
      auto const file_info =
          create_random_sparse_file(dir / fmt::format("file{:04}.bin", i), cfg);
      info.files.push_back(file_info);
      info.total.total_size += file_info.size.total_size;
      info.total.data_size += file_info.size.data_size;
    }
    std::cerr << info;
    return info;
  }

  bool build_image(fs::path const& image) const {
    // Use *really* small blocks, so we can be sure to trigger the
    // `large_hole_size` code paths.
    bool const rv = subprocess::check_run(
                        DWARFS_ARG_EMULATOR_ mkdwarfs_bin, "-i", input.string(),
                        "-o", image.string(), "--categorize", "-l4", "-S14")
                        .has_value();
    if (rv) {
      std::cerr << "Created image: " << image << " ("
                << dwarfs::size_with_unit(fs::file_size(image)) << ")\n";
    }
    return rv;
  }

  std::optional<nlohmann::json> get_fsinfo(fs::path const& image) const {
    auto const out = subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsck_bin,
                                           image.string(), "-j", "-d3");

    if (out) {
      auto fsinfo = nlohmann::json::parse(*out);
      std::cerr << "Ran dwarfsck:\n" << fsinfo.dump(2) << "\n";
      return fsinfo;
    }

    return std::nullopt;
  }

  bool extract_to_dir(fs::path const& image, fs::path const& dir) const {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
      fs::remove_all(dir, ec);
      if (ec) {
        std::cerr << "Failed to remove existing directory " << dir << ": "
                  << ec.message() << "\n";
        return false;
      }
    }
    fs::create_directory(dir, ec);
    if (ec) {
      std::cerr << "Failed to create directory " << dir << ": " << ec.message()
                << "\n";
      return false;
    }
    return subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                                 image.string(), "-o", dir.string())
        .has_value();
  }

  bool extract_to_format(fs::path const& image, std::string_view format,
                         fs::path const& output) const {
    bool const rv = subprocess::check_run(
                        DWARFS_ARG_EMULATOR_ dwarfsextract_bin, "-i",
                        image.string(), "-o", output.string(), "-f", format)
                        .has_value();
    if (rv) {
      std::cerr << "Created " << format << " tarball: " << output << " ("
                << dwarfs::size_with_unit(fs::file_size(output)) << ")\n";
    }
    return rv;
  }

  friend std::ostream&
  operator<<(std::ostream& os, sparse_file_info const& info) {
    os << info.path.filename()
       << ": total_size=" << dwarfs::size_with_unit(info.size.total_size)
       << ", data_size=" << dwarfs::size_with_unit(info.size.data_size)
       << ", extent_count=" << info.extent_count;
    return os;
  }

  friend std::ostream& operator<<(std::ostream& os, sparse_info const& info) {
    for (auto const& file : info.files) {
      os << file << "\n";
    }
    os << "Total: total_size=" << dwarfs::size_with_unit(info.total.total_size)
       << ", data_size=" << dwarfs::size_with_unit(info.total.data_size)
       << "\n";
    return os;
  }

#ifndef _WIN32
  bool tar_supports_sparse(fs::path const& tarbin) const {
    dwarfs::temporary_directory td("dwarfs-tar");
    auto const tarball = test_dir / "sparse.tar";

    auto [out, err, ec] = subprocess::run(tarbin, "-xSf", tarball.string(),
                                          "-C", td.path().string());

    if (ec != 0) {
      std::cerr << "tar -xSf failed: " << out << err << "\n";
      return false;
    }

    auto const sparse_file = td.path() / "hole_then_data";

    if (!fs::exists(sparse_file)) {
      std::cerr << "sparse file not found in tarball\n";
      return false;
    }

    dwarfs::file_stat stat(sparse_file);

    if (stat.size() != 1'060'864) {
      std::cerr << "sparse file size incorrect: " << stat.size() << "\n";
      return false;
    }

    if (std::cmp_greater(stat.allocated_size(), 256_KiB)) {
      std::cerr << "sparse file uses too much disk space: "
                << dwarfs::size_with_unit(stat.allocated_size()) << "\n";
      return false;
    }

    return true;
  }
#endif

  size_t get_extent_count(fs::path const& file) const {
    return os.open_file(file).extents().size();
  }

  bool fuse_supports_sparse(fs::path const& mountpoint,
                            sparse_info const& si) const {
    for (auto const& sfi : si.files) {
      if (sfi.extent_count > 1) {
        auto const path = mountpoint / sfi.path.filename();
        auto const extent_count = get_extent_count(path);
        if (extent_count > 1) {
          std::cerr << "FUSE driver supports sparse files\n";
          return true;
        }
        std::cerr << "File " << path << ": expected " << sfi.extent_count
                  << " extents, but got " << extent_count << "\n";
      }
    }

    std::cerr << "FUSE driver does not support sparse files\n";

    return false;
  }

  std::mt19937_64 rng;
  std::optional<dwarfs::temporary_directory> td;
  fs::path input;
  std::optional<size_t> granularity;
  dwarfs::os_access_generic os;
};

TEST_F(sparse_files_test, random_large_files) {
  static constexpr size_t kNumFiles{20};
  rng.seed(42);
  auto const info = create_random_sparse_files(input, kNumFiles,
                                               {
                                                   .avg_extent_count = 60,
                                                   .avg_hole_size = 500_MiB,
                                                   .avg_data_size = 25_KiB,
                                               });

  auto const image = td->path() / "sparse.dwarfs";
  ASSERT_TRUE(build_image(image));

  auto const fsinfo = get_fsinfo(image);
  ASSERT_TRUE(fsinfo);

  EXPECT_EQ(info.total.total_size,
            (*fsinfo)["original_filesystem_size"].get<dwarfs::file_size_t>());

  auto const dump = subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsck_bin,
                                          image.string(), "-d9");
  ASSERT_TRUE(dump.has_value());
  EXPECT_THAT(*dump, ::testing::HasSubstr("] -> HOLE (size="));
  EXPECT_THAT(*dump, ::testing::HasSubstr("] -> DATA (block="));

  auto const extracted = td->path() / "extracted";
  ASSERT_TRUE(extract_to_dir(image, extracted));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare dwarfsextract extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles) << cdr;
  }

  ASSERT_NO_THROW(fs::remove_all(extracted));

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  auto tarball = td->path() / "extracted.tar";
  ASSERT_TRUE(extract_to_format(image, "pax", tarball));
  EXPECT_LT(fs::file_size(tarball), info.total.data_size * 5)
      << "tarball size is not sufficiently small";

#ifndef _WIN32
  if (auto const tarbin = dwarfs::test::find_binary("tar");
      tarbin && tar_supports_sparse(*tarbin)) {
    ASSERT_NO_THROW(fs::create_directory(extracted));

    ASSERT_TRUE(subprocess::check_run(*tarbin, "-xSf", tarball.string(), "-C",
                                      extracted.string()));

    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare tar extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles) << cdr;
  }
#endif
#endif

#ifdef DWARFS_WITH_FUSE_DRIVER
  if (!skip_fuse_tests()) {
    auto const mountpoint = td->path() / "mnt";

    fs::create_directory(mountpoint);

    {
      driver_runner runner(driver_runner::foreground, fuse3_bin, false, image,
                           mountpoint);

      ASSERT_TRUE(
          wait_until_file_ready(mountpoint / "file0000.bin", kFuseTimeout))
          << runner.cmdline();

      // Only compare if we know the FUSE driver supports sparse files.
      // Otherwise this will try to actually read hundreds of gigabytes
      // of data.
      if (fuse_supports_sparse(mountpoint, info)) {
        auto const cdr = compare_directories(input, mountpoint);

        std::cerr << "Compare FUSE mounted files:\n" << cdr;

        EXPECT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
        EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles)
            << runner.cmdline() << ": " << cdr;
      }

      for (auto const& file : info.files) {
        auto const p = mountpoint / file.path.filename();
        dwarfs::file_stat stat(p);

        EXPECT_EQ(stat.size(), file.size.total_size) << file.path.filename();
      }

      EXPECT_TRUE(runner.unmount()) << runner.cmdline();
    }

    if (fs::exists(fuse2_bin)) {
      driver_runner runner(driver_runner::foreground, fuse2_bin, false, image,
                           mountpoint);

      ASSERT_TRUE(
          wait_until_file_ready(mountpoint / "file0000.bin", kFuseTimeout))
          << runner.cmdline();

      for (auto const& file : info.files) {
        auto const p = mountpoint / file.path.filename();
        dwarfs::file_stat stat(p);

        EXPECT_EQ(stat.size(), file.size.total_size) << file.path.filename();
      }

      EXPECT_TRUE(runner.unmount()) << runner.cmdline();
    }
  }
#endif
}

TEST_F(sparse_files_test, random_small_files_tarball) {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  GTEST_SKIP() << "filesystem_extractor format support disabled";
#elif defined(_WIN32)
  GTEST_SKIP() << "skipping tarball tests on Windows";
#else
  auto const tarbin = dwarfs::test::find_binary("tar");

  if (!tarbin) {
    GTEST_SKIP() << "tar binary not found";
  }

  if (!tar_supports_sparse(*tarbin)) {
    GTEST_SKIP() << "tar does not support sparse files";
  }

  static constexpr size_t kNumFiles{20};
  rng.seed(42);
  auto const info = create_random_sparse_files(input, kNumFiles, {});

  auto const image = td->path() / "sparse.dwarfs";
  ASSERT_TRUE(build_image(image));

  auto const fsinfo = get_fsinfo(image);
  ASSERT_TRUE(fsinfo);

  EXPECT_EQ(info.total.total_size,
            (*fsinfo)["original_filesystem_size"].get<dwarfs::file_size_t>());

  auto const extracted = td->path() / "extracted";
  ASSERT_TRUE(extract_to_dir(image, extracted));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare dwarfsextract extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles) << cdr;
  }

  ASSERT_NO_THROW(fs::remove_all(extracted));

  auto pax_tarball = td->path() / "extracted_pax.tar";
  ASSERT_TRUE(extract_to_format(image, "pax", pax_tarball));

  auto ustar_tarball = td->path() / "extracted_ustar.tar";
  ASSERT_TRUE(extract_to_format(image, "ustar", ustar_tarball));

  EXPECT_LT(fs::file_size(pax_tarball), fs::file_size(ustar_tarball));

  ASSERT_NO_THROW(fs::create_directory(extracted));

  ASSERT_TRUE(subprocess::check_run(*tarbin, "-xSf", pax_tarball.string(), "-C",
                                    extracted.string()));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare pax extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles) << cdr;
  }

  ASSERT_NO_THROW(fs::remove_all(extracted));
  ASSERT_NO_THROW(fs::create_directory(extracted));

  ASSERT_TRUE(subprocess::check_run(*tarbin, "-xf", ustar_tarball.string(),
                                    "-C", extracted.string()));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare ustar extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles) << cdr;
  }
#endif
}

TEST_F(sparse_files_test, random_small_files_fuse) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  static constexpr size_t kNumFiles{30};
  rng.seed(43);
  auto const info = create_random_sparse_files(input, kNumFiles, {});

  auto const image = td->path() / "sparse.dwarfs";
  ASSERT_TRUE(build_image(image));

  auto const fsinfo = get_fsinfo(image);
  ASSERT_TRUE(fsinfo);

  EXPECT_EQ(info.total.total_size,
            (*fsinfo)["original_filesystem_size"].get<dwarfs::file_size_t>());

  auto const mountpoint = td->path() / "mnt";

  ASSERT_NO_THROW(fs::create_directory(mountpoint));

  std::vector<fs::path> drivers;

  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  for (auto const& driver_bin : drivers) {
    driver_runner runner(driver_runner::foreground, driver_bin, false, image,
                         mountpoint);

    ASSERT_TRUE(
        wait_until_file_ready(mountpoint / "file0000.bin", kFuseTimeout))
        << runner.cmdline();

    auto const cdr = compare_directories(input, mountpoint);

    std::cerr << "Compare FUSE mounted files for " << driver_bin.filename()
              << ":\n"
              << cdr;

    EXPECT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), kNumFiles)
        << runner.cmdline() << ": " << cdr;

    for (auto const& file : info.files) {
      auto const p = mountpoint / file.path.filename();
      dwarfs::file_stat stat(p);

      EXPECT_EQ(stat.size(), file.size.total_size) << file.path.filename();
    }

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
#endif
}

TEST_F(sparse_files_test, huge_holes_tar) {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  GTEST_SKIP() << "filesystem_extractor format support disabled";
#elif defined(_WIN32)
  GTEST_SKIP() << "skipping tarball tests on Windows";
#else
  auto const tarbin = dwarfs::test::find_binary("tar");

  if (!tarbin) {
    GTEST_SKIP() << "tar binary not found";
  }

  if (!tar_supports_sparse(*tarbin)) {
    GTEST_SKIP() << "tar does not support sparse files";
  }

  ASSERT_NO_THROW(fs::create_directory(input));

  auto const hole_then_data = input / "hole_then_data";

  {
    auto sfb = dwarfs::test::sparse_file_builder::create(hole_then_data);
    sfb.truncate(5_GiB + 16_KiB);
    sfb.write_data(5_GiB, dwarfs::test::loremipsum(16_KiB));
    sfb.punch_hole(0, 5_GiB);
    sfb.commit();
  }

  auto const hole_only = input / "hole_only";

  {
    auto sfb = dwarfs::test::sparse_file_builder::create(hole_only);
    sfb.truncate(4100_MiB);
    sfb.punch_hole(0, 4100_MiB);
    sfb.commit();
  }

  auto const image = td->path() / "sparse.dwarfs";
  ASSERT_TRUE(build_image(image));

  auto const fsinfo = get_fsinfo(image);
  ASSERT_TRUE(fsinfo);

  EXPECT_EQ(5_GiB + 16_KiB + 4100_MiB,
            (*fsinfo)["original_filesystem_size"].get<dwarfs::file_size_t>());

  auto const extracted = td->path() / "extracted";
  ASSERT_TRUE(extract_to_dir(image, extracted));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare dwarfsextract extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 2) << cdr;
  }

  ASSERT_NO_THROW(fs::remove_all(extracted));

  auto tarball = td->path() / "extracted.tar";
  ASSERT_TRUE(extract_to_format(image, "pax", tarball));

  ASSERT_NO_THROW(fs::create_directory(extracted));

  ASSERT_TRUE(subprocess::check_run(*tarbin, "-xSf", tarball.string(), "-C",
                                    extracted.string()));

  {
    auto const cdr = compare_directories(input, extracted);

    std::cerr << "Compare extracted files:\n" << cdr;

    EXPECT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 2) << cdr;
  }
#endif
}

TEST_F(sparse_files_test, huge_holes_fuse) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  ASSERT_NO_THROW(fs::create_directory(input));

  auto const hole_then_data = input / "hole_then_data";

  {
    auto sfb = dwarfs::test::sparse_file_builder::create(hole_then_data);
    sfb.truncate(5_GiB + 16_KiB);
    sfb.write_data(5_GiB, dwarfs::test::loremipsum(16_KiB));
    sfb.punch_hole(0, 5_GiB);
    sfb.commit();
  }

  auto const hole_only = input / "hole_only";

  {
    auto sfb = dwarfs::test::sparse_file_builder::create(hole_only);
    sfb.truncate(4100_MiB);
    sfb.punch_hole(0, 4100_MiB);
    sfb.commit();
  }

  auto const image = td->path() / "sparse.dwarfs";
  ASSERT_TRUE(build_image(image));

  auto const fsinfo = get_fsinfo(image);
  ASSERT_TRUE(fsinfo);

  EXPECT_EQ(5_GiB + 16_KiB + 4100_MiB,
            (*fsinfo)["original_filesystem_size"].get<dwarfs::file_size_t>());

  auto const mountpoint = td->path() / "mnt";

  ASSERT_NO_THROW(fs::create_directory(mountpoint));

  std::vector<fs::path> drivers;

  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  for (auto const& driver_bin : drivers) {
    driver_runner runner(driver_runner::foreground, driver_bin, false, image,
                         mountpoint);

    ASSERT_TRUE(
        wait_until_file_ready(mountpoint / "hole_then_data", kFuseTimeout))
        << runner.cmdline();

    EXPECT_EQ(fs::file_size(mountpoint / "hole_then_data"), 5_GiB + 16_KiB)
        << runner.cmdline();

    EXPECT_EQ(fs::file_size(mountpoint / "hole_only"), 4100_MiB)
        << runner.cmdline();

    if (get_extent_count(mountpoint / "hole_then_data") > 1) {
      auto const cdr = compare_directories(input, mountpoint);

      std::cerr << "Compare FUSE mounted files for " << driver_bin.filename()
                << ":\n"
                << cdr;

      EXPECT_TRUE(cdr.identical()) << runner.cmdline() << ": " << cdr;
      EXPECT_EQ(cdr.matching_regular_files.size(), 2)
          << runner.cmdline() << ": " << cdr;
    }

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
#endif
}

namespace {

struct file_times {
  template <typename D>
  auto to_sec(std::chrono::time_point<std::chrono::system_clock, D> tp) {
    return std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp));
  }

  uint32_t truncate_to_res(std::chrono::nanoseconds ns) {
#if defined(__s390x__) && defined(DWARFS_CROSSCOMPILING_EMULATOR)
    // S390x qemu user emulation does not support nanosecond timestamps.
    // See https://github.com/bytecodealliance/rustix/pull/282/files
    return 0;
#else
    return static_cast<uint32_t>(
        (ns - ns % dwarfs::file_stat::native_time_resolution()).count());
#endif
  }

  file_times() = default;
  template <typename D>
  file_times(std::chrono::time_point<std::chrono::system_clock, D> m,
             std::chrono::nanoseconds mns,
             std::chrono::time_point<std::chrono::system_clock, D> a,
             std::chrono::nanoseconds ans,
             std::chrono::time_point<std::chrono::system_clock, D> c,
             std::chrono::nanoseconds cns)
      : mtime{to_sec(m), truncate_to_res(mns)}
      , atime{to_sec(a), truncate_to_res(ans)}
      , ctime{to_sec(c), truncate_to_res(cns)} {}

  dwarfs::file_stat::timespec_type mtime;
  dwarfs::file_stat::timespec_type atime;
  dwarfs::file_stat::timespec_type ctime;
};

std::array kFileTimes{
    std::pair{
        "file_1w2s3lb6"sv,
        file_times{std::chrono::sys_days{2021y / 12 / 26} + 8h + 56min + 10s,
                   723'376'645ns,
                   std::chrono::sys_days{2021y / 3 / 3} + 20h + 34min + 12s,
                   91'734'903ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_573stdbu"sv,
        file_times{std::chrono::sys_days{2022y / 3 / 22} + 11h + 32min + 27s,
                   182'893'930ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 24s,
                   796'111'239ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_573stdbu/file_a45sc57n"sv,
        file_times{std::chrono::sys_days{2019y / 3 / 8} + 17h + 3min + 42s,
                   615'891'838ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 28min + 3s,
                   249'965'124ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_573stdbu/file_xh7183o5"sv,
        file_times{std::chrono::sys_days{2019y / 11 / 06} + 16h + 43min + 43s,
                   440'687'449ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 28min + 49s,
                   630'593'008ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_573stdbu/link_mpfppenu"sv,
        file_times{std::chrono::sys_days{2022y / 7 / 16} + 16h + 4min + 21s,
                   203'054'271ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 32s,
                   459'548'315ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_sgy2vnnq"sv,
        file_times{std::chrono::sys_days{2021y / 10 / 25} + 15h + 46min + 46s,
                   570'837'717ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 24s,
                   796'111'239ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_sgy2vnnq/file_lmyplgqf"sv,
        file_times{std::chrono::sys_days{2024y / 6 / 10} + 17h + 17min + 12s,
                   270'375'466ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 28min + 49s,
                   630'593'008ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
    std::pair{
        "dir_sgy2vnnq/link_pjcnuj7u"sv,
        file_times{std::chrono::sys_days{2018y / 11 / 8} + 3h + 28min + 36s,
                   315'733'571ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 32s,
                   459'548'315ns,
                   std::chrono::sys_days{2025y / 10 / 15} + 15h + 27min + 20s,
                   819'390'738ns},
    },
};

} // namespace

TEST(tools_test, timestamps_fuse) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }
  dwarfs::temporary_directory td("dwarfs");
  auto const mountpoint = td.path() / "mnt";
  auto const image = test_dir / "timestamps.dwarfs";

  std::vector<fs::path> drivers;

  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  for (auto const& driver_bin : drivers) {
    driver_runner runner(driver_runner::foreground, driver_bin, false, image,
                         mountpoint);

    ASSERT_TRUE(
        wait_until_file_ready(mountpoint / "file_1w2s3lb6", kFuseTimeout))
        << runner.cmdline();

    for (auto const& [path, ft] : kFileTimes) {
      auto const full_path = mountpoint / path;

      dwarfs::file_stat stat(full_path);

      EXPECT_EQ(ft.mtime, stat.mtimespec()) << path << " " << runner.cmdline();
      EXPECT_EQ(ft.atime, stat.atimespec()) << path << " " << runner.cmdline();
      EXPECT_EQ(ft.ctime, stat.ctimespec()) << path << " " << runner.cmdline();
    }

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
#endif
}

TEST(tools_test, timestamps_extract) {
  dwarfs::temporary_directory td("dwarfs");
  auto const extracted = td.path() / "extracted";
  auto const image = test_dir / "timestamps.dwarfs";

  ASSERT_TRUE(fs::create_directory(extracted));
  EXPECT_TRUE(subprocess::check_run(DWARFS_ARG_EMULATOR_ dwarfsextract_bin,
                                    "-i", image.string(), "-o",
                                    extracted.string()));

  for (auto const& [path, ft] : kFileTimes) {
    auto const full_path = extracted / path;

#ifdef _WIN32
    if (fs::is_symlink(full_path)) {
      // Seems like on Windows, symlink timestamps are not settable?
      continue;
    }
#endif

    dwarfs::file_stat stat(full_path);

    EXPECT_EQ(ft.mtime, stat.mtimespec()) << path;
    EXPECT_EQ(ft.atime, stat.atimespec()) << path;
  }
}

TEST(tools_test, dwarfs_automount) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  dwarfs::temporary_directory td("dwarfs");

  auto const image = td.path() / "timestamps.dwarfs";
  fs::copy(test_dir / "timestamps.dwarfs", image);

  auto const mountpoint = td.path() / "timestamps";

  std::vector<fs::path> drivers;

  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  for (auto const& driver_bin : drivers) {
    driver_runner runner(driver_runner::automount, driver_bin, false, image,
                         mountpoint);

    EXPECT_TRUE(
        wait_until_file_ready(mountpoint / "file_1w2s3lb6", kFuseTimeout))
        << runner.cmdline();

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();

    EXPECT_FALSE(fs::exists(mountpoint));
  }
#endif
}

TEST(tools_test, dwarfs_automount_error) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  dwarfs::temporary_directory td("dwarfs");
  scoped_no_leak_check no_leak_check;

  auto const image_noext = td.path() / "data";
  fs::copy(test_dir / "data.dwarfs", image_noext);

  auto const mountpoint = td.path() / "data";

  {
    auto const [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint",
                        image_noext.string(), mountpoint.string());
    EXPECT_NE(ec, 0);
    EXPECT_THAT(
        err, ::testing::HasSubstr(
                 "error: cannot combine <mountpoint> with --auto-mountpoint"));
  }

  {
    auto const [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint",
                        image_noext.string());
    EXPECT_NE(ec, 0);
    EXPECT_THAT(err,
                ::testing::HasSubstr("error: cannot select mountpoint "
                                     "directory for file with no extension"));
  }

  {
    auto const [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint");
    EXPECT_NE(ec, 0);
    EXPECT_THAT(out, ::testing::HasSubstr("Usage: dwarfs"));
  }

#ifndef _WIN32
  auto const image = td.path() / "data.dwarfs";
  fs::rename(image_noext, image);
  fs::create_directory(mountpoint);
  fs::create_directory(mountpoint / "subdir");

  {
    auto const [out, err, ec] = subprocess::run(
        DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint", image.string());
    EXPECT_NE(ec, 0);
    EXPECT_THAT(
        err, ::testing::HasSubstr(
                 "error: cannot find a suitable empty mountpoint directory"));
  }

  fs::remove_all(mountpoint);
  dwarfs::write_file(mountpoint, "not a directory");

  {
    auto const [out, err, ec] = subprocess::run(
        DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint", image.string());
    EXPECT_NE(ec, 0);
    EXPECT_THAT(
        err, ::testing::HasSubstr(
                 "error: cannot find a suitable empty mountpoint directory"));
  }

  fs::remove(mountpoint);
  fs::remove(image);

  {
    driver_runner runner(fuse3_bin, false, test_dir / "datadata.dwarfs",
                         mountpoint);

    EXPECT_TRUE(wait_until_file_ready(mountpoint / "data.dwarfs", kFuseTimeout))
        << runner.cmdline();

    auto const [out, err, ec] =
        subprocess::run(DWARFS_ARG_EMULATOR_ fuse3_bin, "--auto-mountpoint",
                        mountpoint / "data.dwarfs");
    EXPECT_NE(ec, 0);
    EXPECT_THAT(err, ::testing::HasSubstr(
                         "error: unable to create mountpoint directory: "));

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
#endif
#endif
}

#ifndef _WIN32
TEST(tools_test, dwarfs_fsname_and_subtype) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

#ifdef __linux__
  fs::path proc_mounts{"/proc/self/mounts"};

  if (!fs::exists(proc_mounts)) {
    GTEST_SKIP() << proc_mounts << " not found";
  }
#else
  auto mountbin = dwarfs::test::find_binary("mount");

  if (!mountbin) {
    GTEST_SKIP() << "`mount` binary not found";
  }
#endif

  dwarfs::temporary_directory td("dwarfs");

  auto const image = fs::canonical(test_dir) / "timestamps.dwarfs";
  auto const mountpoint = fs::canonical(td.path()) / "mnt";
  ASSERT_NO_THROW(fs::create_directory(mountpoint));

  std::vector<fs::path> drivers;

  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  for (auto const& driver_bin : drivers) {
    driver_runner runner(driver_bin, false, image, mountpoint);

    EXPECT_TRUE(
        wait_until_file_ready(mountpoint / "file_1w2s3lb6", kFuseTimeout))
        << runner.cmdline();

    std::optional<std::string> out;

#ifdef __linux__
    out.emplace(dwarfs::read_file(proc_mounts));
#else
    out = subprocess::check_run(*mountbin);
#endif

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();

    ASSERT_TRUE(out.has_value()) << runner.cmdline();
    std::optional<std::string> mpline;

    {
      std::istringstream iss(*out);
      std::string line;

      while (std::getline(iss, line)) {
        if (line.find(mountpoint.string()) != std::string::npos) {
          mpline.emplace(line);
          break;
        }
      }
    }

    // Linux: <image> <mountpoint> fuse.dwarfs ...
    // macOS: <image> on <mountpoint> (macfuse_dwarfs, ...)
    // FreeBSD: <image> on <mountpoint> (fusefs.dwarfs, ...)

    ASSERT_TRUE(mpline.has_value()) << runner.cmdline() << "\n" << *out;

#if defined(__linux__)
    EXPECT_THAT(*mpline,
                ::testing::HasSubstr(image.string() + " " +
                                     mountpoint.string() + " fuse.dwarfs "));
#elif defined(__APPLE__)
    // It seems that macFUSE currently truncates the `fsname` string, so
    // we don't check for the full image path here (yet).
    EXPECT_THAT(*mpline, ::testing::HasSubstr("(macfuse_dwarfs"));
#else // FreeBSD
    EXPECT_THAT(*mpline, ::testing::HasSubstr(image.string() + " on " +
                                              mountpoint.string() + " "));
    EXPECT_THAT(*mpline, ::testing::HasSubstr("(fusefs.dwarfs"));
#endif
  }
#endif
}
#endif

TEST(tools_test, dwarfs_image_size) {
#ifndef DWARFS_WITH_FUSE_DRIVER
  GTEST_SKIP() << "FUSE driver not built";
#else
  if (skip_fuse_tests()) {
    GTEST_SKIP() << "skipping FUSE tests";
  }

  dwarfs::temporary_directory td("dwarfs");
  scoped_no_leak_check no_leak_check;

  auto const header = dwarfs::read_file(test_dir / "tools_test.cpp");
  auto const image = dwarfs::read_file(test_dir / "data.dwarfs");
  auto const image_size = image.size();

  dwarfs::write_file(td.path() / "test.dwarfs", header + image + header);
  fs::create_directory(td.path() / "mnt");

  {
    auto [out, err, ec] = subprocess::run(DWARFS_ARG_EMULATOR_ fuse3_bin,
                                          td.path() / "test.dwarfs",
                                          td.path() / "mnt", "-ooffset=auto");

    EXPECT_NE(ec, 0);
    EXPECT_THAT(err, ::testing::HasSubstr("error initializing file system"));
  }

  {
    driver_runner runner(driver_runner::foreground, fuse3_bin, false,
                         td.path() / "test.dwarfs", td.path() / "mnt",
                         "-ooffset=auto",
                         "-oimagesize=" + std::to_string(image_size));

    EXPECT_TRUE(
        wait_until_file_ready(td.path() / "mnt" / "format.sh", kFuseTimeout))
        << runner.cmdline();

    EXPECT_TRUE(runner.unmount()) << runner.cmdline();
  }
#endif
}

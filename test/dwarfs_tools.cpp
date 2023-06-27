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

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <tuple>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/xattr.h>
#endif

#include <folly/portability/Unistd.h>

#include <boost/asio/io_service.hpp>
#include <boost/process.hpp>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/experimental/TestUtil.h>

#include <fmt/format.h>

#include "dwarfs/file_stat.h"

#include "test_helpers.h"

namespace {

namespace bp = boost::process;
namespace fs = std::filesystem;

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto test_data_tar = test_dir / "data.tar";
auto test_data_dwarfs = test_dir / "data.dwarfs";

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

auto diff_bin = fs::path(DIFF_BIN).make_preferred();
auto tar_bin = fs::path(TAR_BIN).make_preferred();

#ifndef _WIN32
pid_t get_dwarfs_pid(fs::path const& path) {
  std::array<char, 32> attr_buf;
  auto attr_len = ::getxattr(path.c_str(), "user.dwarfs.driver.pid",
                             attr_buf.data(), attr_buf.size());
  if (attr_len < 0) {
    throw std::runtime_error("could not read pid from xattr");
  }
  return folly::to<pid_t>(std::string_view(attr_buf.data(), attr_len));
}
#endif

bool wait_until_file_ready(fs::path const& path,
                           std::chrono::milliseconds timeout) {
  auto end = std::chrono::steady_clock::now() + timeout;
  std::error_code ec;
  while (!fs::exists(path, ec)) {
    if (ec) {
      std::cerr << "*** exists: " << ec.message() << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
  }
  return true;
}

#ifdef _WIN32
struct new_process_group : public ::boost::process::detail::handler_base {
  template <class WindowsExecutor>
  void on_setup(WindowsExecutor& e [[maybe_unused]]) const {
    e.creation_flags |= CREATE_NEW_PROCESS_GROUP;
  }
};
#endif

class subprocess {
 public:
  template <typename... Args>
  subprocess(std::filesystem::path const& prog, Args&&... args) {
    std::vector<std::string> cmdline;
    (append_arg(cmdline, std::forward<Args>(args)), ...);

    try {
      c_ = bp::child(prog.string(), bp::args(cmdline), bp::std_in.close(),
                     bp::std_out > out_, bp::std_err > err_, ios_
#ifdef _WIN32
                     ,
                     new_process_group()
#endif
      );
    } catch (...) {
      std::cerr << "failed to create subprocess: " << prog << " "
                << folly::join(' ', cmdline) << "\n";
      throw;
    }
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
#ifdef _WIN32
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid());
#else
    ::kill(pid(), SIGINT);
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

  template <typename T>
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
};

#ifndef _WIN32
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
  driver_runner(fs::path const& driver, fs::path const& image,
                fs::path const& mountpoint, Args&&... args)
      : mountpoint_{mountpoint} {
    setup_mountpoint(mountpoint);
#ifdef _WIN32
    process_ = std::make_unique<subprocess>(driver, image, mountpoint,
                                            std::forward<Args>(args)...);
    process_->run_background();

    wait_until_file_ready(mountpoint, std::chrono::seconds(5));
#else
    if (!subprocess::check_run(driver, image, mountpoint,
                               std::forward<Args>(args)...)) {
      throw std::runtime_error("error running " + driver.string());
    }
    dwarfs_guard_ = process_guard(get_dwarfs_pid(mountpoint));
#endif
  }

  template <typename... Args>
  driver_runner(foreground_t, fs::path const& driver, fs::path const& image,
                fs::path const& mountpoint, Args&&... args)
      : mountpoint_{mountpoint} {
    setup_mountpoint(mountpoint);
    process_ = std::make_unique<subprocess>(driver, image, mountpoint,
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
    if (!mountpoint_.empty()) {
#ifndef _WIN32
      if (process_) {
#endif
#ifdef _WIN32
        constexpr int expected_exit_code = 0;
#else
      constexpr int expected_exit_code = SIGINT;
#endif
        process_->interrupt();
        process_->wait();
        auto ec = process_->exit_code();
        if (ec != expected_exit_code) {
          std::cerr << "driver failed to unmount:\nout:\n"
                    << process_->out() << "err:\n"
                    << process_->err() << "exit code: " << ec << "\n";
        }
        process_.reset();
        mountpoint_.clear();
        return ec == expected_exit_code;
#ifndef _WIN32
      } else {
        subprocess::check_run(find_fusermount(), "-u", mountpoint_);
        mountpoint_.clear();
        return dwarfs_guard_.check_exit(std::chrono::seconds(5));
      }
#endif
    }
    return false;
  }

  ~driver_runner() {
    if (!mountpoint_.empty()) {
      if (!unmount()) {
        ::abort();
      }
    }
  }

 private:
#ifndef _WIN32
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
#ifndef _WIN32
  process_guard dwarfs_guard_;
#endif
};

bool check_readonly(fs::path const& p, bool readonly = false) {
  auto st = fs::status(p);
  bool is_writable =
      (st.permissions() & fs::perms::owner_write) != fs::perms::none;

  if (is_writable == readonly) {
    std::cerr << "readonly=" << readonly << ", st_mode="
              << fmt::format("{0:o}", uint16_t(st.permissions())) << "\n";
    return false;
  }

#ifndef _WIN32
  if (::access(p.string().c_str(), W_OK) == 0) {
    // access(W_OK) should never succeed
    ::perror("access");
    return false;
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

} // namespace

TEST(tools, everything) {
  std::chrono::seconds const timeout{5};
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = fs::path(tempdir.path().string());
  auto image = td / "test.dwarfs";
  auto image_hdr = td / "test_hdr.dwarfs";
  auto data_dir = td / "data";
  auto fsdata_dir = td / "fsdata";
  auto header_data = data_dir / "format.sh";

  ASSERT_TRUE(fs::create_directory(fsdata_dir));
  ASSERT_TRUE(subprocess::check_run(dwarfsextract_bin, "-i", test_data_dwarfs,
                                    "-o", fsdata_dir));

  EXPECT_EQ(num_hardlinks(fsdata_dir / "format.sh"), 3);
  EXPECT_TRUE(fs::is_symlink(fsdata_dir / "foobar"));
  EXPECT_EQ(fs::read_symlink(fsdata_dir / "foobar"), fs::path("foo") / "bar");

  ASSERT_TRUE(subprocess::check_run(tar_bin, "xf", test_data_tar, "-C", td));
  ASSERT_TRUE(subprocess::check_run(diff_bin, "-qruN", data_dir, fsdata_dir));

  ASSERT_TRUE(subprocess::check_run(mkdwarfs_bin, "-i", fsdata_dir, "-o", image,
                                    "--no-progress"));

  ASSERT_TRUE(fs::exists(image));
  ASSERT_GT(fs::file_size(image), 1000);

  ASSERT_TRUE(subprocess::check_run(mkdwarfs_bin, "-i", image, "-o", image_hdr,
                                    "--no-progress", "--recompress=none",
                                    "--header", header_data));

  ASSERT_TRUE(fs::exists(image_hdr));
  ASSERT_GT(fs::file_size(image_hdr), 1000);

  auto mountpoint = td / "mnt";
  auto extracted = td / "extracted";
  auto untared = td / "untared";

  std::vector<fs::path> drivers;
  drivers.push_back(fuse3_bin);

  if (fs::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
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
  };

  for (auto const& driver : drivers) {
    {
      driver_runner runner(driver_runner::foreground, driver, image,
                           mountpoint);

      ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout));
      ASSERT_TRUE(
          subprocess::check_run(diff_bin, "-qruN", fsdata_dir, mountpoint));
      EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh"));

      EXPECT_TRUE(runner.unmount());
    }

    {
      auto const [out, err, ec] =
          subprocess::run(driver, image_hdr, mountpoint);

      EXPECT_NE(0, ec) << "stdout:\n" << out << "\nstderr:\n" << err;
    }

    unsigned const combinations = 1 << all_options.size();

    for (unsigned bitmask = 0; bitmask < combinations; ++bitmask) {
      std::vector<std::string> args;
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

      {
        driver_runner runner(driver, image, mountpoint, args);

        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar"));
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar");
        EXPECT_TRUE(
            subprocess::check_run(diff_bin, "-qruN", fsdata_dir, mountpoint));
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(enable_nlink ? 3 : 1,
                  num_hardlinks(mountpoint / "format.sh"));
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly));
#endif
      }

      args.push_back("-ooffset=auto");

      {
        driver_runner runner(driver, image_hdr, mountpoint, args);

        EXPECT_TRUE(fs::is_symlink(mountpoint / "foobar"));
        EXPECT_EQ(fs::read_symlink(mountpoint / "foobar"),
                  fs::path("foo") / "bar");
        EXPECT_TRUE(
            subprocess::check_run(diff_bin, "-qruN", fsdata_dir, mountpoint));
#ifndef _WIN32
        // TODO: https://github.com/winfsp/winfsp/issues/511
        EXPECT_EQ(enable_nlink ? 3 : 1,
                  num_hardlinks(mountpoint / "format.sh"));
        // This doesn't really work on Windows (yet)
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly));
#endif
      }
    }
  }

  auto meta_export = td / "test.meta";

  ASSERT_TRUE(subprocess::check_run(dwarfsck_bin, image));
  ASSERT_TRUE(subprocess::check_run(dwarfsck_bin, image, "--check-integrity"));
  ASSERT_TRUE(subprocess::check_run(dwarfsck_bin, image, "--export-metadata",
                                    meta_export));

  {
    std::string header;

    EXPECT_TRUE(folly::readFile(header_data.string().c_str(), header));

    auto output = subprocess::check_run(dwarfsck_bin, image_hdr, "-H");

    ASSERT_TRUE(output);

    EXPECT_EQ(header, *output);
  }

  EXPECT_GT(fs::file_size(meta_export), 1000);

  ASSERT_TRUE(fs::create_directory(extracted));

  ASSERT_TRUE(
      subprocess::check_run(dwarfsextract_bin, "-i", image, "-o", extracted));
  EXPECT_EQ(3, num_hardlinks(extracted / "format.sh"));
  EXPECT_TRUE(fs::is_symlink(extracted / "foobar"));
  EXPECT_EQ(fs::read_symlink(extracted / "foobar"), fs::path("foo") / "bar");
  ASSERT_TRUE(subprocess::check_run(diff_bin, "-qruN", fsdata_dir, extracted));

  auto tarfile = td / "test.tar";

  ASSERT_TRUE(subprocess::check_run(dwarfsextract_bin, "-i", image, "-f",
                                    "gnutar", "-o", tarfile));

  ASSERT_TRUE(fs::create_directory(untared));
  ASSERT_TRUE(subprocess::check_run(tar_bin, "xf", tarfile, "-C", untared));
  ASSERT_TRUE(subprocess::check_run(diff_bin, "-qruN", fsdata_dir, untared));
}

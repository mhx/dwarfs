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

#include <chrono>
#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/Subprocess.h>
#include <folly/experimental/TestUtil.h>

#include "test_helpers.h"

namespace {

auto data_archive = std::filesystem::path(TEST_DATA_DIR) / "data.tar";

auto tools_dir = std::filesystem::path(TOOLS_BIN_DIR);
auto mkdwarfs_bin = tools_dir / "mkdwarfs";
auto fuse3_bin = tools_dir / "dwarfs";
auto fuse2_bin = tools_dir / "dwarfs2";
auto dwarfsextract_bin = tools_dir / "dwarfsextract";
auto dwarfsck_bin = tools_dir / "dwarfsck";

auto diff_bin = std::filesystem::path(DIFF_BIN);
auto tar_bin = std::filesystem::path(TAR_BIN);

pid_t get_dwarfs_pid(std::filesystem::path const& path) {
  std::array<char, 32> attr_buf;
  auto attr_len = ::getxattr(path.c_str(), "user.dwarfs.driver.pid",
                             attr_buf.data(), attr_buf.size());
  if (attr_len < 0) {
    throw std::runtime_error("could not read pid from xattr");
  }
  return folly::to<pid_t>(std::string_view(attr_buf.data(), attr_len));
}

template <typename... Args>
void run(Args&&... args) {
  std::vector<std::string> cmdline;
  (cmdline.emplace_back(std::forward<Args>(args)), ...);
  folly::Subprocess proc(
      cmdline, folly::Subprocess::Options().pipeStdout().pipeStderr());
  const auto [out, err] = proc.communicate();
  ASSERT_EQ(0, proc.wait().exitStatus()) << "stdout:\n"
                                         << out << "\nstderr:\n"
                                         << err;
}

class process_guard {
 public:
  process_guard() = default;

  process_guard(pid_t pid)
      : pid_{pid} {
    auto proc_dir =
        std::filesystem::path("/proc") / folly::to<std::string>(pid);
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

class driver_runner {
 public:
  template <typename... Args>
  driver_runner(std::filesystem::path const& driver,
                std::filesystem::path const& image,
                std::filesystem::path const& mountpoint, Args&&... args)
      : fusermount_{find_fusermount()}
      , mountpoint_{mountpoint} {
    run(driver, image, mountpoint, std::forward<Args>(args)...);
    auto dwarfs_pid = get_dwarfs_pid(mountpoint);
    dwarfs_guard_ = process_guard(dwarfs_pid);
  }

  ~driver_runner() {
    run(fusermount_, "-u", mountpoint_);
    EXPECT_TRUE(dwarfs_guard_.check_exit(std::chrono::seconds(5)));
  }

  static void umount(std::filesystem::path const& mountpoint) {
    run(find_fusermount(), "-u", mountpoint);
  }

 private:
  static std::filesystem::path find_fusermount() {
    auto fusermount_bin = dwarfs::test::find_binary("fusermount");
    if (!fusermount_bin) {
      fusermount_bin = dwarfs::test::find_binary("fusermount3");
    }
    if (!fusermount_bin) {
      throw std::runtime_error("no fusermount binary found");
    }
    return *fusermount_bin;
  }

  std::filesystem::path const fusermount_;
  std::filesystem::path const mountpoint_;
  process_guard dwarfs_guard_;
};

bool wait_until_file_ready(std::filesystem::path const& path,
                           std::chrono::milliseconds timeout) {
  auto end = std::chrono::steady_clock::now() + timeout;
  while (::access(path.c_str(), F_OK) != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
  }
  return true;
}

::nlink_t num_hardlinks(std::filesystem::path const& p) {
  struct ::stat buf;
  if (::stat(p.c_str(), &buf) != 0) {
    throw std::runtime_error("could not stat " + p.string());
  }
  return buf.st_nlink;
}

} // namespace

TEST(tools, everything) {
  std::chrono::seconds const timeout{5};
  folly::test::TemporaryDirectory tempdir("dwarfs");
  auto td = std::filesystem::path(tempdir.path().string());
  auto image = td / "test.dwarfs";
  auto data_dir = td / "data";

  run(tar_bin, "xf", data_archive, "-C", td);
  run(mkdwarfs_bin, "-i", data_dir, "-o", image, "--no-progress");

  ASSERT_TRUE(std::filesystem::exists(image));
  ASSERT_GT(std::filesystem::file_size(image), 1000);

  auto mountpoint = td / "mnt";
  auto extracted = td / "extracted";
  auto untared = td / "untared";

  ASSERT_TRUE(std::filesystem::create_directory(mountpoint));

  {
    std::thread driver_thread([&] { run(fuse3_bin, image, mountpoint, "-f"); });

    ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout));

    run(diff_bin, "-qruN", data_dir, mountpoint);

    EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh"));

    driver_runner::umount(mountpoint);

    driver_thread.join();
  }

  {
    driver_runner driver(fuse3_bin, image, mountpoint);

    run(diff_bin, "-qruN", data_dir, mountpoint);

    EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh"));
  }

  {
    driver_runner driver(fuse3_bin, image, mountpoint, "-o", "enable_nlink");

    run(diff_bin, "-qruN", data_dir, mountpoint);

    EXPECT_EQ(3, num_hardlinks(mountpoint / "format.sh"));
  }

  if (std::filesystem::exists(fuse2_bin)) {
    driver_runner driver(fuse2_bin, image, mountpoint);

    run(diff_bin, "-qruN", data_dir, mountpoint);

    EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh"));
  }

  auto meta_export = td / "test.meta";

  run(dwarfsck_bin, image);
  run(dwarfsck_bin, image, "--check-integrity");
  run(dwarfsck_bin, image, "--export-metadata", meta_export);

  EXPECT_GT(std::filesystem::file_size(meta_export), 1000);

  ASSERT_TRUE(std::filesystem::create_directory(extracted));

  run(dwarfsextract_bin, "-i", image, "-o", extracted);
  run(diff_bin, "-qruN", data_dir, extracted);

  auto tarfile = td / "test.tar";

  run(dwarfsextract_bin, "-i", image, "-f", "gnutar", "-o", tarfile);

  ASSERT_TRUE(std::filesystem::create_directory(untared));
  run(tar_bin, "xf", tarfile, "-C", untared);
  run(diff_bin, "-qruN", data_dir, untared);
}

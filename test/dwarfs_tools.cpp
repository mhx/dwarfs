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
#include <iostream>
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

#include <fmt/format.h>

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

void append_arg(std::vector<std::string>& args, std::string const& arg) {
  args.emplace_back(arg);
}

void append_arg(std::vector<std::string>& args,
                std::vector<std::string> const& more) {
  args.insert(args.end(), more.begin(), more.end());
}

template <typename... Args>
folly::Subprocess make_subprocess(Args&&... args) {
  std::vector<std::string> cmdline;
  (append_arg(cmdline, std::forward<Args>(args)), ...);
  return folly::Subprocess(
      cmdline, folly::Subprocess::Options().pipeStdout().pipeStderr());
}

template <typename... Args>
std::optional<std::string> check_run(Args&&... args) {
  auto proc = make_subprocess(std::forward<Args>(args)...);
  const auto [out, err] = proc.communicate();
  if (auto ec = proc.wait().exitStatus(); ec != 0) {
    std::cerr << "stdout:\n" << out << "\nstderr:\n" << err << std::endl;
    return std::nullopt;
  }
  return out;
}

class process_guard {
 public:
  process_guard() = default;

  explicit process_guard(pid_t pid)
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
  driver_runner()
      : fusermount_{find_fusermount()} {}

  template <typename... Args>
  driver_runner(std::filesystem::path const& driver,
                std::filesystem::path const& image,
                std::filesystem::path const& mountpoint, Args&&... args)
      : fusermount_{find_fusermount()}
      , mountpoint_{mountpoint} {
    if (!check_run(driver, image, mountpoint, std::forward<Args>(args)...)) {
      throw std::runtime_error("error running " + driver.string());
    }
    auto dwarfs_pid = get_dwarfs_pid(mountpoint);
    dwarfs_guard_ = process_guard(dwarfs_pid);
  }

  ~driver_runner() {
    check_run(fusermount_, "-u", mountpoint_);
    EXPECT_TRUE(dwarfs_guard_.check_exit(std::chrono::seconds(5)));
  }

  static bool umount(std::filesystem::path const& mountpoint) {
    return static_cast<bool>(check_run(find_fusermount(), "-u", mountpoint));
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

bool check_readonly(std::filesystem::path const& p, bool readonly = false) {
  struct ::stat buf;
  if (::stat(p.c_str(), &buf) != 0) {
    throw std::runtime_error("could not stat " + p.string());
  }

  bool is_writable = (buf.st_mode & S_IWUSR) != 0;

  if (is_writable == readonly) {
    std::cerr << "readonly=" << readonly
              << ", st_mode=" << fmt::format("{0:o}", buf.st_mode) << std::endl;
    return false;
  }

  if (::access(p.c_str(), W_OK) == 0) {
    // access(W_OK) should never succeed
    ::perror("access");
    return false;
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
  auto image_hdr = td / "test_hdr.dwarfs";
  auto data_dir = td / "data";
  auto header_data = data_dir / "format.sh";

  ASSERT_TRUE(check_run(tar_bin, "xf", data_archive, "-C", td));
  ASSERT_TRUE(
      check_run(mkdwarfs_bin, "-i", data_dir, "-o", image, "--no-progress"));

  ASSERT_TRUE(std::filesystem::exists(image));
  ASSERT_GT(std::filesystem::file_size(image), 1000);

  ASSERT_TRUE(check_run(mkdwarfs_bin, "-i", image, "-o", image_hdr,
                        "--no-progress", "--recompress=none", "--header",
                        header_data));

  ASSERT_TRUE(std::filesystem::exists(image_hdr));
  ASSERT_GT(std::filesystem::file_size(image_hdr), 1000);

  auto mountpoint = td / "mnt";
  auto extracted = td / "extracted";
  auto untared = td / "untared";

  ASSERT_TRUE(std::filesystem::create_directory(mountpoint));

  std::vector<std::filesystem::path> drivers;
  drivers.push_back(fuse3_bin);

  if (std::filesystem::exists(fuse2_bin)) {
    drivers.push_back(fuse2_bin);
  }

  std::vector<std::string> all_options{
      "-s",          "-oenable_nlink",   "-oreadonly",
      "-omlock=try", "-ono_cache_image", "-ocache_files",
  };

  for (auto const& driver : drivers) {
    {
      std::thread driver_thread(
          [&] { check_run(driver, image, mountpoint, "-f"); });

      ASSERT_TRUE(wait_until_file_ready(mountpoint / "format.sh", timeout));
      ASSERT_TRUE(check_run(diff_bin, "-qruN", data_dir, mountpoint));
      EXPECT_EQ(1, num_hardlinks(mountpoint / "format.sh"));

      driver_runner::umount(mountpoint);
      driver_thread.join();
    }

    {
      auto proc = make_subprocess(driver, image_hdr, mountpoint);

      const auto [out, err] = proc.communicate();

      EXPECT_NE(0, proc.wait().exitStatus()) << "stdout:\n"
                                             << out << "\nstderr:\n"
                                             << err;
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

        ASSERT_TRUE(check_run(diff_bin, "-qruN", data_dir, mountpoint));
        EXPECT_EQ(enable_nlink ? 3 : 1,
                  num_hardlinks(mountpoint / "format.sh"));
        EXPECT_TRUE(check_readonly(mountpoint / "format.sh", readonly));
      }
    }
  }

  auto meta_export = td / "test.meta";

  ASSERT_TRUE(check_run(dwarfsck_bin, image));
  ASSERT_TRUE(check_run(dwarfsck_bin, image, "--check-integrity"));
  ASSERT_TRUE(check_run(dwarfsck_bin, image, "--export-metadata", meta_export));

  {
    std::string header;
    EXPECT_TRUE(folly::readFile(header_data.c_str(), header));

    auto output = check_run(dwarfsck_bin, image_hdr, "-H");

    ASSERT_TRUE(output);

    EXPECT_EQ(header, *output);
  }

  EXPECT_GT(std::filesystem::file_size(meta_export), 1000);

  ASSERT_TRUE(std::filesystem::create_directory(extracted));

  ASSERT_TRUE(check_run(dwarfsextract_bin, "-i", image, "-o", extracted));
  ASSERT_TRUE(check_run(diff_bin, "-qruN", data_dir, extracted));

  auto tarfile = td / "test.tar";

  ASSERT_TRUE(
      check_run(dwarfsextract_bin, "-i", image, "-f", "gnutar", "-o", tarfile));

  ASSERT_TRUE(std::filesystem::create_directory(untared));
  ASSERT_TRUE(check_run(tar_bin, "xf", tarfile, "-C", untared));
  ASSERT_TRUE(check_run(diff_bin, "-qruN", data_dir, untared));
}

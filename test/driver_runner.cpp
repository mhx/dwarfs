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

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include <dwarfs/conv.h>
#include <dwarfs/xattr.h>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#else
#include <signal.h>
#ifdef __APPLE__
#include <sys/mount.h>
#endif
#endif

#include "driver_runner.h"
#include "process_guard.h"
#include "test_helpers.h"

namespace dwarfs::test {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

#ifdef __linux__
constexpr auto kTimeout = 10s;
#else
constexpr auto kTimeout = 30s;
#endif

template <typename Fn>
bool retry(Fn&& fn) {
  auto deadline = clock_type::now() + kTimeout;
  auto delay = 1ms;
  for (;;) {
    if (fn()) {
      return true;
    }
    auto now = clock_type::now();
    if (now >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(
        std::min(deadline - now,
                 std::chrono::duration_cast<clock_type::duration>(delay)));
    delay = std::min(2 * delay, 500ms);
  }
}

#ifndef _WIN32
fs::path unmount_program() {
#ifdef __APPLE__
  auto p = find_binary("diskutil");
#elif defined(__FreeBSD__)
  auto p = find_binary("umount");
#else
  auto p = find_binary("fusermount");
  if (!p) {
    p = find_binary("fusermount3");
  }
#endif
  if (!p) {
    throw std::runtime_error("no FUSE unmount program found");
  }
  return *p;
}
#endif

#ifdef __APPLE__
bool is_mounted(fs::path const& mp) {
  struct statfs buf{};
  if (::statfs(mp.c_str(), &buf) == 0) {
    return std::string_view(buf.f_fstypename) == "macfuse_dwarfs";
  }
  return errno != ENOENT && errno != ENOTDIR;
}
#endif

bool expected_exit(int code) {
#ifdef _WIN32
  return code == 0 || code == -1073741510; // STATUS_CONTROL_C_EXIT
#elif defined(__APPLE__)
  return code == 0;
#else
  return code == 0 || code == SIGINT;
#endif
}

} // namespace

class driver_runner::impl {
 public:
  explicit impl(fs::path const& mountpoint)
      : mountpoint_{mountpoint} {}

  void start(mode how, fs::path const& driver, bool tool_arg,
             fs::path const& image, subprocess::argument_list const& args) {
    subprocess::argument_list command;

    if (tool_arg) {
      command.emplace_back("--tool=dwarfs");
    }

    if (how == mode::automount) {
      command.emplace_back("--auto-mountpoint");
      command.push_back(image);
    } else {
      if (fs::exists(mountpoint_)) {
        fs::remove(mountpoint_);
      }
#ifndef _WIN32
      fs::create_directory(mountpoint_);
#endif
      command.push_back(image);
      command.push_back(mountpoint_);
    }

#ifndef _WIN32
    if (how != mode::normal) {
      command.emplace_back("-f");
    }
#endif

    command.insert(command.end(), args.begin(), args.end());

#ifdef DWARFS_CROSSCOMPILING_EMULATOR
    command.insert(command.begin(), driver);
    process_ = std::make_unique<subprocess>(
        fs::path(DWARFS_CROSSCOMPILING_EMULATOR), command);
#else
    process_ = std::make_unique<subprocess>(driver, command);
#endif

    cmdline_ = process_->cmdline();
    active_ = true;

#ifndef _WIN32
    if (how == mode::normal) {
      process_->wait();
      errs_ = process_->err();
      if (process_->exit_code() != 0) {
        active_ = false;
        throw std::runtime_error("FUSE launcher failed: " + cmdline_ + "\n" +
                                 process_->out() + errs_);
      }
      process_.reset();
      if (!retry([&] {
            std::error_code ec;
            auto pid =
                dwarfs::getxattr(mountpoint_, "user.dwarfs.driver.pid", ec);
            if (ec) {
              return false;
            }
            daemon_ = std::make_unique<process_guard>(dwarfs::to<pid_t>(pid));
            return true;
          })) {
        throw std::runtime_error("could not obtain FUSE daemon PID: " +
                                 cmdline_);
      }
    }
#endif
  }

  bool wait_until_ready(fs::path const& path) {
    bool exited = false;
    bool const ready = retry([&] {
      if (process_ && process_->wait_for(0ms)) {
        std::cerr << "driver exited before readiness: " << process_->exit_code()
                  << '\n'
                  << process_->out() << process_->err();
        exited = true;
        return true; // stop retrying
      }

#ifndef _WIN32
      if (daemon_ && daemon_->wait_for(0ms)) {
        std::cerr << "daemon exited before readiness: " << cmdline_ << '\n';
        exited = true;
        return true;
      }
#endif
      std::error_code ec;
      return fs::exists(path, ec);
    });

    if (!ready) {
      std::cerr << path << " not ready: " << cmdline_ << '\n';
    }

    return ready && !exited;
  }

#ifndef _WIN32

  bool external_unmount() {
    auto program = unmount_program();
    return retry([&] {
#ifdef __APPLE__
      if (!is_mounted(mountpoint_)) {
        return true;
      }
      subprocess p(program, "unmount", mountpoint_);
#elif defined(__FreeBSD__)
      subprocess p(program, mountpoint_);
#else
      subprocess p(program, "-u", mountpoint_);
#endif
      if (!p.wait_for(kTimeout)) {
        std::cerr << "unmount command timed out: " << p.cmdline() << '\n';
        p.terminate();
        return false;
      }
      if (p.exit_code() == 0) {
        return true;
      }
      std::cerr << "unmount failed: " << p.cmdline() << '\n'
                << p.out() << p.err() << "exit code: " << p.exit_code() << '\n';
      return false;
    });
  }

#endif // !defined(_WIN32)

  bool unmount() {
    if (!active_) {
      return true;
    }
    bool success = true;
#if !defined(_WIN32) && !defined(__APPLE__)
    bool const had_child = process_ != nullptr;
    bool forced = false;
#endif

#ifdef __APPLE__
    success = external_unmount();
#else
#ifndef _WIN32
    if (!process_) {
      success = external_unmount();
    }
#endif
#endif

    if (process_) {
#ifdef __APPLE__
      if (!success)
#endif
      {
        if (!process_->interrupt()) {
          std::cerr << "could not interrupt: " << cmdline_ << '\n';
        }
      }

      if (!process_->wait_for(kTimeout)) {
        std::cerr << "driver failed to exit: " << cmdline_ << '\n';
        // Try the native unmount utility before resorting to a hard kill.
#ifndef _WIN32
#ifndef __APPLE__
        external_unmount();
        if (!process_->wait_for(kTimeout))
#endif
#endif
        {
          process_->terminate();
#if !defined(_WIN32) && !defined(__APPLE__)
          forced = true;
#endif
        }

        success = false;
      }

      errs_ = process_->err();

      if (!expected_exit(process_->exit_code())) {
        std::cerr << "driver exit code: " << process_->exit_code() << '\n'
                  << process_->out() << errs_;
        success = false;
      }

      process_.reset();
    }

#ifndef _WIN32
    if (daemon_) {
      if (!daemon_->wait_for(kTimeout)) {
        std::cerr << "daemon did not exit: " << cmdline_ << '\n';
        success = false;
      } else {
        daemon_.reset();
      }
    }
#endif

#ifdef __APPLE__
    if (!retry([&] { return !is_mounted(mountpoint_); })) {
      success = false;
    }
#endif

#ifdef __APPLE__
    active_ = is_mounted(mountpoint_) || daemon_ != nullptr;
#elif defined(_WIN32)
    active_ = false;
#else
    active_ = daemon_ != nullptr || (!success && !had_child) || forced;
#endif
    return success;
  }

  void cleanup() noexcept {
    if (active_) {
      try {
        if (!unmount()) {
          ADD_FAILURE() << "FUSE cleanup failed: " << cmdline_;
        }
      } catch (std::exception const& e) {
        ADD_FAILURE() << "FUSE cleanup failed: " << cmdline_ << ": "
                      << e.what();
      } catch (...) {
        ADD_FAILURE() << "FUSE cleanup failed: " << cmdline_;
      }
    }
  }

  fs::path mountpoint_;
  std::unique_ptr<subprocess> process_;
#ifndef _WIN32
  std::unique_ptr<process_guard> daemon_;
#endif
  std::string cmdline_, errs_;
  bool active_{false};
};

driver_runner::driver_runner(mode how, fs::path const& driver, bool tool_arg,
                             fs::path const& image, fs::path const& mountpoint,
                             subprocess::argument_list const& args)
    : impl_{std::make_unique<impl>(mountpoint)} {
  try {
    impl_->start(how, driver, tool_arg, image, args);
  } catch (...) {
    impl_->cleanup();
    throw;
  }
}

driver_runner::~driver_runner() { impl_->cleanup(); }

bool driver_runner::wait_until_ready(fs::path const& path) {
  return impl_->wait_until_ready(path);
}

bool driver_runner::unmount() { return impl_->unmount(); }

std::string const& driver_runner::cmdline() const { return impl_->cmdline_; }

std::string const& driver_runner::err() const { return impl_->errs_; }

} // namespace dwarfs::test

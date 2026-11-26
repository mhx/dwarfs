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

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#else
#include <poll.h>
#include <sys/syscall.h>
#endif

#include "process_guard.h"

namespace dwarfs::test {

namespace {

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

[[noreturn]] void fail(char const* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

class unique_fd {
 public:
  unique_fd() = default;

  explicit unique_fd(int fd)
      : fd_{fd} {}

  ~unique_fd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  unique_fd(unique_fd const&) = delete;
  unique_fd& operator=(unique_fd const&) = delete;

  void reset(int fd) { fd_ = fd; } // initially empty
  int get() const { return fd_; }

 private:
  int fd_{-1};
};

} // namespace

class process_guard::impl {
 public:
  explicit impl(pid_t pid) {
    if (pid <= 0) {
      throw std::invalid_argument("invalid daemon PID");
    }

#if defined(__APPLE__) || defined(__FreeBSD__)

    fd_.reset(::kqueue());

    if (fd_.get() < 0) {
      fail("kqueue");
    }

    if (::fcntl(fd_.get(), F_SETFD, FD_CLOEXEC) < 0) {
      fail("fcntl");
    }

    struct kevent event{};

    EV_SET(&event, static_cast<uintptr_t>(pid), EVFILT_PROC,
           EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);

    if (::kevent(fd_.get(), &event, 1, nullptr, 0, nullptr) < 0) {
      if (errno == ESRCH) {
        exited_ = true;
      } else {
        fail("kevent registration");
      }
    }

#else // !(defined(__APPLE__) || defined(__FreeBSD__))

#ifdef SYS_pidfd_open
    fd_.reset(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));

    if (fd_.get() >= 0) {
      pidfd_ = true;
      return;
    }

    if (errno == ESRCH) {
      exited_ = true;
      return;
    }
#endif

    auto path = "/proc/" + std::to_string(pid);

    fd_.reset(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));

    if (fd_.get() < 0) {
      if (errno == ENOENT || errno == ESRCH) {
        exited_ = true;
      } else {
        fail("open daemon proc directory");
      }
    }

#endif // defined(__APPLE__) || defined(__FreeBSD__)
  }

  bool wait_for(std::chrono::milliseconds timeout) {
    auto deadline = clock_type::now() + timeout;

    while (!exited_) {
      auto remaining =
          std::max(0ms, std::chrono::ceil<std::chrono::milliseconds>(
                            deadline - clock_type::now()));

#if defined(__APPLE__) || defined(__FreeBSD__)

      struct timespec ts{
          static_cast<time_t>(remaining.count() / 1000),
          static_cast<long>((remaining.count() % 1000) * 1000000)};
      struct kevent event{};
      auto n = ::kevent(fd_.get(), nullptr, 0, &event, 1, &ts);

      if (n < 0 && errno != EINTR) {
        fail("kevent wait");
      }

      if (n > 0) {
        if (event.flags & EV_ERROR) {
          throw std::system_error(static_cast<int>(event.data),
                                  std::generic_category(), "kevent event");
        }
        exited_ = event.filter == EVFILT_PROC && (event.fflags & NOTE_EXIT);
      }

#else

      if (pidfd_) {
        struct pollfd pfd{fd_.get(), POLLIN, 0};
        auto n =
            ::poll(&pfd, 1,
                   static_cast<int>(std::min<int64_t>(
                       remaining.count(), std::numeric_limits<int>::max())));

        if (n < 0 && errno != EINTR) {
          fail("poll pidfd");
        }

        if (n > 0) {
          if (pfd.revents & (POLLERR | POLLNVAL)) {
            throw std::runtime_error("invalid daemon pidfd poll event");
          }
          exited_ = (pfd.revents & (POLLIN | POLLHUP)) != 0;
        }
      } else {
        unique_fd statfd{::openat(fd_.get(), "stat", O_RDONLY | O_CLOEXEC)};

        if (statfd.get() < 0) {
          if (errno == ENOENT || errno == ESRCH) {
            exited_ = true;
          } else if (errno != EINTR) {
            fail("open daemon stat");
          }
        } else {
          char buffer[4096];
          auto n = ::read(statfd.get(), buffer, sizeof(buffer));

          if (n < 0) {
            if (errno == ESRCH || errno == ENOENT) {
              exited_ = true;
            } else if (errno != EINTR) {
              fail("read daemon stat");
            }
          } else if (n == 0) {
            exited_ = true;
          } else {
            std::string stat(buffer, static_cast<size_t>(n));
            auto pos = stat.rfind(')');
            if (pos == std::string::npos || pos + 2 >= stat.size()) {
              throw std::runtime_error("invalid daemon stat");
            }
            exited_ = stat[pos + 2] == 'Z' || stat[pos + 2] == 'X';
          }
        }

        if (!exited_) {
          std::this_thread::sleep_for(std::min(remaining, 10ms));
        }
      }

#endif

      if (clock_type::now() >= deadline) {
        break;
      }
    }

    return exited_;
  }

 private:
  unique_fd fd_;
  bool exited_{false};
#if !defined(__APPLE__) && !defined(__FreeBSD__)
  bool pidfd_{false};
#endif
};

process_guard::process_guard(pid_t pid)
    : impl_{std::make_unique<impl>(pid)} {}

process_guard::~process_guard() = default;

bool process_guard::wait_for(std::chrono::milliseconds timeout) {
  return impl_->wait_for(timeout);
}

} // namespace dwarfs::test

#endif

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include <dwarfs/scoped_output_capture.h>

#ifdef _WIN32
#define close(fd) _close(fd)
#define dup(fd) _dup(fd)
#define dup2(from, to) _dup2(from, to)
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#define read(fd, buf, count) _read(fd, buf, static_cast<unsigned>(count))
#endif

namespace dwarfs {

namespace {

constexpr bool wants_stdout(scoped_output_capture::capture c) {
  return std::to_underlying(c) &
         std::to_underlying(scoped_output_capture::capture::stdout_only);
}

constexpr bool wants_stderr(scoped_output_capture::capture c) {
  return std::to_underlying(c) &
         std::to_underlying(scoped_output_capture::capture::stderr_only);
}

void flush_all() {
  std::fflush(nullptr);

  std::cout.flush();
  std::cerr.flush();

  std::wcout.flush();
  std::wcerr.flush();
}

[[noreturn]] void throw_errno(char const* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

int dup_fd_or_throw(int fd, char const* what) {
  int r = ::dup(fd);
  if (r == -1) {
    throw_errno(what);
  }
  return r;
}

void dup2_or_throw(int from, int to, char const* what) {
  if (::dup2(from, to) == -1) {
    throw_errno(what);
  }
}

void close_fd_noexcept(int& fd) noexcept {
  if (fd != -1) {
    ::close(fd);
    fd = -1;
  }
}

void close_target_fd_noexcept(int fd) noexcept {
  if (fd != -1) {
    ::close(fd);
  }
}

void close_fd_or_throw(int& fd, char const* what) {
  if (fd == -1) {
    return;
  }
  if (::close(fd) == -1) {
    throw_errno(what);
  }
  fd = -1;
}

#ifdef _WIN32
[[noreturn]] void throw_last_error(char const* what) {
  throw std::system_error(static_cast<int>(::GetLastError()),
                          std::system_category(), what);
}
#endif

class capture_channel {
 public:
#ifdef _WIN32
  capture_channel(int target_fd, DWORD std_handle_id)
      : target_fd_{target_fd}
      , std_handle_id_ {
    std_handle_id
  }
#else
  explicit capture_channel(int target_fd)
      : target_fd_{target_fd}
#endif
  {
    if (target_fd_ < 0) {
      throw std::runtime_error("target stream has no valid file descriptor");
    }

    flush_all();

    saved_fd_ = dup_fd_or_throw(target_fd_, "saving original fd failed");

#ifdef _WIN32
    saved_std_handle_ = ::GetStdHandle(std_handle_id_);
    if (saved_std_handle_ == INVALID_HANDLE_VALUE) {
      close_fd_noexcept(saved_fd_);
      throw_last_error("GetStdHandle failed");
    }
#endif

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
      close_fd_noexcept(saved_fd_);
      throw_errno("_pipe failed");
    }

    read_fd_ = pipe_fds[0];
    int write_fd = pipe_fds[1];

    try {
      dup2_or_throw(write_fd, target_fd_, "redirecting fd failed");

#ifdef _WIN32
      HANDLE redirected =
          reinterpret_cast<HANDLE>(::_get_osfhandle(target_fd_));
      if (redirected == INVALID_HANDLE_VALUE) {
        throw_errno("_get_osfhandle failed");
      }

      if (!::SetStdHandle(std_handle_id_, redirected)) {
        throw_last_error("SetStdHandle failed");
      }
#endif

      close_fd_or_throw(write_fd, "closing temporary pipe write fd failed");
      reader_ = std::thread(&capture_channel::reader_main, this);
    } catch (...) {
      close_fd_noexcept(write_fd);
      rollback_startup_noexcept();
      throw;
    }
  }

  capture_channel(capture_channel const&) = delete;
  capture_channel& operator=(capture_channel const&) = delete;

  capture_channel(capture_channel&&) = delete;
  capture_channel& operator=(capture_channel&&) = delete;

  ~capture_channel() noexcept {
    try {
      stop();
    } catch (...) {
    }

    if (reader_.joinable()) {
      reader_.join();
    }

    close_fd_noexcept(saved_fd_);
    close_fd_noexcept(read_fd_);
  }

  void stop() {
    if (stopped_) {
      return;
    }

    std::exception_ptr first_error;

    flush_all();

#ifdef _WIN32
    if (saved_std_handle_ != INVALID_HANDLE_VALUE) {
      if (!::SetStdHandle(std_handle_id_, saved_std_handle_)) {
        first_error = std::current_exception();
        try {
          throw_last_error("SetStdHandle(restore) failed");
        } catch (...) {
          first_error = std::current_exception();
        }
      }
    }
#endif

    try {
      dup2_or_throw(saved_fd_, target_fd_, "restoring original stream failed");
    } catch (...) {
      if (!first_error) {
        first_error = std::current_exception();
      }
      close_target_fd_noexcept(target_fd_);
    }

    try {
      close_fd_or_throw(saved_fd_, "closing saved fd failed");
    } catch (...) {
      if (!first_error) {
        first_error = std::current_exception();
      }
    }

    if (reader_.joinable()) {
      reader_.join();
    }

    try {
      close_fd_or_throw(read_fd_, "closing pipe read fd failed");
    } catch (...) {
      if (!first_error) {
        first_error = std::current_exception();
      }
    }

    stopped_ = true;

    if (reader_error_ && !first_error) {
      first_error = reader_error_;
    }

    if (first_error) {
      std::rethrow_exception(first_error);
    }
  }

  std::string const& captured() const noexcept { return buffer_; }

 private:
  void rollback_startup_noexcept() noexcept {
#ifdef _WIN32
    if (saved_std_handle_ != INVALID_HANDLE_VALUE) {
      ::SetStdHandle(std_handle_id_, saved_std_handle_);
    }
#endif
    if (saved_fd_ != -1) {
      ::dup2(saved_fd_, target_fd_);
    }
    close_fd_noexcept(saved_fd_);
    close_fd_noexcept(read_fd_);
  }

  void reader_main() {
    std::array<char, 4096> tmp;

    try {
      for (;;) {
        auto const n = ::read(read_fd_, tmp.data(), tmp.size());
        if (n > 0) {
          buffer_.append(tmp.data(), static_cast<std::size_t>(n));
          continue;
        }
        if (n == 0) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        reader_error_ = std::make_exception_ptr(
            std::system_error(errno, std::generic_category(), "read failed"));
        break;
      }
    } catch (...) {
      reader_error_ = std::current_exception();
    }
  }

  int target_fd_ = -1;
  int saved_fd_ = -1;
  int read_fd_ = -1;
  std::thread reader_;
  std::string buffer_;
  std::exception_ptr reader_error_;
  bool stopped_ = false;

#ifdef _WIN32
  DWORD std_handle_id_ = 0;
  HANDLE saved_std_handle_ = INVALID_HANDLE_VALUE;
#endif
};

} // namespace

class scoped_output_capture::impl {
 public:
  explicit impl(capture which) {
    if (wants_stdout(which)) {
#ifdef _WIN32
      stdout_.emplace(_fileno(stdout), STD_OUTPUT_HANDLE);
#else
      stdout_.emplace(STDOUT_FILENO);
#endif
    }

    if (wants_stderr(which)) {
#ifdef _WIN32
      stderr_.emplace(_fileno(stderr), STD_ERROR_HANDLE);
#else
      stderr_.emplace(STDERR_FILENO);
#endif
    }
  }

  void stop() {
    std::exception_ptr first_error;

    auto stop_one = [&](std::optional<capture_channel>& channel) {
      if (!channel) {
        return;
      }
      try {
        channel->stop();
      } catch (...) {
        if (!first_error) {
          first_error = std::current_exception();
        }
      }
    };

    stop_one(stdout_);
    stop_one(stderr_);

    if (first_error) {
      std::rethrow_exception(first_error);
    }
  }

  std::string const& captured_stdout() const {
    if (!stdout_) {
      throw std::logic_error("stdout is not being captured");
    }
    return stdout_->captured();
  }

  std::string const& captured_stderr() const {
    if (!stderr_) {
      throw std::logic_error("stderr is not being captured");
    }
    return stderr_->captured();
  }

  std::string const& captured() const {
    if (stdout_ && stderr_) {
      throw std::logic_error("both stdout and stderr are being captured");
    }
    if (stdout_) {
      return stdout_->captured();
    }
    if (stderr_) {
      return stderr_->captured();
    }
    throw std::logic_error("neither stdout nor stderr is being captured");
  }

 private:
  std::optional<capture_channel> stdout_;
  std::optional<capture_channel> stderr_;
};

scoped_output_capture::scoped_output_capture(capture which)
    : impl_(std::make_unique<impl>(which)) {}

scoped_output_capture::~scoped_output_capture() = default;

scoped_output_capture::scoped_output_capture(scoped_output_capture&&) noexcept =
    default;
scoped_output_capture&
scoped_output_capture::operator=(scoped_output_capture&&) noexcept = default;

void scoped_output_capture::stop() { impl_->stop(); }

std::string const& scoped_output_capture::captured_stdout() {
  stop();
  return impl_->captured_stdout();
}

std::string const& scoped_output_capture::captured_stderr() {
  stop();
  return impl_->captured_stderr();
}

std::string const& scoped_output_capture::captured() {
  stop();
  return impl_->captured();
}

} // namespace dwarfs

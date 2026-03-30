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
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <dwarfs/scoped_output_capture.h>

#include <dwarfs/internal/scoped_output_capture_test_api.h>

#ifndef _WIN32
namespace {

using posix_ops = dwarfs::internal::test::scoped_output_capture_posix_ops;

int real_close(int fd) { return ::close(fd); }
int real_dup(int fd) { return ::dup(fd); }
int real_dup2(int from, int to) { return ::dup2(from, to); }

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
int real_pipe(int fds[2]) { return ::pipe(fds); }
ssize_t real_read(int fd, void* buf, std::size_t count) {
  return ::read(fd, buf, count);
}

posix_ops const real_ops{
    real_close, real_dup, real_dup2, real_pipe, real_read,
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
posix_ops const* g_ops = &real_ops;

} // namespace

namespace dwarfs::internal::test {

scoped_output_capture_posix_ops real_scoped_output_capture_posix_ops() {
  return real_ops;
}

scoped_output_capture_posix_ops_override::
    scoped_output_capture_posix_ops_override(
        scoped_output_capture_posix_ops ops)
    : ops_{std::move(ops)}
    , previous_{g_ops} {
  g_ops = &ops_;
}

scoped_output_capture_posix_ops_override::
    ~scoped_output_capture_posix_ops_override() {
  g_ops = previous_;
}

} // namespace dwarfs::internal::test

#endif

namespace dwarfs {

namespace {

using stream = scoped_output_capture::stream;

#ifdef _WIN32
int os_close(int fd) { return ::_close(fd); }
int os_dup(int fd) { return ::_dup(fd); }
int os_dup2(int from, int to) { return ::_dup2(from, to); }
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
int os_pipe(int fds[2]) { return ::_pipe(fds, 4096, _O_BINARY); }
int os_read(int fd, void* buf, std::size_t count) {
  return ::_read(fd, buf, static_cast<unsigned>(count));
}
#else
int os_close(int fd) { return g_ops->close_fn(fd); }
int os_dup(int fd) { return g_ops->dup_fn(fd); }
int os_dup2(int from, int to) { return g_ops->dup2_fn(from, to); }
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
int os_pipe(int fds[2]) { return g_ops->pipe_fn(fds); }
ssize_t os_read(int fd, void* buf, std::size_t count) {
  return g_ops->read_fn(fd, buf, count);
}
#endif

void flush_stream(stream which) {
  switch (which) {
  case stream::std_out:
    // NOLINTNEXTLINE(cert-err33-c)
    std::fflush(stdout);
    std::cout.flush();
    std::wcout.flush();
    break;

  case stream::std_err:
    // NOLINTNEXTLINE(cert-err33-c)
    std::fflush(stderr);
    std::cerr.flush();
    std::clog.flush();
    std::wcerr.flush();
    std::wclog.flush();
    break;
  }
}

[[noreturn]] void throw_errno(char const* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

#ifdef _WIN32
[[noreturn]] void throw_last_error(char const* what) {
  throw std::system_error(static_cast<int>(::GetLastError()),
                          std::system_category(), what);
}
#endif

int dup_fd_or_throw(int fd, char const* what) {
  int const r = os_dup(fd);
  if (r == -1) {
    throw_errno(what);
  }
  return r;
}

void dup2_or_throw(int from, int to, char const* what) {
  if (os_dup2(from, to) == -1) {
    throw_errno(what);
  }
}

void close_fd_noexcept(int& fd) noexcept {
  if (fd != -1) {
    os_close(fd);
    fd = -1;
  }
}

void close_raw_fd_noexcept(int fd) noexcept {
  if (fd != -1) {
    os_close(fd);
  }
}

template <class F>
void capture_first_exception(std::exception_ptr& ep, F&& fn) noexcept {
  try {
    std::forward<F>(fn)();
  } catch (...) {
    if (!ep) {
      ep = std::current_exception();
    }
  }
}

#ifdef _WIN32
DWORD std_handle_id_for_stream(stream which) {
  switch (which) {
  case stream::std_out:
    return STD_OUTPUT_HANDLE;
  case stream::std_err:
    return STD_ERROR_HANDLE;
  }
  throw std::invalid_argument("invalid stream selection");
}
#endif

int target_fd_for_stream(stream which) {
  switch (which) {
  case stream::std_out:
#ifdef _WIN32
    return _fileno(stdout);
#else
    return STDOUT_FILENO;
#endif

  case stream::std_err:
#ifdef _WIN32
    return _fileno(stderr);
#else
    return STDERR_FILENO;
#endif
  }

  throw std::invalid_argument("invalid stream selection");
}

class capture_channel {
 public:
  explicit capture_channel(stream which)
      : which_{which}
      , target_fd_{target_fd_for_stream(which)}
#ifdef _WIN32
      , std_handle_id_{std_handle_id_for_stream(which)}
#endif
  {
    if (target_fd_ < 0) {
      throw std::runtime_error("target stream has no valid file descriptor");
    }

    flush_stream(which_);

    saved_fd_ = dup_fd_or_throw(target_fd_, "saving original fd failed");

#ifdef _WIN32
    saved_std_handle_ = ::GetStdHandle(std_handle_id_);
    if (saved_std_handle_ == INVALID_HANDLE_VALUE) {
      close_fd_noexcept(saved_fd_);
      throw_last_error("GetStdHandle failed");
    }
#endif

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    int pipe_fds[2] = {-1, -1};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if (os_pipe(pipe_fds) != 0) {
      close_fd_noexcept(saved_fd_);
      throw_errno("pipe failed");
    }

    read_fd_ = pipe_fds[0];
    int write_fd = pipe_fds[1];

    try {
      dup2_or_throw(write_fd, target_fd_, "redirecting fd failed");

#ifdef _WIN32
      auto const redirected =
          reinterpret_cast<HANDLE>(::_get_osfhandle(target_fd_));
      if (redirected == INVALID_HANDLE_VALUE) {
        throw_errno("_get_osfhandle failed");
      }

      if (!::SetStdHandle(std_handle_id_, redirected)) {
        throw_last_error("SetStdHandle failed");
      }
#endif

      if (os_close(write_fd) == -1) {
        throw_errno("closing temporary pipe write fd failed");
      }
      write_fd = -1;

      reader_ = std::thread(&capture_channel::reader_main, this);
    } catch (...) {
      if (write_fd != -1) {
        os_close(write_fd);
      }
#ifdef _WIN32
      if (saved_std_handle_ != INVALID_HANDLE_VALUE) {
        ::SetStdHandle(std_handle_id_, saved_std_handle_);
      }
#endif

      if (saved_fd_ != -1) {
        os_dup2(saved_fd_, target_fd_);
      }

      close_fd_noexcept(saved_fd_);
      close_fd_noexcept(read_fd_);
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
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }

  void stop() {
    if (std::exchange(stopped_, true)) {
      return;
    }

    std::exception_ptr first_error;

    flush_stream(which_);

#ifdef _WIN32
    capture_first_exception(first_error, [&] {
      if (!::SetStdHandle(std_handle_id_, saved_std_handle_)) {
        throw_last_error("SetStdHandle(restore) failed");
      }
    });
#endif

    bool restored = false;
    capture_first_exception(first_error, [&] {
      dup2_or_throw(saved_fd_, target_fd_, "restoring original stream failed");
      restored = true;
    });

    if (!restored) {
      close_raw_fd_noexcept(target_fd_);
    }

    close_fd_noexcept(saved_fd_);

    reader_.join();

    close_fd_noexcept(read_fd_);

    if (reader_error_ && !first_error) {
      first_error = reader_error_;
    }

    if (first_error) {
      std::rethrow_exception(first_error);
    }
  }

  std::string const& captured() const noexcept { return buffer_; }

 private:
  void reader_main() {
    std::array<char, 4096> tmp{};

    try {
      for (;;) {
        auto const n = os_read(read_fd_, tmp.data(), tmp.size());

        if (n > 0) {
          buffer_.append(tmp.data(), static_cast<std::size_t>(n));
          continue;
        }

        if (n == 0) {
          break;
        }

        if (errno != EINTR) {
          throw_errno("read failed");
        }
      }
    } catch (...) {
      reader_error_ = std::current_exception();
    }
  }

  stream which_;
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
  explicit impl(stream which)
      : channel_{which} {}

  void stop() { channel_.stop(); }

  std::string const& captured() const { return channel_.captured(); }

 private:
  capture_channel channel_;
};

scoped_output_capture::scoped_output_capture(stream which)
    : impl_(std::make_unique<impl>(which)) {}

scoped_output_capture::~scoped_output_capture() = default;

scoped_output_capture::scoped_output_capture(scoped_output_capture&&) noexcept =
    default;

scoped_output_capture&
scoped_output_capture::operator=(scoped_output_capture&&) noexcept = default;

void scoped_output_capture::stop() { impl_->stop(); }

std::string const& scoped_output_capture::captured() {
  stop();
  return impl_->captured();
}

} // namespace dwarfs

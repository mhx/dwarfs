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

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <dwarfs/portability/windows.h>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <dwarfs/conv.h>
#include <dwarfs/file_util.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

fs::path make_tempdir_path(std::string_view prefix) {
  static thread_local boost::uuids::random_generator gen;
  auto dirname = boost::uuids::to_string(gen());
  if (!prefix.empty()) {
    dirname = std::string(prefix) + '.' + dirname;
  }
  return fs::temp_directory_path() / dirname;
}

bool keep_temporary_directories() {
  static bool keep = getenv_is_enabled("DWARFS_KEEP_TEMPORARY_DIRECTORIES");
  return keep;
}

std::error_code get_last_error() {
#ifdef _WIN32
  return {static_cast<int>(::GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}

#ifdef _WIN32

class file_handle {
 public:
  explicit file_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept
      : h_(h) {}

  file_handle(file_handle const&) = delete;
  file_handle& operator=(file_handle const&) = delete;

  file_handle(file_handle&& other) noexcept
      : h_(std::exchange(other.h_, INVALID_HANDLE_VALUE)) {}

  file_handle& operator=(file_handle&& other) noexcept {
    if (this != &other) {
      reset();
      h_ = std::exchange(other.h_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  ~file_handle() { reset(); }

  [[nodiscard]] bool valid() const noexcept {
    return h_ != INVALID_HANDLE_VALUE;
  }

  [[nodiscard]] HANDLE get() const noexcept { return h_; }

  void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
    if (valid()) {
      ::CloseHandle(h_);
    }
    h_ = h;
  }

 private:
  HANDLE h_;
};

#else

class file_descriptor {
 public:
  explicit file_descriptor(int value = -1) noexcept
      : value_(value) {}

  file_descriptor(file_descriptor const&) = delete;
  file_descriptor& operator=(file_descriptor const&) = delete;

  file_descriptor(file_descriptor&& other) noexcept
      : value_(std::exchange(other.value_, -1)) {}

  file_descriptor& operator=(file_descriptor&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }

  ~file_descriptor() { reset(); }

  [[nodiscard]] bool valid() const noexcept { return value_ >= 0; }

  [[nodiscard]] int get() const noexcept { return value_; }

  void reset(int value = -1) noexcept {
    if (valid()) {
      ::close(value_);
    }
    value_ = value;
  }

 private:
  int value_;
};

#endif

} // namespace

temporary_directory::temporary_directory()
    : temporary_directory(std::string_view{}) {}

temporary_directory::temporary_directory(std::string_view prefix)
    : path_{make_tempdir_path(prefix)} {
  fs::create_directory(path_);
}

temporary_directory::~temporary_directory() {
  if (!keep_temporary_directories()) {
    std::error_code ec;
    fs::remove_all(path_, ec);
    if (ec) {
      std::cerr << "Failed to remove temporary directory " << path_ << ": "
                << ec.message() << "\n";
    }
  }
}

std::string read_file(std::filesystem::path const& path, std::error_code& ec) {
  static constexpr std::size_t kBufferSize = 4096;

  ec.clear();

  std::string result;
  std::array<char, kBufferSize> buffer;

#ifdef _WIN32
  file_handle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 nullptr));

  if (!file.valid()) {
    goto error;
  }

  for (;;) {
    DWORD read = 0;

    if (!::ReadFile(file.get(), buffer.data(),
                    static_cast<DWORD>(buffer.size()), &read, nullptr)) {
      goto error;
    }

    if (read == 0) {
      break;
    }

    result.append(buffer.data(), read);
  }
#else
  file_descriptor file(::open(path.c_str(), O_RDONLY));

  if (!file.valid()) {
    goto error;
  }

  for (;;) {
    auto const n = ::read(file.get(), buffer.data(), buffer.size());

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      goto error;
    }

    if (n == 0) {
      break;
    }

    result.append(buffer.data(), static_cast<std::size_t>(n));
  }
#endif

  goto success;

error:
  result.clear();
  ec = get_last_error();

success:
  return result;
}

std::string read_file(std::filesystem::path const& path) {
  std::error_code ec;
  auto content = read_file(path, ec);
  if (ec) {
    throw std::system_error(ec, "read_file");
  }
  return content;
}

void write_file(std::filesystem::path const& path, std::string_view content,
                std::error_code& ec) {
  ec.clear();

#ifdef _WIN32
  file_handle file(::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                 nullptr));

  if (!file.valid()) {
    ec = get_last_error();
    return;
  }

  std::size_t offset = 0;

  while (offset < content.size()) {
    auto const remaining = content.size() - offset;
    auto const chunk =
        std::min<DWORD>(remaining, std::numeric_limits<DWORD>::max());

    DWORD written = 0;

    if (!::WriteFile(file.get(), content.data() + offset, chunk, &written,
                     nullptr)) {
      ec = get_last_error();
      return;
    }

    if (written == 0) {
      ec = std::make_error_code(std::errc::io_error);
      return;
    }

    offset += written;
  }

#else
  file_descriptor file(
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666));

  if (!file.valid()) {
    ec = get_last_error();
    return;
  }

  std::size_t offset = 0;
  while (offset < content.size()) {
    auto const remaining = content.size() - offset;
    auto const n = ::write(file.get(), content.data() + offset, remaining);

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ec = get_last_error();
      return;
    }

    if (n == 0) {
      ec = std::make_error_code(std::errc::io_error);
      return;
    }

    offset += static_cast<std::size_t>(n);
  }
#endif
}

void write_file(std::filesystem::path const& path, std::string_view content) {
  std::error_code ec;
  write_file(path, content, ec);
  if (ec) {
    throw std::system_error(ec, "write_file");
  }
}

} // namespace dwarfs

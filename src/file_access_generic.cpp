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

#include <cerrno>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include <dwarfs/file_access.h>
#include <dwarfs/file_access_generic.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace {

void assign_error_code(std::error_code& ec) {
  ec.assign(errno, std::generic_category());
}

template <typename Base, typename Stream>
class file_stream_base : public Base {
 public:
  file_stream_base(std::filesystem::path const& path,
                   std::ios_base::openmode mode, std::error_code& ec)
      : stream_{path, mode} {
    if (path.empty()) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return;
    }
    if (stream_.bad() || stream_.fail() || !stream_.is_open()) {
      assign_error_code(ec);
    }
  }

  void close(std::error_code& ec) override {
    stream_.close();
    if (stream_.bad()) {
      assign_error_code(ec);
    }
  }

  void close() override {
    std::error_code ec;
    close(ec);
    if (ec) {
      throw std::system_error(ec, "close()");
    }
  }

 protected:
  Stream& stream() { return stream_; }

 private:
  Stream stream_;
};

class file_input_stream : public file_stream_base<input_stream, std::ifstream> {
 public:
  using file_stream_base<input_stream, std::ifstream>::file_stream_base;

  std::istream& is() override { return stream(); }
};

class file_output_stream
    : public file_stream_base<output_stream, std::ofstream> {
 public:
  using file_stream_base<output_stream, std::ofstream>::file_stream_base;

  std::ostream& os() override { return stream(); }
};

class file_input_output_stream
    : public file_stream_base<input_output_stream, std::fstream> {
 public:
  using file_stream_base<input_output_stream, std::fstream>::file_stream_base;

  std::iostream& ios() override { return stream(); }
};

class file_access_generic : public file_access {
 public:
  bool exists(std::filesystem::path const& path) const override {
    return std::filesystem::exists(path);
  }

  std::unique_ptr<input_stream> open_input(std::filesystem::path const& path,
                                           std::error_code& ec) const override {
    ec.clear();
    auto rv = std::make_unique<file_input_stream>(path, std::ios::in, ec);
    if (ec) {
      rv.reset();
    }
    return rv;
  }

  std::unique_ptr<input_stream>
  open_input(std::filesystem::path const& path) const override {
    std::error_code ec;
    auto rv = open_input(path, ec);
    if (ec) {
      throw std::system_error(ec,
                              fmt::format("open_input('{}')", path.string()));
    }
    return rv;
  }

  std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path,
                    std::error_code& ec) const override {
    ec.clear();
    auto rv = std::make_unique<file_input_stream>(path, std::ios::binary, ec);
    if (ec) {
      rv.reset();
    }
    return rv;
  }

  std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path) const override {
    std::error_code ec;
    auto rv = open_input_binary(path, ec);
    if (ec) {
      throw std::system_error(
          ec, fmt::format("open_input_binary('{}')", path.string()));
    }
    return rv;
  }

  std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path,
              std::error_code& ec) const override {
    ec.clear();
    auto rv = std::make_unique<file_output_stream>(path, std::ios::trunc, ec);
    if (ec) {
      rv.reset();
    }
    return rv;
  }

  std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path) const override {
    std::error_code ec;
    auto rv = open_output(path, ec);
    if (ec) {
      throw std::system_error(ec,
                              fmt::format("open_output('{}')", path.string()));
    }
    return rv;
  }

  std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path,
                     std::error_code& ec) const override {
    ec.clear();
    auto rv = std::make_unique<file_output_stream>(
        path, std::ios::binary | std::ios::trunc, ec);
    if (ec) {
      rv.reset();
    }
    return rv;
  }

  std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path) const override {
    std::error_code ec;
    auto rv = open_output_binary(path, ec);
    if (ec) {
      throw std::system_error(
          ec, fmt::format("open_output_binary('{}')", path.string()));
    }
    return rv;
  }

  std::unique_ptr<input_output_stream>
  open(std::filesystem::path const& path, std::ios_base::openmode mode,
       std::error_code& ec) const override {
    ec.clear();
    auto rv = std::make_unique<file_input_output_stream>(path, mode, ec);
    if (ec) {
      rv.reset();
    }
    return rv;
  }

  std::unique_ptr<input_output_stream>
  open(std::filesystem::path const& path,
       std::ios_base::openmode mode) const override {
    std::error_code ec;
    auto rv = open(path, mode, ec);
    if (ec) {
      throw std::system_error(
          ec, fmt::format("open_input_output_binary('{}')", path.string()));
    }
    return rv;
  }
};

} // namespace

std::unique_ptr<file_access const> create_file_access_generic() {
  return std::make_unique<file_access_generic>();
}

} // namespace dwarfs

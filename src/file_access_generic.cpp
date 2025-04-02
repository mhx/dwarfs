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

class file_input_stream : public input_stream {
 public:
  file_input_stream(std::filesystem::path const& path, std::error_code& ec,
                    std::ios_base::openmode mode)
      : is_{path.string().c_str(), mode} {
    if (is_.bad() || is_.fail() || !is_.is_open()) {
      assign_error_code(ec);
    }
  }

  std::istream& is() override { return is_; }

  void close(std::error_code& ec) override {
    is_.close();
    if (is_.bad()) {
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

 private:
  std::ifstream is_;
};

class file_output_stream : public output_stream {
 public:
  file_output_stream(std::filesystem::path const& path, std::error_code& ec,
                     std::ios_base::openmode mode)
      : os_{path.string().c_str(), mode} {
    if (os_.bad() || os_.fail() || !os_.is_open()) {
      assign_error_code(ec);
    }
  }

  std::ostream& os() override { return os_; }

  void close(std::error_code& ec) override {
    os_.close();
    if (os_.bad()) {
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

 private:
  std::ofstream os_;
};

class file_access_generic : public file_access {
 public:
  bool exists(std::filesystem::path const& path) const override {
    return std::filesystem::exists(path);
  }

  std::unique_ptr<input_stream> open_input(std::filesystem::path const& path,
                                           std::error_code& ec) const override {
    auto rv = std::make_unique<file_input_stream>(path, ec, std::ios::in);
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
    auto rv = std::make_unique<file_input_stream>(path, ec, std::ios::binary);
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
    auto rv = std::make_unique<file_output_stream>(path, ec, std::ios::trunc);
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
    auto rv = std::make_unique<file_output_stream>(
        path, ec, std::ios::binary | std::ios::trunc);
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
};

} // namespace

std::unique_ptr<file_access const> create_file_access_generic() {
  return std::make_unique<file_access_generic>();
}

} // namespace dwarfs

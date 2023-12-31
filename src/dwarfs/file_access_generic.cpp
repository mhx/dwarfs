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

#include <cerrno>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include "dwarfs/file_access.h"
#include "dwarfs/file_access_generic.h"
#include "dwarfs/util.h"

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

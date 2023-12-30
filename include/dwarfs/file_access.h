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

#pragma once

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <system_error>

namespace dwarfs {

class input_stream {
 public:
  virtual ~input_stream() = default;

  virtual std::istream& is() = 0;
  virtual void close() = 0;
  virtual void close(std::error_code& ec) = 0;
};

class output_stream {
 public:
  virtual ~output_stream() = default;

  virtual std::ostream& os() = 0;
  virtual void close() = 0;
  virtual void close(std::error_code& ec) = 0;
};

class file_access {
 public:
  virtual ~file_access() = default;

  virtual bool exists(std::filesystem::path const& path) const = 0;

  virtual std::unique_ptr<input_stream>
  open_input(std::filesystem::path const& path) const = 0;
  virtual std::unique_ptr<input_stream>
  open_input(std::filesystem::path const& path, std::error_code& ec) const = 0;

  virtual std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path) const = 0;
  virtual std::unique_ptr<input_stream>
  open_input_binary(std::filesystem::path const& path,
                    std::error_code& ec) const = 0;

  virtual std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path) const = 0;
  virtual std::unique_ptr<output_stream>
  open_output(std::filesystem::path const& path, std::error_code& ec) const = 0;

  virtual std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path) const = 0;
  virtual std::unique_ptr<output_stream>
  open_output_binary(std::filesystem::path const& path,
                     std::error_code& ec) const = 0;
};

} // namespace dwarfs

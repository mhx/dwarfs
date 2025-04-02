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

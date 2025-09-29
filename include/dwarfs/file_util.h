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
#include <string>
#include <string_view>
#include <system_error>

namespace dwarfs {

std::string read_file(std::filesystem::path const& path, std::error_code& ec);
std::string read_file(std::filesystem::path const& path);

void write_file(std::filesystem::path const& path, std::string_view content,
                std::error_code& ec);
void write_file(std::filesystem::path const& path, std::string_view content);

class temporary_directory {
 public:
  temporary_directory();
  explicit temporary_directory(std::string_view prefix);
  ~temporary_directory();

  temporary_directory(temporary_directory&&) = default;
  temporary_directory& operator=(temporary_directory&&) = default;

  std::filesystem::path const& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

} // namespace dwarfs

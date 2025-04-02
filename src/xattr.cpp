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

#include <dwarfs/xattr.h>

namespace dwarfs {

std::string
getxattr(std::filesystem::path const& path, std::string const& name) {
  std::error_code ec;

  auto value = getxattr(path, name, ec);

  if (ec) {
    throw std::system_error(ec);
  }

  return value;
}

void setxattr(std::filesystem::path const& path, std::string const& name,
              std::string_view value) {
  std::error_code ec;

  setxattr(path, name, value, ec);

  if (ec) {
    throw std::system_error(ec);
  }
}

void removexattr(std::filesystem::path const& path, std::string const& name) {
  std::error_code ec;

  removexattr(path, name, ec);

  if (ec) {
    throw std::system_error(ec);
  }
}

std::vector<std::string> listxattr(std::filesystem::path const& path) {
  std::error_code ec;

  auto names = listxattr(path, ec);

  if (ec) {
    throw std::system_error(ec);
  }

  return names;
}

} // namespace dwarfs

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

#include <fmt/format.h>

#include <dwarfs/file_type.h>

#include <dwarfs/internal/file_status_conv.h>

namespace dwarfs::internal {

namespace fs = std::filesystem;

fs::file_status file_mode_to_status(uint32_t mode) {
  fs::file_type ft;

  switch (mode & posix_file_type::mask) {
  case posix_file_type::socket:
    ft = fs::file_type::socket;
    break;
  case posix_file_type::symlink:
    ft = fs::file_type::symlink;
    break;
  case posix_file_type::regular:
    ft = fs::file_type::regular;
    break;
  case posix_file_type::block:
    ft = fs::file_type::block;
    break;
  case posix_file_type::directory:
    ft = fs::file_type::directory;
    break;
  case posix_file_type::character:
    ft = fs::file_type::character;
    break;
  case posix_file_type::fifo:
    ft = fs::file_type::fifo;
    break;
  default:
    throw std::runtime_error(fmt::format("invalid file mode: {:#06x}", mode));
  }

  return fs::file_status(ft, fs::perms(mode & 07777));
}

uint16_t file_status_to_mode(std::filesystem::file_status status) {
  posix_file_type::value ft;

  switch (status.type()) {
  case fs::file_type::socket:
    ft = posix_file_type::socket;
    break;
  case fs::file_type::symlink:
    ft = posix_file_type::symlink;
    break;
  case fs::file_type::regular:
    ft = posix_file_type::regular;
    break;
  case fs::file_type::block:
    ft = posix_file_type::block;
    break;
  case fs::file_type::directory:
#ifdef _WIN32
  case fs::file_type::junction:
#endif
    ft = posix_file_type::directory;
    break;
  case fs::file_type::character:
    ft = posix_file_type::character;
    break;
  case fs::file_type::fifo:
    ft = posix_file_type::fifo;
    break;
  default:
    throw std::runtime_error(
        fmt::format("invalid file type: {}", fmt::underlying(status.type())));
  }

  return static_cast<uint16_t>(ft) |
         static_cast<uint16_t>(status.permissions());
}

} // namespace dwarfs::internal

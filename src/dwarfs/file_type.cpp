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

#include <fmt/format.h>

#include "dwarfs/file_type.h"

namespace dwarfs {

namespace fs = std::filesystem;

fs::file_status file_mode_to_status(uint16_t mode) {
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
    break;
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
    break;
  }

  return static_cast<uint16_t>(ft) |
         static_cast<uint16_t>(status.permissions());
}

} // namespace dwarfs

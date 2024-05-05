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

#include "dwarfs/xattr.h"

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

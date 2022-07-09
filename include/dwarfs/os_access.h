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

#include <folly/portability/SysStat.h>

#include <memory>
#include <string>

namespace dwarfs {

class mmif;

class dir_reader {
 public:
  virtual ~dir_reader() = default;

  virtual bool read(std::string& name) const = 0;
};

class os_access {
 public:
  virtual ~os_access() = default;

  virtual std::shared_ptr<dir_reader>
  opendir(const std::string& path) const = 0;
  virtual void lstat(const std::string& path, struct ::stat* st) const = 0;
  virtual std::string readlink(const std::string& path, size_t size) const = 0;
  virtual std::shared_ptr<mmif>
  map_file(const std::string& path, size_t size) const = 0;
  virtual int access(const std::string& path, int mode) const = 0;
};
} // namespace dwarfs

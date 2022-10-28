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

#include <string>

#include "dwarfs/object.h"

namespace dwarfs {

class entry_interface : public object {
 public:
  virtual std::string path() const = 0;
  virtual std::string dpath() const = 0;
  virtual std::string const& name() const = 0;
  virtual std::string type_string() const = 0;
  virtual size_t size() const = 0;

  virtual uint16_t get_permissions() const = 0;
  virtual void set_permissions(uint16_t perm) = 0;
  virtual uint16_t get_uid() const = 0;
  virtual void set_uid(uint16_t uid) = 0;
  virtual uint16_t get_gid() const = 0;
  virtual void set_gid(uint16_t gid) = 0;
  virtual uint64_t get_atime() const = 0;
  virtual void set_atime(uint64_t atime) = 0;
  virtual uint64_t get_mtime() const = 0;
  virtual void set_mtime(uint64_t mtime) = 0;
  virtual uint64_t get_ctime() const = 0;
  virtual void set_ctime(uint64_t ctime) = 0;
};
} // namespace dwarfs

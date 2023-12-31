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

#include "dwarfs/file_stat.h"
#include "dwarfs/object.h"

namespace dwarfs {

class entry_interface : public object {
 public:
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;

  virtual std::string path_as_string() const = 0;
  virtual std::string dpath() const = 0;
  virtual std::string unix_dpath() const = 0;
  virtual std::string const& name() const = 0;
  virtual size_t size() const = 0;
  virtual bool is_directory() const = 0;

  virtual mode_type get_permissions() const = 0;
  virtual void set_permissions(mode_type perm) = 0;
  virtual uid_type get_uid() const = 0;
  virtual void set_uid(uid_type uid) = 0;
  virtual gid_type get_gid() const = 0;
  virtual void set_gid(gid_type gid) = 0;
  virtual uint64_t get_atime() const = 0;
  virtual void set_atime(uint64_t atime) = 0;
  virtual uint64_t get_mtime() const = 0;
  virtual void set_mtime(uint64_t mtime) = 0;
  virtual uint64_t get_ctime() const = 0;
  virtual void set_ctime(uint64_t ctime) = 0;
};
} // namespace dwarfs

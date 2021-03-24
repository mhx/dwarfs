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

#include <memory>
#include <string>
#include <vector>

#include "dwarfs/os_access.h"
#include "dwarfs/script.h"

namespace dwarfs {
namespace test {

class dir_reader_mock : public dir_reader {
 public:
  dir_reader_mock(std::vector<std::string>&& files);

  bool read(std::string& name) const override;

 private:
  std::vector<std::string> m_files;
  mutable size_t m_index;
};

class os_access_mock : public os_access {
 public:
  std::shared_ptr<dir_reader> opendir(const std::string& path) const override;

  void lstat(const std::string& path, struct ::stat* st) const override;

  std::string readlink(const std::string& path, size_t size) const override;

  std::shared_ptr<mmif>
  map_file(const std::string& path, size_t size) const override;

  int access(const std::string&, int) const override;
};

class script_mock : public script {
 public:
  bool has_configure() const override { return true; }
  bool has_filter() const override { return true; }
  bool has_transform() const override { return true; }
  bool has_order() const override { return true; }

  void configure(options_interface const& /*oi*/) override {}

  bool filter(entry_interface const& /*ei*/) override { return true; }

  void transform(entry_interface& /*ei*/) override {
    // do nothing
  }

  void order(inode_vector& /*iv*/) override {
    // do nothing
  }
};

struct simplestat {
  ::ino_t st_ino;
  ::mode_t st_mode;
  ::nlink_t st_nlink;
  ::uid_t st_uid;
  ::gid_t st_gid;
  ::off_t st_size;
  ::dev_t st_rdev;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
};

extern std::map<std::string, simplestat> statmap;

} // namespace test
} // namespace dwarfs

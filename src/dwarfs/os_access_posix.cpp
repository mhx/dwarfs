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

#include <folly/portability/Dirent.h>
#include <folly/portability/SysStat.h>
#include <folly/portability/Unistd.h>

#include <stdexcept>
#include <vector>

#include <fmt/format.h>

#include "dwarfs/error.h"
#include "dwarfs/mmap.h"
#include "dwarfs/os_access_posix.h"

namespace dwarfs {

class posix_dir_reader final : public dir_reader {
 public:
  explicit posix_dir_reader(const std::string& path)
      : dir_(::opendir(path.c_str())) {
    if (!dir_) {
      DWARFS_THROW(system_error, fmt::format("opendir('{}')", path));
    }
  }

  ~posix_dir_reader() noexcept override {
    if (dir_) {
      ::closedir(dir_);
    }
  }

  bool read(std::string& name) const override {
    errno = 0;

    auto ent = readdir(dir_);

    if (ent) {
      name.assign(ent->d_name);
      return true;
    }

    if (errno == 0) {
      return false;
    }

    DWARFS_THROW(system_error, "readdir");
  }

 private:
  DIR* dir_;
};

std::shared_ptr<dir_reader>
os_access_posix::opendir(const std::string& path) const {
  return std::make_shared<posix_dir_reader>(path);
}

void os_access_posix::lstat(const std::string& path, struct ::stat* st) const {
  if (::lstat(path.c_str(), st) == -1) {
    DWARFS_THROW(system_error, fmt::format("lstat('{}')", path));
  }
}

std::string
os_access_posix::readlink(const std::string& path, size_t size) const {
  std::vector<char> linkname(size);
#ifndef _WIN32
  ssize_t rv = ::readlink(path.c_str(), &linkname[0], size);
#else
  ssize_t rv = folly::portability::unistd::readlink(path.c_str(), &linkname[0], size);
#endif

  if (rv == static_cast<ssize_t>(size)) {
    return std::string(linkname.begin(), linkname.end());
  }

  DWARFS_THROW(system_error, fmt::format("readlink('{}')", path));
}

std::shared_ptr<mmif>
os_access_posix::map_file(const std::string& path, size_t size) const {
  return std::make_shared<mmap>(path, size);
}

int os_access_posix::access(const std::string& path, int mode) const {
  return ::access(path.c_str(), mode);
}
} // namespace dwarfs

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

#include <cstdlib>

#include <folly/portability/Unistd.h>

#include "dwarfs/mmap.h"
#include "dwarfs/os_access_generic.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

class generic_dir_reader final : public dir_reader {
 public:
  explicit generic_dir_reader(fs::path const& path)
      : it_(fs::directory_iterator(path)) {}

  bool read(fs::path& name) override {
    if (it_ != fs::directory_iterator()) {
      name.assign(it_->path());
      ++it_;
      return true;
    }

    return false;
  }

 private:
  fs::directory_iterator it_;
};

} // namespace

std::unique_ptr<dir_reader>
os_access_generic::opendir(fs::path const& path) const {
  return std::make_unique<generic_dir_reader>(path);
}

file_stat os_access_generic::symlink_info(fs::path const& path) const {
  return make_file_stat(path);
}

fs::path os_access_generic::read_symlink(fs::path const& path) const {
  return fs::read_symlink(path);
}

std::unique_ptr<mmif>
os_access_generic::map_file(fs::path const& path, size_t size) const {
  return std::make_unique<mmap>(path, size);
}

int os_access_generic::access(fs::path const& path, int mode) const {
#ifdef _WIN32
  return ::_waccess(path.wstring().c_str(), mode);
#else
  return ::access(path.string().c_str(), mode);
#endif
}

fs::path os_access_generic::canonical(fs::path const& path) const {
  return canonical_path(path);
}

fs::path os_access_generic::current_path() const { return fs::current_path(); }

std::optional<std::string>
os_access_generic::getenv(std::string_view name) const {
  std::string name_str(name);
  if (auto value = std::getenv(name_str.c_str())) {
    return value;
  }
  return std::nullopt;
}

} // namespace dwarfs

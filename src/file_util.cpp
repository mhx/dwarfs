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

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <folly/FileUtil.h>
#include <folly/portability/Windows.h>

#include <dwarfs/conv.h>
#include <dwarfs/file_util.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

fs::path make_tempdir_path(std::string_view prefix) {
  static thread_local boost::uuids::random_generator gen;
  auto dirname = boost::uuids::to_string(gen());
  if (!prefix.empty()) {
    dirname = std::string(prefix) + '.' + dirname;
  }
  return fs::temp_directory_path() / dirname;
}

bool keep_temporary_directories() {
  static bool keep = getenv_is_enabled("DWARFS_KEEP_TEMPORARY_DIRECTORIES");
  return keep;
}

std::error_code get_last_error_code() {
#ifdef _WIN32
  return std::error_code(::GetLastError(), std::system_category());
#else
  return std::error_code(errno, std::generic_category());
#endif
}

} // namespace

temporary_directory::temporary_directory()
    : temporary_directory(std::string_view{}) {}

temporary_directory::temporary_directory(std::string_view prefix)
    : path_{make_tempdir_path(prefix)} {
  fs::create_directory(path_);
}

temporary_directory::~temporary_directory() {
  if (!keep_temporary_directories()) {
    std::error_code ec;
    fs::remove_all(path_, ec);
    if (ec) {
      std::cerr << "Failed to remove temporary directory " << path_ << ": "
                << ec.message() << "\n";
    }
  }
}

std::string read_file(std::filesystem::path const& path, std::error_code& ec) {
  std::string out;
  if (folly::readFile(path.string().c_str(), out)) {
    ec.clear();
  } else {
    ec = get_last_error_code();
  }
  return out;
}

std::string read_file(std::filesystem::path const& path) {
  std::error_code ec;
  auto content = read_file(path, ec);
  if (ec) {
    throw std::system_error(ec);
  }
  return content;
}

void write_file(std::filesystem::path const& path, std::string const& content,
                std::error_code& ec) {
  if (folly::writeFile(content, path.string().c_str())) {
    ec.clear();
  } else {
    ec = get_last_error_code();
  }
}

void write_file(std::filesystem::path const& path, std::string const& content) {
  std::error_code ec;
  write_file(path, content, ec);
  if (ec) {
    throw std::system_error(ec);
  }
}

} // namespace dwarfs

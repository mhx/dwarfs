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
  return {static_cast<int>(::GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
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

void write_file(std::filesystem::path const& path, std::string_view content,
                std::error_code& ec) {
  if (folly::writeFile(content, path.string().c_str())) {
    ec.clear();
  } else {
    ec = get_last_error_code();
  }
}

void write_file(std::filesystem::path const& path, std::string_view content) {
  std::error_code ec;
  write_file(path, content, ec);
  if (ec) {
    throw std::system_error(ec);
  }
}

} // namespace dwarfs

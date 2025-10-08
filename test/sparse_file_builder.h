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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include <dwarfs/types.h>

namespace dwarfs::test {

class sparse_file_builder {
 public:
  // returns the hole granularity of the filesystem containing 'path'
  // if the filesystem does not support sparse files, returns nullopt
  static std::optional<size_t>
  hole_granularity(std::filesystem::path const& path);

  // creates a sparse file builder for the given path
  static sparse_file_builder
  create(std::filesystem::path const& path, std::error_code& ec);
  static sparse_file_builder create(std::filesystem::path const& path);

  // destructor commits the file if not done before, but ignores errors
  ~sparse_file_builder();

  void truncate(file_size_t size, std::error_code& ec);
  void truncate(file_size_t size);

  void
  write_data(file_off_t offset, std::string_view data, std::error_code& ec);
  void write_data(file_off_t offset, std::string_view data);

  void punch_hole(file_off_t offset, file_off_t size, std::error_code& ec);
  void punch_hole(file_off_t offset, file_off_t size);

  void commit(std::error_code& ec);
  void commit();

  class impl;

  sparse_file_builder(sparse_file_builder&&) = default;
  sparse_file_builder& operator=(sparse_file_builder&&) = default;

 private:
  explicit sparse_file_builder(std::unique_ptr<impl> impl);

  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::test

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

#pragma once

#include <filesystem>
#include <memory>

#include <dwarfs/file_stat.h>

namespace dwarfs::writer {

namespace internal {

class entry;
class file;
class dir;
class link;
class device;

class entry_factory_;

} // namespace internal

class entry_storage {
 public:
  entry_storage();
  ~entry_storage();

  entry_storage(entry_storage&&) noexcept;
  entry_storage& operator=(entry_storage&&) noexcept;

  entry_storage(entry_storage const&) = delete;
  entry_storage& operator=(entry_storage const&) = delete;

  internal::entry* root() const noexcept;
  bool empty() const noexcept;

 private:
  friend class internal::entry_factory_;

  internal::dir*
  create_root_dir(std::filesystem::path const& path, file_stat const& st);

  internal::file* create_file(std::filesystem::path const& path,
                              internal::entry* parent, file_stat const& st);

  internal::dir* create_dir(std::filesystem::path const& path,
                            internal::entry* parent, file_stat const& st);

  internal::link* create_link(std::filesystem::path const& path,
                              internal::entry* parent, file_stat const& st);

  internal::device* create_device(std::filesystem::path const& path,
                                  internal::entry* parent, file_stat const& st);

  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::writer

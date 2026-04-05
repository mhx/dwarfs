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
#include <optional>

#include <dwarfs/file_stat.h>
#include <dwarfs/writer/entry_handle.h>
#include <dwarfs/writer/entry_interface.h>

namespace dwarfs {

class os_access;

namespace writer::internal {

class provisional_entry : public entry_interface {
 public:
  provisional_entry(os_access const& os, std::filesystem::path const& path,
                    std::optional<entry_handle> parent = std::nullopt);

  provisional_entry(provisional_entry const&) = delete;
  provisional_entry& operator=(provisional_entry const&) = delete;
  provisional_entry(provisional_entry&&) = delete;
  provisional_entry& operator=(provisional_entry&&) = delete;

  entry_type type() const;
  std::string name() const;

  bool is_directory() const override;
  std::string unix_dpath() const override;

  entry_handle commit(entry_storage& tree);

 private:
  std::filesystem::path path_;
  file_stat stat_;
  std::optional<entry_handle> parent_;
};

} // namespace writer::internal
} // namespace dwarfs

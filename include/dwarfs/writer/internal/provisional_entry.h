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
#include <variant>

#include <dwarfs/writer/entry_handle.h>

#include <dwarfs/writer/internal/entry.h>

namespace dwarfs {

class os_access;

namespace writer::internal {

class provisional_entry {
 public:
  provisional_entry(os_access const& os, std::filesystem::path const& path);
  provisional_entry(os_access const& os, std::filesystem::path const& path,
                    entry_handle parent);

  bool valid() const { return !std::holds_alternative<std::monostate>(entry_); }
  explicit operator bool() const { return valid(); }

  provisional_entry(provisional_entry const&) = delete;
  provisional_entry& operator=(provisional_entry const&) = delete;
  provisional_entry(provisional_entry&&) = delete;
  provisional_entry& operator=(provisional_entry&&) = delete;

  const_entry_handle handle() const;
  entry_handle commit(entry_storage& tree);

 private:
  std::variant<std::monostate, file, dir, link, device> entry_;
  bool const is_root_dir_{false};
};

} // namespace writer::internal
} // namespace dwarfs

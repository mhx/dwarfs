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
#include <iosfwd>
#include <memory>
#include <optional>
#include <variant>

namespace dwarfs {

class input_stream;
class os_access;

namespace writer {

struct input_entry {
  std::filesystem::path path;
};

class input_entry_reader {
 public:
  virtual ~input_entry_reader() = default;

  virtual std::optional<input_entry> next() = 0;
};

using entry_reader_stream =
    std::variant<std::istream*, std::unique_ptr<input_stream>>;

class plain_input_entry_reader {
 public:
  static std::unique_ptr<input_entry_reader>
  create(entry_reader_stream input,
         std::filesystem::path const& canonical_input_root,
         os_access const& os);
};

} // namespace writer
} // namespace dwarfs

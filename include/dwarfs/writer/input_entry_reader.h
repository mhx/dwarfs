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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_type.h>

namespace dwarfs {

class input_stream;
class os_access;
class logger;

namespace writer {

struct input_entry_attributes {
  std::optional<file_stat::perms_type> permissions{};
  std::optional<file_stat::uid_type> uid{};
  std::optional<file_stat::gid_type> gid{};
  std::optional<file_stat::timespec_type> atime{};
  std::optional<file_stat::timespec_type> mtime{};
  std::optional<file_stat::timespec_type> ctime{};
};

struct input_entry_spec {
  struct file {
    std::filesystem::path source;
    std::vector<std::filesystem::path> aliases{};
  };

  struct directory {};

  struct symlink {
    std::string target;
  };

  struct device {
    posix_file_type::value type;
    std::uint32_t major;
    std::uint32_t minor;
  };

  struct special {
    posix_file_type::value type;
  };

  struct hardlink {
    std::filesystem::path target;
  };

  std::variant<file, directory, symlink, device, special, hardlink> content;
  input_entry_attributes attributes{};
};

struct input_entry {
  // Logical path in the resulting image.
  std::filesystem::path path;
  // Path of the source if backed by a real file system entry.
  std::optional<std::filesystem::path> source;
  // Optional specification of the entry's attributes.
  std::optional<input_entry_spec> spec{};
};

class input_entry_reader {
 public:
  virtual ~input_entry_reader() = default;
  virtual std::optional<input_entry> next() = 0;
  // Root entry to use if the input does not contain an explicit root entry.
  // If this returns nullopt, a synthetic root should be created.
  virtual std::optional<input_entry> implicit_root() const = 0;
  virtual std::size_t error_count() const noexcept = 0;
};

using entry_reader_stream =
    std::variant<std::istream*, std::unique_ptr<input_stream>>;

class input_entry_reader_factory {
 public:
  static std::span<std::string_view const> names() noexcept;

  static std::unique_ptr<input_entry_reader>
  create(std::string_view name, logger& lgr, os_access const& os,
         entry_reader_stream input,
         std::filesystem::path const& canonical_input_root);
};

} // namespace writer
} // namespace dwarfs

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

#include <cstdint>
#include <string>
#include <string_view>

#include <dwarfs/file_view.h>
#include <dwarfs/types.h>

#include <dwarfs/writer/internal/inode_id.h>

namespace dwarfs::writer {

struct inode_options;

namespace internal {

class entry_storage;
class progress;
class scanner_progress;

namespace detail {

enum class scan_mode {
  skip_holes,
  include_holes,
};

class inode_scanner {
 public:
  inode_scanner(entry_storage& storage, inode_id self_id);

  void populate(file_size_t size);

  void scan(file_view const& mm, inode_options const& opts, progress& prog);

 private:
  std::shared_ptr<scanner_progress>
  make_progress_context(std::string_view context, file_view const& mm,
                        progress& prog, size_t min_size) const;

  void scan_range(file_view const& mm, scanner_progress* sprog,
                  file_off_t offset, file_size_t size, size_t chunk_size,
                  std::invocable<std::span<uint8_t const>> auto&& scanner,
                  scan_mode mode = scan_mode::skip_holes);

  void
  scan_range(file_view const& mm, scanner_progress* sprog, size_t chunk_size,
             std::invocable<std::span<uint8_t const>> auto&& scanner,
             scan_mode mode = scan_mode::skip_holes);

  void scan_fragments(file_view const& mm, scanner_progress* sprog,
                      inode_options const& opts, size_t chunk_size);

  void scan_full(file_view const& mm, scanner_progress* sprog,
                 inode_options const& opts, size_t chunk_size);

  entry_storage* storage_{nullptr};
  inode_id self_id_;
};

} // namespace detail

} // namespace internal

} // namespace dwarfs::writer

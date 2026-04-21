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
#include <exception>
#include <variant>
#include <vector>

#include <dwarfs/file_view.h>

#include <dwarfs/writer/internal/entry_handle.h>
#include <dwarfs/writer/internal/nilsimsa.h>

namespace dwarfs::writer::internal {

struct inode_mmap_any_result {
  file_view view;
  const_file_handle handle;
  std::vector<std::pair<const_file_handle, std::exception_ptr>> errors;
};

struct inode_similarity_hash_data {
  fragment_category category;
  std::variant<std::uint32_t, nilsimsa::hash_type> hash;

  inode_similarity_hash_data(fragment_category c, std::uint32_t h)
      : category{c}
      , hash{h} {}

  inode_similarity_hash_data(fragment_category c, nilsimsa::hash_type h)
      : category{c}
      , hash{h} {}
};

} // namespace dwarfs::writer::internal

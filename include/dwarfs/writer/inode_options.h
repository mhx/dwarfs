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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <dwarfs/writer/categorized_option.h>
#include <dwarfs/writer/fragment_order_options.h>

namespace dwarfs::writer {

class categorizer_manager;

struct inode_options {
  std::optional<size_t> max_similarity_scan_size;
  std::shared_ptr<writer::categorizer_manager> categorizer_mgr;
  writer::categorized_option<fragment_order_options> fragment_order{
      fragment_order_options()};
};

} // namespace dwarfs::writer

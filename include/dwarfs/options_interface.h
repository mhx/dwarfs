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
 */

#pragma once

#include <string>

#include <dwarfs/options.h>

namespace dwarfs {

class options_interface {
 public:
  enum set_mode { DEFAULT, OVERRIDE };

  virtual ~options_interface() = default;

  virtual void
  set_order(file_order_mode order_mode, set_mode mode = DEFAULT) = 0;
  virtual void
  set_remove_empty_dirs(bool remove_empty, set_mode mode = DEFAULT) = 0;
  virtual void enable_similarity() = 0;
};

} // namespace dwarfs

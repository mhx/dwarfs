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

#include <memory>
#include <vector>

namespace dwarfs {

class entry_interface;
class inode;
class options_interface;

class script {
 public:
  using inode_ptr = std::shared_ptr<inode>;
  using inode_vector = std::vector<inode_ptr>;

  virtual ~script() = default;

  virtual bool has_configure() const = 0;
  virtual bool has_filter() const = 0;
  virtual bool has_transform() const = 0;
  virtual bool has_order() const = 0;

  virtual void configure(options_interface const& oi) = 0;
  virtual bool filter(entry_interface const& ei) = 0;
  virtual void transform(entry_interface& ei) = 0;
  virtual void order(inode_vector& iv) = 0;
};

} // namespace dwarfs

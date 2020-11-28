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

#include <functional>
#include <memory>
#include <ostream>

#include "dwarfs/file_interface.h"
#include "dwarfs/inode.h"

namespace dwarfs {

class script;

class inode_manager {
 public:
  static std::unique_ptr<inode_manager> create();

  virtual ~inode_manager() = default;
  virtual std::shared_ptr<inode> create_inode() = 0;
  virtual size_t count() const = 0;
  virtual void order_inodes() = 0;
  virtual void order_inodes(std::shared_ptr<script> scr) = 0;
  virtual void order_inodes_by_similarity() = 0;
  virtual void number_inodes(size_t first_no) = 0;
  virtual void for_each_inode(
      std::function<void(std::shared_ptr<inode> const&)> const& fn) const = 0;
};
} // namespace dwarfs

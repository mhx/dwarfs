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

#include "dwarfs/inode.h"

namespace dwarfs {

class logger;

class inode_ordering {
 public:
  inode_ordering(logger& lgr);

  void by_inode_number(sortable_inode_span& sp) const {
    impl_->by_inode_number(sp);
  }

  void by_path(sortable_inode_span& sp) const { impl_->by_path(sp); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void by_inode_number(sortable_inode_span& sp) const = 0;
    virtual void by_path(sortable_inode_span& sp) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

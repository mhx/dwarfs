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

#include <cstddef>
#include <functional>
#include <memory>

namespace dwarfs {

class inode;
class logger;
class progress;
class script;

struct file_order_options;

class inode_manager {
 public:
  using inode_cb = std::function<void(std::shared_ptr<inode> const&)>;
  using order_cb = std::function<int64_t(std::shared_ptr<inode> const&)>;

  inode_manager(logger& lgr, progress& prog);

  std::shared_ptr<inode> create_inode() { return impl_->create_inode(); }

  size_t count() const { return impl_->count(); }

  void order_inodes(std::shared_ptr<script> scr,
                    file_order_options const& file_order, uint32_t first_inode,
                    order_cb const& fn) {
    impl_->order_inodes(std::move(scr), file_order, first_inode, fn);
  }

  void for_each_inode(inode_cb const& fn) const { impl_->for_each_inode(fn); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::shared_ptr<inode> create_inode() = 0;
    virtual size_t count() const = 0;
    virtual void order_inodes(std::shared_ptr<script> scr,
                              file_order_options const& file_order,
                              uint32_t first_inode, order_cb const& fn) = 0;
    virtual void for_each_inode(
        std::function<void(std::shared_ptr<inode> const&)> const& fn) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

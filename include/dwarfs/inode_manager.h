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
#include <iosfwd>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "dwarfs/fragment_category.h"
#include "dwarfs/inode.h"

namespace dwarfs {

class file;
class inode;
class logger;
class os_access;
class progress;
class script;
class worker_group;

struct inode_options;

class inode_manager {
 public:
  using inode_cb = std::function<void(std::shared_ptr<inode> const&)>;

  struct fragment_info {
    fragment_info(fragment_category::value_type cat, size_t count, size_t size)
        : category{cat}
        , fragment_count{count}
        , total_size{size} {}

    fragment_category::value_type category;
    size_t fragment_count;
    size_t total_size;
  };

  struct fragment_infos {
    std::vector<fragment_category> categories;
    std::vector<fragment_info> info;
    std::unordered_map<fragment_category, size_t> category_size;
    size_t total_size{0};
  };

  inode_manager(logger& lgr, progress& prog, inode_options const& opts);

  std::shared_ptr<inode> create_inode() { return impl_->create_inode(); }

  size_t count() const { return impl_->count(); }

  void for_each_inode_in_order(inode_cb const& fn) const {
    impl_->for_each_inode_in_order(fn);
  }

  fragment_infos fragment_category_info() const {
    return impl_->fragment_category_info();
  }

  void scan_background(worker_group& wg, os_access const& os,
                       std::shared_ptr<inode> ino, file* p) const {
    impl_->scan_background(wg, os, std::move(ino), p);
  }

  bool has_invalid_inodes() const { return impl_->has_invalid_inodes(); }

  void try_scan_invalid(worker_group& wg, os_access const& os) {
    impl_->try_scan_invalid(wg, os);
  }

  void dump(std::ostream& os) const { impl_->dump(os); }

  sortable_inode_span sortable_span() const { return impl_->sortable_span(); }

  sortable_inode_span
  ordered_span(fragment_category cat, worker_group& wg) const {
    return impl_->ordered_span(cat, wg);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::shared_ptr<inode> create_inode() = 0;
    virtual size_t count() const = 0;
    virtual void for_each_inode_in_order(
        std::function<void(std::shared_ptr<inode> const&)> const& fn) const = 0;
    virtual fragment_infos fragment_category_info() const = 0;
    virtual void scan_background(worker_group& wg, os_access const& os,
                                 std::shared_ptr<inode> ino, file* p) const = 0;
    virtual bool has_invalid_inodes() const = 0;
    virtual void try_scan_invalid(worker_group& wg, os_access const& os) = 0;
    virtual void dump(std::ostream& os) const = 0;
    virtual sortable_inode_span sortable_span() const = 0;
    virtual sortable_inode_span
    ordered_span(fragment_category cat, worker_group& wg) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

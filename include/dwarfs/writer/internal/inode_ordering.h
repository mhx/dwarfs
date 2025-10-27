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

#include <filesystem>
#include <memory>
#include <unordered_map>

#include <dwarfs/writer/internal/inode.h>

namespace dwarfs {

class logger;

namespace internal {

class worker_group;

}

namespace writer {

struct inode_options;
struct fragment_order_options;

namespace internal {

class progress;

struct similarity_ordering_options;

class inode_ordering {
 public:
  inode_ordering(logger& lgr, progress& prog, inode_options const& opts);

  void by_inode_number(sortable_inode_span& sp) const {
    impl_->by_inode_number(sp);
  }

  void by_input_order(sortable_inode_span& sp) const {
    impl_->by_input_order(sp);
  }

  void by_path(sortable_inode_span& sp) const { impl_->by_path(sp); }

  void by_reverse_path(sortable_inode_span& sp) const {
    impl_->by_reverse_path(sp);
  }

  void by_similarity(sortable_inode_span& sp, fragment_category cat) const {
    impl_->by_similarity(sp, cat);
  }

  void by_nilsimsa(dwarfs::internal::worker_group& wg,
                   similarity_ordering_options const& opts,
                   sortable_inode_span& sp, fragment_category cat) const {
    impl_->by_nilsimsa(wg, opts, sp, cat);
  }

  void by_explicit_order(sortable_inode_span& sp,
                         std::filesystem::path const& root_path,
                         fragment_order_options const& opts) const {
    impl_->by_explicit_order(sp, root_path, opts);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void by_inode_number(sortable_inode_span& sp) const = 0;
    virtual void by_input_order(sortable_inode_span& sp) const = 0;
    virtual void by_path(sortable_inode_span& sp) const = 0;
    virtual void by_reverse_path(sortable_inode_span& sp) const = 0;
    virtual void
    by_similarity(sortable_inode_span& sp, fragment_category cat) const = 0;
    virtual void
    by_nilsimsa(dwarfs::internal::worker_group& wg,
                similarity_ordering_options const& opts,
                sortable_inode_span& sp, fragment_category cat) const = 0;
    virtual void
    by_explicit_order(sortable_inode_span& sp,
                      std::filesystem::path const& root_path,
                      fragment_order_options const& opts) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace internal
} // namespace writer
} // namespace dwarfs

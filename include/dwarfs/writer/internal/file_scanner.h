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

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>

namespace dwarfs {

class logger;
class os_access;

namespace internal {

class worker_group;

} // namespace internal

namespace writer {

struct inode_options;

namespace internal {

class file;
class inode_manager;
class progress;

class file_scanner {
 public:
  struct options {
    std::optional<std::string> hash_algo{};
    bool debug_inode_create{false};
  };

  file_scanner(logger& lgr, dwarfs::internal::worker_group& wg,
               os_access const& os, inode_manager& im, progress& prog,
               options const& opts);

  void scan(file* p) { impl_->scan(p); }
  void finalize(uint32_t& inode_num) { impl_->finalize(inode_num); }
  uint32_t num_unique() const { return impl_->num_unique(); }
  void dump(std::ostream& os) const { impl_->dump(os); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void scan(file* p) = 0;
    virtual void finalize(uint32_t& inode_num) = 0;
    virtual uint32_t num_unique() const = 0;
    virtual void dump(std::ostream& os) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace internal
} // namespace writer
} // namespace dwarfs

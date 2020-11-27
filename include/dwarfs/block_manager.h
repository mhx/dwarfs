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

#include "dwarfs/logger.h"

namespace dwarfs {

class filesystem_writer;
class inode;
class os_access;
class progress;

class block_manager {
 public:
  struct config {
    config();

    std::vector<size_t> blockhash_window_size;
    unsigned window_increment_shift;
    size_t memory_limit;
    unsigned block_size_bits;
  };

  block_manager(logger& lgr, progress& prog, const config& cfg,
                std::shared_ptr<os_access> os, filesystem_writer& fsw);

  void add_inode(std::shared_ptr<inode> ino) { impl_->add_inode(ino); }

  void finish_blocks() { impl_->finish_blocks(); }

  size_t total_size() const { return impl_->total_size(); }

  size_t total_blocks() const { return impl_->total_blocks(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_inode(std::shared_ptr<inode> ino) = 0;
    virtual void finish_blocks() = 0;
    virtual size_t total_size() const = 0;
    virtual size_t total_blocks() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs

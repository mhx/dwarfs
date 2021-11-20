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
#include <cstdint>

#include <future>
#include <memory>

#include "dwarfs/block_compressor.h"
#include "dwarfs/fstypes.h"

namespace dwarfs {

struct block_cache_options;
struct cache_tidy_config;

class block_range;
class fs_section;
class logger;
class mmif;

class block_cache {
 public:
  block_cache(logger& lgr, std::shared_ptr<mmif> mm,
              const block_cache_options& options);

  size_t block_count() const { return impl_->block_count(); }

  void insert(fs_section const& section) { impl_->insert(section); }

  void set_block_size(size_t size) { impl_->set_block_size(size); }

  void set_num_workers(size_t num) { impl_->set_num_workers(num); }

  void set_tidy_config(cache_tidy_config const& cfg) {
    impl_->set_tidy_config(cfg);
  }

  std::future<block_range>
  get(size_t block_no, size_t offset, size_t size) const {
    return impl_->get(block_no, offset, size);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual size_t block_count() const = 0;
    virtual void insert(fs_section const& section) = 0;
    virtual void set_block_size(size_t size) = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual std::future<block_range>
    get(size_t block_no, size_t offset, size_t length) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs

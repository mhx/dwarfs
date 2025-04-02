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

#include <dwarfs/block_compressor.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/writer/fragment_category.h>

namespace dwarfs {

class logger;
class thread_pool;

namespace writer {

struct filesystem_writer_options;

class writer_progress;

namespace internal {

class filesystem_writer_detail;

}

class filesystem_writer {
 public:
  filesystem_writer(std::ostream& os, logger& lgr, thread_pool& pool,
                    writer_progress& prog);

  filesystem_writer(std::ostream& os, logger& lgr, thread_pool& pool,
                    writer_progress& prog,
                    filesystem_writer_options const& options,
                    std::istream* header = nullptr);

  ~filesystem_writer() noexcept;
  filesystem_writer(filesystem_writer&&) noexcept;
  filesystem_writer& operator=(filesystem_writer&&) noexcept;

  void add_default_compressor(block_compressor bc);
  void add_category_compressor(fragment_category::value_type cat,
                               block_compressor bc);
  void add_section_compressor(section_type type, block_compressor bc);

  internal::filesystem_writer_detail& get_internal() { return *impl_; }

 private:
  std::unique_ptr<internal::filesystem_writer_detail> impl_;
};

} // namespace writer

} // namespace dwarfs

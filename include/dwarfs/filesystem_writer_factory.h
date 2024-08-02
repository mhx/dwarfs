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

#include <iosfwd>

#include <dwarfs/filesystem_writer.h>
#include <dwarfs/options.h>

namespace dwarfs {

class block_compressor;
class logger;
class writer_progress;
class thread_pool;

class filesystem_writer_factory {
 public:
  static filesystem_writer
  create(std::ostream& os, logger& lgr, thread_pool& pool,
         writer_progress& prog, block_compressor const& schema_bc,
         block_compressor const& metadata_bc,
         block_compressor const& history_bc,
         filesystem_writer_options const& options = filesystem_writer_options(),
         std::istream* header = nullptr);
};

} // namespace dwarfs

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

#include <ostream>

#include <dwarfs/util.h>
#include <dwarfs/writer/internal/chmod_transformer.h>
#include <dwarfs/writer/metadata_options.h>

namespace dwarfs::writer {

uint32_t metadata_options::time_resolution_sec() const {
  if (time_resolution.has_value()) {
    if (auto const res = *time_resolution; res > std::chrono::seconds{1}) {
      if (res % std::chrono::seconds{1} != std::chrono::nanoseconds{0}) {
        DWARFS_THROW(runtime_error,
                     "cannot handle time resolution that is larger "
                     "than one second but not a whole number of "
                     "seconds");
      }

      return static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::seconds>(res).count());
    }
  }

  return 1;
}

uint32_t metadata_options::subsecond_resolution_nsec_multiplier() const {
  if (time_resolution.has_value()) {
    if (auto const res = *time_resolution; res < std::chrono::seconds{1}) {
      if (std::chrono::seconds{1} % res != std::chrono::nanoseconds{0}) {
        DWARFS_THROW(runtime_error,
                     "cannot handle subsecond time resolution "
                     "that is not a whole divisor of one second");
      }

      return static_cast<uint32_t>(res.count());
    }
  }

  return 0;
}

void metadata_options::validate(metadata_options const& opts) {
  internal::chmod_transformer::build_chain(opts.chmod_specifiers, opts.umask);
}

std::ostream& operator<<(std::ostream& os, metadata_options const& opts) {
  os << "{";
  if (opts.uid) {
    os << "uid: " << *opts.uid << ", ";
  }
  if (opts.gid) {
    os << "gid: " << *opts.gid << ", ";
  }
  if (opts.timestamp) {
    os << "timestamp: " << *opts.timestamp << ", ";
  }
  if (opts.keep_all_times) {
    os << "keep_all_times, ";
  }
  if (opts.time_resolution) {
    os << "time_resolution: " << time_with_unit(*opts.time_resolution) << ", ";
  }
  if (opts.pack_chunk_table) {
    os << "pack_chunk_table, ";
  }
  if (opts.pack_directories) {
    os << "pack_directories, ";
  }
  if (opts.pack_shared_files_table) {
    os << "pack_shared_files_table, ";
  }
  if (opts.plain_names_table) {
    os << "plain_names_table, ";
  }
  if (opts.pack_names) {
    os << "pack_names, ";
  }
  if (opts.pack_names_index) {
    os << "pack_names_index, ";
  }
  if (opts.plain_symlinks_table) {
    os << "plain_symlinks_table, ";
  }
  if (opts.pack_symlinks) {
    os << "pack_symlinks, ";
  }
  if (opts.pack_symlinks_index) {
    os << "pack_symlinks_index, ";
  }
  if (opts.force_pack_string_tables) {
    os << "force_pack_string_tables, ";
  }
  if (opts.no_create_timestamp) {
    os << "no_create_timestamp, ";
  }
  os << "inode_size_cache_min_chunk_count: "
     << opts.inode_size_cache_min_chunk_count;
  os << "}";
  return os;
}

} // namespace dwarfs::writer

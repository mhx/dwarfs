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

#include <exception>
#include <iosfwd>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <dwarfs/file_view.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/inode_fragments.h>
#include <dwarfs/writer/internal/sortable_span.h>
#include <dwarfs/writer/object.h>

#include <dwarfs/writer/internal/inode_hole_mapper.h>
#include <dwarfs/writer/internal/nilsimsa.h>

namespace dwarfs {

namespace thrift::metadata {

class chunk;

}

class os_access;

namespace writer {

struct inode_options;

namespace internal {

class file;
class progress;

class inode : public object {
 public:
  using files_vector = small_vector<file*, 1>;

  virtual void set_files(files_vector&& fv) = 0;
  virtual void populate(file_size_t size) = 0;
  virtual void
  scan(file_view const& mm, inode_options const& options, progress& prog) = 0;
  virtual void set_num(uint32_t num) = 0;
  virtual uint32_t num() const = 0;
  virtual bool has_category(fragment_category cat) const = 0;
  virtual std::optional<uint32_t>
  similarity_hash(fragment_category cat) const = 0;
  virtual nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const = 0;
  virtual file_size_t size() const = 0;
  virtual file const* any() const = 0;
  virtual files_vector const& all() const = 0;
  virtual bool
  append_chunks_to(std::vector<thrift::metadata::chunk>& vec,
                   std::optional<inode_hole_mapper>& hole_mapper) const = 0;
  virtual inode_fragments& fragments() = 0;
  virtual void dump(std::ostream& os, inode_options const& options) const = 0;
  virtual void set_scan_error(file const* fp, std::exception_ptr ep) = 0;
  virtual std::optional<std::pair<file const*, std::exception_ptr>>
  get_scan_error() const = 0;
  // TODO: WTH is this interface???
  virtual std::tuple<file_view, file const*,
                     std::vector<std::pair<file const*, std::exception_ptr>>>
  mmap_any(os_access const& os) const = 0;
};

using sortable_inode_span =
    sortable_span<std::shared_ptr<inode> const, uint32_t>;

} // namespace internal
} // namespace writer
} // namespace dwarfs

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

#include <exception>
#include <iosfwd>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <folly/small_vector.h>

#include "dwarfs/inode_fragments.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/object.h"
#include "dwarfs/sortable_span.h"

namespace dwarfs {

namespace thrift::metadata {
class chunk;
}

class file;
class mmif;
class os_access;
class progress;

struct inode_options;

class inode : public object {
 public:
  using files_vector = folly::small_vector<file*, 1>;

  virtual void set_files(files_vector&& fv) = 0;
  virtual void populate(size_t size) = 0;
  virtual void scan(mmif* mm, inode_options const& options, progress& prog) = 0;
  virtual void set_num(uint32_t num) = 0;
  virtual uint32_t num() const = 0;
  virtual bool has_category(fragment_category cat) const = 0;
  virtual std::optional<uint32_t>
  similarity_hash(fragment_category cat) const = 0;
  virtual nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const = 0;
  virtual size_t size() const = 0;
  virtual file const* any() const = 0;
  virtual files_vector const& all() const = 0;
  virtual bool
  append_chunks_to(std::vector<thrift::metadata::chunk>& vec) const = 0;
  virtual inode_fragments& fragments() = 0;
  virtual void dump(std::ostream& os, inode_options const& options) const = 0;
  virtual void set_scan_error(file const* fp, std::exception_ptr ep) = 0;
  virtual std::optional<std::pair<file const*, std::exception_ptr>>
  get_scan_error() const = 0;
  virtual std::tuple<std::unique_ptr<mmif>, file const*,
                     std::vector<std::pair<file const*, std::exception_ptr>>>
  mmap_any(os_access const& os) const = 0;
};

using sortable_inode_span =
    sortable_span<std::shared_ptr<inode> const, uint32_t>;

} // namespace dwarfs

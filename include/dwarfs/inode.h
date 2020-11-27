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
#include <vector>

#include "dwarfs/fstypes.h"

namespace dwarfs {

namespace thrift::metadata {
struct chunk;
}

class file;
class file_interface;

class inode : public file_interface {
 public:
  virtual void set_file(const file* f) = 0;
  virtual void set_num(uint32_t num) = 0;
  virtual uint32_t num() const = 0;
  virtual uint32_t similarity_hash() const = 0;
  virtual const file_interface* any() const = 0; // TODO
  virtual void add_chunk(size_t block, size_t offset, size_t size) = 0;
  virtual const std::vector<chunk_type>& chunks() const = 0;
  virtual void
  append_chunks(std::vector<thrift::metadata::chunk>& vec) const = 0;
};
} // namespace dwarfs

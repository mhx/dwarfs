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

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/compiler.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/small_vector.h>

#include <dwarfs/writer/internal/entry_id.h>
#include <dwarfs/writer/internal/entry_type.h>
#include <dwarfs/writer/internal/inode_id.h>

namespace dwarfs {

namespace thrift::metadata {

class inode_data;
class metadata;

} // namespace thrift::metadata

class os_access;

namespace writer::internal {

class file;
class link;
class dir;
class device;
class other;

class entry_storage;
class global_entry_data;
class inode;
class progress;
class time_resolution_converter;

class entry {
 public:
  using type_t = entry_type;

  virtual ~entry() = default;

  entry(file_stat const& st);

  virtual void scan(entry_storage& storage, entry_id self_id,
                    os_access const& os, progress& prog) = 0;
  void set_entry_index(uint32_t index) { entry_index_ = index; }
  std::optional<uint32_t> const& entry_index() const { return entry_index_; }

 private:
  friend class device;

  std::optional<uint32_t> entry_index_;
};

class file : public entry {
 public:
  using entry::entry;

  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;

  void set_order_index(uint32_t index) { order_index_ = index; }
  uint32_t order_index() const { return order_index_; }

 private:
  uint32_t order_index_{0};
};

class dir : public entry {
 public:
  using entry::entry;

  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
};

class link : public entry {
 public:
  using entry::entry;

  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
};

/**
 * A `device` represents a character or block device.
 */
class device : public entry {
 public:
  using entry::entry;

  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
};

/**
 * An `other` entry is a FIFO or socket.
 */
class other : public entry {
 public:
  using entry::entry;

  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
};

} // namespace writer::internal

} // namespace dwarfs

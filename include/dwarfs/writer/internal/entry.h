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

#include <atomic>
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

#include <parallel_hashmap/phmap.h>

#include <dwarfs/compiler.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/writer/entry_id.h>
#include <dwarfs/writer/entry_type.h>
#include <dwarfs/writer/unique_inode_id.h>

namespace dwarfs {

namespace thrift::metadata {

class inode_data;
class metadata;

} // namespace thrift::metadata

class os_access;

namespace writer {

class entry_storage;

} // namespace writer

namespace writer::internal {

class file;
class link;
class dir;
class device;
class other;
class global_entry_data;
class inode;
class progress;
class time_resolution_converter;

class entry {
 public:
  using type_t = entry_type;

  virtual ~entry() = default;

  entry(std::filesystem::path const& path, file_stat const& st,
        entry_id parent_id);

  bool has_parent() const;
  entry_id parent_id() const { return parent_id_; }
  std::filesystem::path name_as_path() const;
  std::string_view name() const;
  file_size_t size() const;
  file_size_t allocated_size() const;
  virtual type_t type() const = 0;
  void
  pack(thrift::metadata::inode_data& entry_v2, global_entry_data const& data,
       time_resolution_converter const& timeres) const;
  void update(global_entry_data& data) const;
  virtual void scan(entry_storage& storage, entry_id self_id,
                    os_access const& os, progress& prog) = 0;
  file_stat const& status() const { return stat_; }
  void set_entry_index(uint32_t index) { entry_index_ = index; }
  std::optional<uint32_t> const& entry_index() const { return entry_index_; }
  unique_inode_id inode_id() const;
  uint64_t num_hard_links() const;
  virtual void set_inode_num(entry_storage& storage, uint32_t ino) = 0;
  virtual std::optional<uint32_t> const&
  inode_num(entry_storage& storage) const = 0;

  void set_empty();

 private:
#ifdef _WIN32
  std::filesystem::path path_;
#endif
  std::string name_;
  entry_id parent_id_;
  file_stat stat_;
  std::optional<uint32_t> entry_index_;
};

struct file_data {
  using hash_type = small_vector<char, 16>;
  hash_type hash;
  uint32_t hardlink_count{1};
  std::optional<uint32_t> inode_num;
  std::atomic<bool> invalid{false};
};

class file : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  std::string_view hash(entry_storage& storage) const;
  void set_inode(inode* ino);
  inode* get_inode() const;
  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
  void scan(entry_storage& storage, entry_id self_id, file_view const& mm,
            progress& prog, std::optional<std::string> const& hash_alg);
  void create_data(entry_storage& storage);
  void hardlink(entry_storage& storage, file* other, progress& prog);
  uint32_t unique_file_id() const;

  void set_inode_num(entry_storage& storage, uint32_t ino) override;
  std::optional<uint32_t> const&
  inode_num(entry_storage& storage) const override;

  void set_invalid(entry_storage& storage) {
    get_data(storage).invalid.store(true);
  }
  bool is_invalid(entry_storage& storage) const {
    return get_data(storage).invalid.load();
  }

  uint32_t hardlink_count(entry_storage& storage) const {
    return get_data(storage).hardlink_count;
  }

  void set_order_index(uint32_t index) { order_index_ = index; }
  uint32_t order_index() const { return order_index_; }

 private:
  static constexpr auto kInvalidDataIndex =
      std::numeric_limits<uint32_t>::max();

  file_data& get_data(entry_storage& storage) const;

  inode* inode_{};
  uint32_t order_index_{0};
  uint32_t data_index_{kInvalidDataIndex};
};

class dir : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void add(entry_handle e);
  void sort(entry_storage& storage);
  void pack_entry(entry_storage& storage, thrift::metadata::metadata& mv2,
                  global_entry_data const& data,
                  time_resolution_converter const& timeres) const;
  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
  bool empty() const { return entries_.empty(); }
  void remove_empty_dirs(entry_storage& storage, progress& prog);

  void set_inode_num(entry_storage&, uint32_t ino) override {
    inode_num_ = ino;
  }
  std::optional<uint32_t> const& inode_num(entry_storage&) const override {
    return inode_num_;
  }

  entry_id find(entry_storage& storage, std::filesystem::path const& path);

  void for_each_child(std::function<void(entry_id)> const& f) const;

 private:
  static constexpr size_t kLookupTableSizeThreshold = 16;

  using lookup_table = phmap::flat_hash_map<std::string_view, entry_id>;

  void populate_lookup_table(entry_storage& storage);

  std::vector<entry_id> entries_;
  std::optional<uint32_t> inode_num_;
  std::unique_ptr<lookup_table> lookup_;
};

class link : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  std::string const& linkname() const;
  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;

  void set_inode_num(entry_storage&, uint32_t ino) override {
    inode_num_ = ino;
  }
  std::optional<uint32_t> const& inode_num(entry_storage&) const override {
    return inode_num_;
  }

 private:
  std::string link_;
  std::optional<uint32_t> inode_num_;
};

/**
 * A `device` represents a character or block device.
 */
class device : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;
  uint64_t device_id() const;

  void set_inode_num(entry_storage&, uint32_t ino) override {
    inode_num_ = ino;
  }
  std::optional<uint32_t> const& inode_num(entry_storage&) const override {
    return inode_num_;
  }

 private:
  std::optional<uint32_t> inode_num_;
};

/**
 * An `other` entry is a FIFO or socket.
 */
class other : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void scan(entry_storage& storage, entry_id self_id, os_access const& os,
            progress& prog) override;

  void set_inode_num(entry_storage&, uint32_t ino) override {
    inode_num_ = ino;
  }
  std::optional<uint32_t> const& inode_num(entry_storage&) const override {
    return inode_num_;
  }

 private:
  std::optional<uint32_t> inode_num_;
};

} // namespace writer::internal

} // namespace dwarfs

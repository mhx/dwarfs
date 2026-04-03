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
#include <unordered_map>
#include <vector>

#include <dwarfs/compiler.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/writer/entry_type.h>
#include <dwarfs/writer/unique_inode_id.h>

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
class global_entry_data;
class inode;
class progress;
class time_resolution_converter;

class entry {
 public:
  using type_t = entry_type;

  virtual ~entry() = default;

  entry(std::filesystem::path const& path, entry* parent, file_stat const& st);

  bool has_parent() const;
  entry* parent() const;
  std::filesystem::path fs_path() const;
  std::string path_as_string() const;
  std::string unix_dpath() const;
  std::string_view name() const;
  bool less_revpath(entry const& rhs) const;
  file_size_t size() const;
  file_size_t allocated_size() const;
  virtual type_t type() const = 0;
  bool is_directory() const;
  void
  pack(thrift::metadata::inode_data& entry_v2, global_entry_data const& data,
       time_resolution_converter const& timeres) const;
  void update(global_entry_data& data) const;
  virtual void scan(os_access const& os, progress& prog) = 0;
  file_stat const& status() const { return stat_; }
  void set_entry_index(uint32_t index) { entry_index_ = index; }
  std::optional<uint32_t> const& entry_index() const { return entry_index_; }
  unique_inode_id inode_id() const;
  uint64_t num_hard_links() const;
  virtual void set_inode_num(uint32_t ino) = 0;
  virtual std::optional<uint32_t> const& inode_num() const = 0;

  void set_empty();

  bool is_file() const noexcept { return type() == entry_type::E_FILE; }
  bool is_dir() const noexcept { return type() == entry_type::E_DIR; }
  bool is_link() const noexcept { return type() == entry_type::E_LINK; }
  bool is_device() const noexcept { return type() == entry_type::E_DEVICE; }
  bool is_other() const noexcept { return type() == entry_type::E_OTHER; }

  file* as_file() noexcept;
  dir* as_dir() noexcept;
  link* as_link() noexcept;
  device* as_device() noexcept;

  file const* as_file() const noexcept;
  dir const* as_dir() const noexcept;
  link const* as_link() const noexcept;
  device const* as_device() const noexcept;

 private:
#ifdef _WIN32
  std::filesystem::path path_;
#endif
  std::string name_;
  entry* parent_{nullptr};
  file_stat stat_;
  std::optional<uint32_t> entry_index_;
};

class file : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  std::string_view hash() const;
  void set_inode(std::shared_ptr<inode> ino);
  std::shared_ptr<inode> get_inode() const;
  void scan(os_access const& os, progress& prog) override;
  void scan(file_view const& mm, progress& prog,
            std::optional<std::string> const& hash_alg);
  void create_data();
  void hardlink(file* other, progress& prog);
  uint32_t unique_file_id() const;

  void set_inode_num(uint32_t ino) override;
  std::optional<uint32_t> const& inode_num() const override;

  void set_invalid() { data_->invalid.store(true); }
  bool is_invalid() const { return data_->invalid.load(); }

  uint32_t hardlink_count() const { return data_->hardlink_count; }

  void set_order_index(uint32_t index) { order_index_ = index; }
  uint32_t order_index() const { return order_index_; }

 private:
  struct data {
    using hash_type = small_vector<char, 16>;
    hash_type hash;
    uint32_t hardlink_count{1};
    std::optional<uint32_t> inode_num;
    std::atomic<bool> invalid{false};
  };

  std::shared_ptr<data> data_;
  std::shared_ptr<inode> inode_;
  uint32_t order_index_{0};
};

class dir : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void add(entry* e);
  void sort();
  void pack(thrift::metadata::metadata& mv2, global_entry_data const& data,
            time_resolution_converter const& timeres) const;
  void
  pack_entry(thrift::metadata::metadata& mv2, global_entry_data const& data,
             time_resolution_converter const& timeres) const;
  void scan(os_access const& os, progress& prog) override;
  bool empty() const { return entries_.empty(); }
  void remove_empty_dirs(progress& prog);

  void set_inode_num(uint32_t ino) override { inode_num_ = ino; }
  std::optional<uint32_t> const& inode_num() const override {
    return inode_num_;
  }

  entry* find(std::filesystem::path const& path);

  void for_each_child(std::function<void(entry*)> const& f);

 private:
  using entry_ptr = entry*;
  using lookup_table = std::unordered_map<std::string_view, entry_ptr>;

  void populate_lookup_table();

  std::vector<entry_ptr> entries_;
  std::optional<uint32_t> inode_num_;
  std::unique_ptr<lookup_table> lookup_;
};

class link : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  std::string const& linkname() const;
  void scan(os_access const& os, progress& prog) override;

  void set_inode_num(uint32_t ino) override { inode_num_ = ino; }
  std::optional<uint32_t> const& inode_num() const override {
    return inode_num_;
  }

 private:
  std::string link_;
  std::optional<uint32_t> inode_num_;
};

/**
 * A `device` actually represents anything that's not a file,
 * dir or link.
 */
class device : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void scan(os_access const& os, progress& prog) override;
  uint64_t device_id() const;

  void set_inode_num(uint32_t ino) override { inode_num_ = ino; }
  std::optional<uint32_t> const& inode_num() const override {
    return inode_num_;
  }

 private:
  std::optional<uint32_t> inode_num_;
};

} // namespace writer::internal

} // namespace dwarfs

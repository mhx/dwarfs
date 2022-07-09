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

#include <folly/portability/SysStat.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dwarfs/entry_interface.h"

namespace dwarfs {

namespace thrift::metadata {

class inode_data;
class metadata;

} // namespace thrift::metadata

class file;
class link;
class dir;
class device;
class inode;
class mmif;
class os_access;
class progress;
class global_entry_data;

class entry_visitor {
 public:
  virtual ~entry_visitor() = default;
  virtual void visit(file* p) = 0;
  virtual void visit(device* p) = 0;
  virtual void visit(link* p) = 0;
  virtual void visit(dir* p) = 0;
};

class entry : public entry_interface {
 public:
  enum type_t { E_FILE, E_DIR, E_LINK, E_DEVICE, E_OTHER };

  entry(const std::string& name, std::shared_ptr<entry> parent,
        const struct ::stat& st);

  bool has_parent() const;
  std::shared_ptr<entry> parent() const;
  void set_name(const std::string& name);
  std::string path() const override;
  const std::string& name() const override { return name_; }
  size_t size() const override { return stat_.st_size; }
  virtual type_t type() const = 0;
  std::string type_string() const override;
  virtual void walk(std::function<void(entry*)> const& f);
  virtual void walk(std::function<void(const entry*)> const& f) const;
  void pack(thrift::metadata::inode_data& entry_v2,
            global_entry_data const& data) const;
  void update(global_entry_data& data) const;
  virtual void accept(entry_visitor& v, bool preorder = false) = 0;
  virtual void scan(os_access& os, progress& prog) = 0;
  const struct ::stat& status() const { return stat_; }
  void set_entry_index(uint32_t index) { entry_index_ = index; }
  std::optional<uint32_t> const& entry_index() const { return entry_index_; }
  uint64_t raw_inode_num() const { return stat_.st_ino; }
  uint64_t num_hard_links() const { return stat_.st_nlink; }
  virtual void set_inode_num(uint32_t ino) = 0;
  virtual std::optional<uint32_t> const& inode_num() const = 0;

  // more methods from entry_interface
  uint16_t get_permissions() const override;
  void set_permissions(uint16_t perm) override;
  uint16_t get_uid() const override;
  void set_uid(uint16_t uid) override;
  uint16_t get_gid() const override;
  void set_gid(uint16_t gid) override;
  uint64_t get_atime() const override;
  void set_atime(uint64_t atime) override;
  uint64_t get_mtime() const override;
  void set_mtime(uint64_t mtime) override;
  uint64_t get_ctime() const override;
  void set_ctime(uint64_t ctime) override;

 private:
  std::string name_;
  std::weak_ptr<entry> parent_;
  struct ::stat stat_;
  std::optional<uint32_t> entry_index_;
};

class file : public entry {
 public:
  file(const std::string& name, std::shared_ptr<entry> parent,
       const struct ::stat& st)
      : entry(name, std::move(parent), st) {}

  type_t type() const override;
  std::string_view hash() const;
  void set_inode(std::shared_ptr<inode> ino);
  std::shared_ptr<inode> get_inode() const;
  void accept(entry_visitor& v, bool preorder) override;
  void scan(os_access& os, progress& prog) override;
  void scan(std::shared_ptr<mmif> const& mm, progress& prog);
  void create_data();
  void hardlink(file* other, progress& prog);
  uint32_t unique_file_id() const;

  void set_inode_num(uint32_t ino) override;
  std::optional<uint32_t> const& inode_num() const override;

  uint32_t refcount() const { return data_->refcount; }

 private:
  struct data {
    using hash_type = std::array<char, 16>;
    hash_type hash{0};
    uint32_t refcount{1};
    std::optional<uint32_t> inode_num;
  };

  std::shared_ptr<data> data_;
  std::shared_ptr<inode> inode_;
};

class dir : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void add(std::shared_ptr<entry> e);
  void walk(std::function<void(entry*)> const& f) override;
  void walk(std::function<void(const entry*)> const& f) const override;
  void accept(entry_visitor& v, bool preorder) override;
  void sort();
  void
  pack(thrift::metadata::metadata& mv2, global_entry_data const& data) const;
  void pack_entry(thrift::metadata::metadata& mv2,
                  global_entry_data const& data) const;
  void scan(os_access& os, progress& prog) override;
  bool empty() const { return entries_.empty(); }
  void remove_empty_dirs(progress& prog);

  void set_inode_num(uint32_t ino) override { inode_num_ = ino; }
  std::optional<uint32_t> const& inode_num() const override {
    return inode_num_;
  }

 private:
  using entry_ptr = std::shared_ptr<entry>;

  std::vector<std::shared_ptr<entry>> entries_;
  std::optional<uint32_t> inode_num_;
};

class link : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  const std::string& linkname() const;
  void accept(entry_visitor& v, bool preorder) override;
  void scan(os_access& os, progress& prog) override;

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
  void accept(entry_visitor& v, bool preorder) override;
  void scan(os_access& os, progress& prog) override;
  uint64_t device_id() const;

  void set_inode_num(uint32_t ino) override { inode_num_ = ino; }
  std::optional<uint32_t> const& inode_num() const override {
    return inode_num_;
  }

 private:
  std::optional<uint32_t> inode_num_;
};

class entry_factory {
 public:
  static std::unique_ptr<entry_factory> create();

  virtual ~entry_factory() = default;

  virtual std::shared_ptr<entry>
  create(os_access& os, const std::string& name,
         std::shared_ptr<entry> parent = nullptr) = 0;
};
} // namespace dwarfs

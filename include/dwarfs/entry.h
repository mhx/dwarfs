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

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>

#include "dwarfs/file_interface.h"

namespace dwarfs {

namespace thrift::metadata {

class entry;
class metadata;

} // namespace thrift::metadata

class file;
class link;
class dir;
class inode;
class os_access;
class progress;
class global_entry_data;

class entry_visitor {
 public:
  virtual ~entry_visitor() = default;
  virtual void visit(file* p) = 0;
  virtual void visit(link* p) = 0;
  virtual void visit(dir* p) = 0;
};

class entry : public file_interface {
 public:
  enum type_t { E_FILE, E_DIR, E_LINK };

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
  void
  pack(thrift::metadata::entry& entry_v2, global_entry_data const& data) const;
  void update(global_entry_data& data) const;
  virtual void accept(entry_visitor& v, bool preorder = false) = 0;
  virtual uint32_t inode_num() const = 0;
  virtual void scan(os_access& os, progress& prog) = 0;
  const struct ::stat& status() const { return stat_; }

 private:
  std::string name_;
  std::weak_ptr<entry> parent_;
  struct ::stat stat_;
};

class file : public entry {
 public:
  file(const std::string& name, std::shared_ptr<entry> parent,
       const struct ::stat& st, bool with_similarity)
      : entry(name, parent, st)
      , with_similarity_(with_similarity) {}

  type_t type() const override;
  std::string_view hash() const;
  void set_inode(std::shared_ptr<inode> ino);
  std::shared_ptr<inode> get_inode() const;
  void accept(entry_visitor& v, bool preorder) override;
  uint32_t inode_num() const override;
  uint32_t similarity_hash() const { return similarity_hash_; }
  void scan(os_access& os, progress& prog) override;

 private:
  using hash_type = std::array<char, 20>;

  uint32_t similarity_hash_{0};
  const bool with_similarity_;
  hash_type hash_{0};
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
  void set_inode(uint32_t inode);
  void
  pack(thrift::metadata::metadata& mv2, global_entry_data const& data) const;
  void pack_entry(thrift::metadata::metadata& mv2,
                  global_entry_data const& data) const;
  uint32_t inode_num() const override { return inode_; }
  void scan(os_access& os, progress& prog) override;

 private:
  using entry_ptr = std::shared_ptr<entry>;

  std::vector<std::shared_ptr<entry>> entries_;
  uint32_t inode_{0};
};

class link : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  const std::string& linkname() const;
  void set_inode(uint32_t inode);
  void accept(entry_visitor& v, bool preorder) override;
  uint32_t inode_num() const override { return inode_; }
  void scan(os_access& os, progress& prog) override;

 private:
  std::string link_;
  uint32_t inode_{0};
};

class entry_factory {
 public:
  static std::unique_ptr<entry_factory> create(bool with_similarity = false);

  virtual ~entry_factory() = default;

  virtual std::shared_ptr<entry>
  create(os_access& os, const std::string& name,
         std::shared_ptr<entry> parent = std::shared_ptr<entry>()) = 0;
};
} // namespace dwarfs

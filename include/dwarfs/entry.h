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
#include <functional>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#include "dwarfs/file_interface.h"
#include "dwarfs/fstypes.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

struct global_entry_data {
  global_entry_data(bool no_time) : no_time_(no_time) {}

  void add_uid(uint16_t uid) { add(uid, uids, next_uid_index); }

  void add_gid(uint16_t gid) { add(gid, gids, next_gid_index); }

  void add_mode(uint16_t mode) { add(mode, modes, next_mode_index); }

  void add(uint16_t val, std::unordered_map<uint16_t, uint16_t>& map,
           uint16_t& next_index) {
    if (map.emplace(val, next_index).second) {
      ++next_index;
    }
  }

  void add_time(uint64_t time) {
    if (time < timestamp_base) {
      timestamp_base = time;
    }
  }

  void add_name(std::string const& name) { names.emplace(name, 0); }

  void add_link(std::string const& link) { links.emplace(link, 0); }

  void index() {
    index(names);
    index(links);
  }

  void index(std::unordered_map<std::string, uint32_t>& map);

  uint16_t get_uid_index(uint16_t uid) const { return uids.at(uid); }

  uint16_t get_gid_index(uint16_t gid) const { return gids.at(gid); }

  uint16_t get_mode_index(uint16_t mode) const { return modes.at(mode); }

  uint32_t get_name_index(std::string const& name) const {
    return names.at(name);
  }

  uint32_t get_link_index(std::string const& link) const {
    return links.at(link);
  }

  uint64_t get_time_offset(uint64_t time) const {
    return no_time_ ? 0 : time - timestamp_base;
  }

  std::vector<uint16_t> get_uids() const;

  std::vector<uint16_t> get_gids() const;

  std::vector<uint16_t> get_modes() const;

  std::vector<std::string> get_names() const;

  std::vector<std::string> get_links() const;

  // TODO: make private
  template <typename T, typename U>
  std::vector<T> get_vector(std::unordered_map<T, U> const& map) const;

  std::unordered_map<uint16_t, uint16_t> uids;
  std::unordered_map<uint16_t, uint16_t> gids;
  std::unordered_map<uint16_t, uint16_t> modes;
  std::unordered_map<std::string, uint32_t> names;
  std::unordered_map<std::string, uint32_t> links;
  uint16_t next_uid_index{0};
  uint16_t next_gid_index{0};
  uint16_t next_mode_index{0};
  uint64_t timestamp_base{std::numeric_limits<uint64_t>::max()};
  bool no_time_;
};

class file;
class link;
class dir;
class inode;
class os_access;
class progress;

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

  void scan(os_access& os, progress& prog);
  bool has_parent() const;
  std::shared_ptr<entry> parent() const;
  void set_name(const std::string& name);
  void set_name_offset(size_t offset);
  std::string path() const override;
  const std::string& name() const override { return name_; }
  size_t size() const override { return stat_.st_size; }
  virtual type_t type() const = 0;
  std::string type_string() const override;
  virtual size_t total_size() const;
  virtual void walk(std::function<void(entry*)> const& f);
  virtual void walk(std::function<void(const entry*)> const& f) const;
  void pack(dir_entry& de) const;
  void pack(dir_entry_ug& de) const;
  void pack(dir_entry_ug_time& de) const;
  void
  pack(thrift::metadata::entry& entry_v2, global_entry_data const& data) const;
  void update(global_entry_data& data) const;
  virtual void accept(entry_visitor& v, bool preorder = false) = 0;
  virtual uint32_t inode_num() const = 0;

 protected:
  virtual void pack_specific(dir_entry& de) const = 0;
  virtual void scan(os_access& os, const std::string& p, progress& prog) = 0;

 private:
  std::string name_;
  std::weak_ptr<entry> parent_;
  struct ::stat stat_;
  uint32_t name_offset_;
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

 protected:
  void pack_specific(dir_entry& de) const override;
  void scan(os_access& os, const std::string& p, progress& prog) override;

 private:
  uint32_t similarity_hash_{0};
  const bool with_similarity_;
  std::array<char, 20> hash_{0};
  std::shared_ptr<inode> inode_;
};

class dir : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  void add(std::shared_ptr<entry> e);
  size_t total_size() const override;
  void walk(std::function<void(entry*)> const& f) override;
  void walk(std::function<void(const entry*)> const& f) const override;
  void accept(entry_visitor& v, bool preorder) override;
  void sort();
  void set_offset(size_t offset);
  void set_inode(uint32_t inode);
  virtual size_t packed_size() const = 0;
  virtual void
  pack(uint8_t* buf,
       std::function<void(const entry* e, size_t offset)> const& offset_cb)
      const = 0;
  virtual void pack(thrift::metadata::metadata& mv2,
                    global_entry_data const& data) const = 0;
  virtual size_t packed_entry_size() const = 0;
  virtual void pack_entry(uint8_t* buf) const = 0;
  virtual void pack_entry(thrift::metadata::metadata& mv2,
                          global_entry_data const& data) const = 0;
  uint32_t inode_num() const override { return inode_; }

 protected:
  void pack_specific(dir_entry& de) const override;
  void scan(os_access& os, const std::string& p, progress& prog) override;

  using entry_ptr = std::shared_ptr<entry>;

  std::vector<std::shared_ptr<entry>> entries_;
  uint32_t offset_ = 0;
  uint32_t inode_ = 0;
};

class link : public entry {
 public:
  using entry::entry;

  type_t type() const override;
  const std::string& linkname() const;
  void set_offset(size_t offset);
  void set_inode(uint32_t inode);
  void accept(entry_visitor& v, bool preorder) override;
  uint32_t inode_num() const override { return inode_; }

 protected:
  void pack_specific(dir_entry& de) const override;
  void scan(os_access& os, const std::string& p, progress& prog) override;

 private:
  std::string link_;
  uint32_t offset_ = 0;
  uint32_t inode_ = 0;
};

class entry_factory {
 public:
  static std::shared_ptr<entry_factory>
  create(bool no_owner = false, bool no_time = false,
         bool with_similarity = false);

  virtual ~entry_factory() = default;

  virtual std::shared_ptr<entry>
  create(os_access& os, const std::string& name,
         std::shared_ptr<entry> parent = std::shared_ptr<entry>()) = 0;
  virtual dir_entry_type de_type() const = 0;
};
} // namespace dwarfs

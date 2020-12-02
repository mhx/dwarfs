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

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <openssl/sha.h>

#include "dwarfs/entry.h"
#include "dwarfs/global_entry_data.h"
#include "dwarfs/inode.h"
#include "dwarfs/mmif.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/similarity.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

entry::entry(const std::string& name, std::shared_ptr<entry> parent,
             const struct ::stat& st)
    : name_(name)
    , parent_(std::move(parent))
    , stat_(st) {}

bool entry::has_parent() const {
  if (parent_.lock()) {
    return true;
  }

  return false;
}

std::shared_ptr<entry> entry::parent() const { return parent_.lock(); }

void entry::set_name(const std::string& name) { name_ = name; }

std::string entry::path() const {
  if (auto parent = parent_.lock()) {
    return parent->path() + "/" + name_;
  }

  return name_;
}

std::string entry::type_string() const {
  // TODO: this type stuff is a mess, see if we really need it
  switch (type()) {
  case E_FILE:
    return "file";
  case E_LINK:
    return "link";
  case E_DIR:
    return "dir";
  case E_DEVICE:
    return "device";
  case E_OTHER:
    return "pipe/socket";
  default:
    throw std::runtime_error("invalid file type");
  }
}

void entry::walk(std::function<void(entry*)> const& f) { f(this); }

void entry::walk(std::function<void(const entry*)> const& f) const { f(this); }

void entry::update(global_entry_data& data) const {
  data.add_uid(stat_.st_uid);
  data.add_gid(stat_.st_gid);
  data.add_mode(stat_.st_mode & 0xFFFF);
  data.add_time(stat_.st_atime);
  data.add_time(stat_.st_mtime);
  data.add_time(stat_.st_ctime);
}

void entry::pack(thrift::metadata::entry& entry_v2,
                 global_entry_data const& data) const {
  entry_v2.name_index = has_parent() ? data.get_name_index(name_) : 0;
  entry_v2.mode_index = data.get_mode_index(stat_.st_mode & 0xFFFF);
  entry_v2.owner_index = data.get_uid_index(stat_.st_uid);
  entry_v2.group_index = data.get_gid_index(stat_.st_gid);
  entry_v2.atime_offset = data.get_time_offset(stat_.st_atime);
  entry_v2.mtime_offset = data.get_time_offset(stat_.st_mtime);
  entry_v2.ctime_offset = data.get_time_offset(stat_.st_ctime);
  entry_v2.inode = inode_num();
}

entry::type_t file::type() const { return E_FILE; }

std::string_view file::hash() const {
  return std::string_view(&hash_[0], hash_.size());
}

void file::set_inode(std::shared_ptr<inode> ino) {
  if (inode_) {
    throw std::runtime_error("inode already set for file");
  }

  inode_ = std::move(ino);
}

std::shared_ptr<inode> file::get_inode() const { return inode_; }

uint32_t file::inode_num() const { return inode_->num(); }

void file::accept(entry_visitor& v, bool) { v.visit(this); }

void file::scan(os_access& os, progress& prog) {
  static_assert(SHA_DIGEST_LENGTH == sizeof(hash_type));

  if (size_t s = size(); s > 0) {
    prog.original_size += s;
    auto mm = os.map_file(path(), s);
    ::SHA1(mm->as<unsigned char>(), s,
           reinterpret_cast<unsigned char*>(&hash_[0]));

    if (with_similarity_) {
      similarity_hash_ = get_similarity_hash(mm->as<uint8_t>(), s);
    }
  } else {
    ::SHA1(nullptr, 0, reinterpret_cast<unsigned char*>(&hash_[0]));
  }
}

entry::type_t dir::type() const { return E_DIR; }

void dir::add(std::shared_ptr<entry> e) { entries_.emplace_back(std::move(e)); }

void dir::walk(std::function<void(entry*)> const& f) {
  f(this);

  for (entry_ptr const& e : entries_) {
    e->walk(f);
  }
}

void dir::walk(std::function<void(const entry*)> const& f) const {
  f(this);

  for (entry_ptr const& e : entries_) {
    const_cast<entry const*>(e.get())->walk(f);
  }
}

void dir::accept(entry_visitor& v, bool preorder) {
  if (preorder) {
    v.visit(this);
  }

  for (entry_ptr const& e : entries_) {
    e->accept(v, preorder);
  }

  if (!preorder) {
    v.visit(this);
  }
}

void dir::sort() {
  std::sort(entries_.begin(), entries_.end(),
            [](entry_ptr const& a, entry_ptr const& b) {
              return a->name() < b->name();
            });
}

void dir::set_inode(uint32_t inode) { inode_ = inode; }

void dir::scan(os_access&, progress&) {}

void dir::pack_entry(thrift::metadata::metadata& mv2,
                     global_entry_data const& data) const {
  mv2.entry_index.at(inode_num()) = mv2.entries.size();
  mv2.entries.emplace_back();
  entry::pack(mv2.entries.back(), data);
}

void dir::pack(thrift::metadata::metadata& mv2,
               global_entry_data const& data) const {
  thrift::metadata::directory d;
  d.parent_inode =
      has_parent() ? std::dynamic_pointer_cast<dir>(parent())->inode_num() : 0;
  d.first_entry = mv2.entries.size();
  // d.entry_count = entries_.size();
  mv2.directories.push_back(d);
  for (entry_ptr const& e : entries_) {
    mv2.entry_index.at(e->inode_num()) = mv2.entries.size();
    mv2.entries.emplace_back();
    e->pack(mv2.entries.back(), data);
  }
}

void dir::remove_empty_dirs(progress& prog) {
  auto last = std::remove_if(entries_.begin(), entries_.end(),
                             [&](std::shared_ptr<entry> const& e) {
                               if (auto d = dynamic_cast<dir*>(e.get())) {
                                 d->remove_empty_dirs(prog);
                                 return d->empty();
                               }
                               return false;
                             });

  if (last != entries_.end()) {
    auto num = std::distance(last, entries_.end());
    prog.dirs_scanned -= num;
    prog.dirs_found -= num;
    entries_.erase(last, entries_.end());
  }
}

entry::type_t link::type() const { return E_LINK; }

const std::string& link::linkname() const { return link_; }

void link::set_inode(uint32_t inode) { inode_ = inode; }

void link::accept(entry_visitor& v, bool) { v.visit(this); }

void link::scan(os_access& os, progress& prog) {
  link_ = os.readlink(path(), size());
  prog.original_size += size();
}

entry::type_t device::type() const {
  auto mode = status().st_mode;
  return S_ISCHR(mode) || S_ISBLK(mode) ? E_DEVICE : E_OTHER;
}

void device::set_inode(uint32_t inode) { inode_ = inode; }

void device::accept(entry_visitor& v, bool) { v.visit(this); }

void device::scan(os_access&, progress&) {}

uint64_t device::device_id() const { return status().st_rdev; }

class entry_factory_ : public entry_factory {
 public:
  entry_factory_(bool with_similarity)
      : with_similarity_(with_similarity) {}

  std::shared_ptr<entry> create(os_access& os, const std::string& name,
                                std::shared_ptr<entry> parent) override {
    const std::string& p = parent ? parent->path() + "/" + name : name;
    struct ::stat st;

    os.lstat(p, &st);
    auto mode = st.st_mode;

    if (S_ISREG(mode)) {
      return std::make_shared<file>(name, std::move(parent), st,
                                    with_similarity_);
    } else if (S_ISDIR(mode)) {
      return std::make_shared<dir>(name, std::move(parent), st);
    } else if (S_ISLNK(mode)) {
      return std::make_shared<link>(name, std::move(parent), st);
    } else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) ||
               S_ISSOCK(mode)) {
      return std::make_shared<device>(name, std::move(parent), st);
    } else {
      // TODO: warn
    }

    return std::shared_ptr<entry>();
  }

 private:
  const bool with_similarity_;
};

std::unique_ptr<entry_factory> entry_factory::create(bool with_similarity) {
  return std::make_unique<entry_factory_>(with_similarity);
}
} // namespace dwarfs

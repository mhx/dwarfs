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

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <folly/Conv.h>
#include <folly/gen/Base.h>

#include <openssl/sha.h>

#include "dwarfs/entry.h"
#include "dwarfs/inode.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/similarity.h"

namespace dwarfs {

template <typename T, typename U>
std::vector<T>
global_entry_data::get_vector(std::unordered_map<T, U> const& map) const {
  using namespace folly::gen;
  std::vector<std::pair<T, U>> pairs(map.begin(), map.end());
  return from(pairs) | orderBy([](auto const& p) { return p.second; }) |
         get<0>() | as<std::vector>();
}

std::vector<uint16_t> global_entry_data::get_uids() const {
  return get_vector(uids);
}

std::vector<uint16_t> global_entry_data::get_gids() const {
  return get_vector(gids);
}

std::vector<uint16_t> global_entry_data::get_modes() const {
  return get_vector(modes);
}

std::vector<std::string> global_entry_data::get_names() const {
  return get_vector(names);
}

std::vector<std::string> global_entry_data::get_links() const {
  return get_vector(links);
}

void global_entry_data::index(std::unordered_map<std::string, uint32_t>& map) {
  using namespace folly::gen;
  uint32_t ix = 0;
  from(map) | get<0>() | order | [&](std::string const& s) { map[s] = ix++; };
}

entry::entry(const std::string& name, std::shared_ptr<entry> parent,
             const struct ::stat& st)
    : name_(name)
    , parent_(std::move(parent))
    , stat_(st)
    , name_offset_(0) {}

void entry::scan(os_access& os, progress& prog) {
  const std::string& p = path();
  os.lstat(p, &stat_);
  scan(os, p, prog);
}

bool entry::has_parent() const {
  if (parent_.lock()) {
    return true;
  }

  return false;
}

std::shared_ptr<entry> entry::parent() const { return parent_.lock(); }

void entry::set_name(const std::string& name) { name_ = name; }

void entry::set_name_offset(size_t offset) {
  name_offset_ = folly::to<uint32_t>(offset);
}

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
  default:
    throw std::runtime_error("invalid file type");
  }
}

size_t entry::total_size() const { return size(); }

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

void file::scan(os_access& os, const std::string& p, progress& prog) {
  assert(SHA_DIGEST_LENGTH == hash_.size());

  size_t s = size();

  if (s > 0) {
    prog.original_size += s;
    auto mm = os.map_file(p, s);
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

size_t dir::total_size() const {
  size_t total = 0;

  for (entry_ptr const& e : entries_) {
    total += e->total_size();
  }

  return total;
}

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

void dir::set_offset(size_t offset) { offset_ = folly::to<uint32_t>(offset); }

void dir::set_inode(uint32_t inode) { inode_ = inode; }

void dir::scan(os_access&, const std::string&, progress&) {}

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
  d.entry_count = entries_.size();
  mv2.directories.push_back(d);
  for (entry_ptr const& e : entries_) {
    mv2.entry_index.at(e->inode_num()) = mv2.entries.size();
    mv2.entries.emplace_back();
    e->pack(mv2.entries.back(), data);
  }
}

entry::type_t link::type() const { return E_LINK; }

const std::string& link::linkname() const { return link_; }

void link::set_offset(size_t offset) { offset_ = folly::to<uint32_t>(offset); }

void link::set_inode(uint32_t inode) { inode_ = inode; }

void link::accept(entry_visitor& v, bool) { v.visit(this); }

void link::scan(os_access& os, const std::string& p, progress& prog) {
  link_ = os.readlink(p, size());
  prog.original_size += size();
}

class entry_factory_ : public entry_factory {
 public:
  entry_factory_(bool with_similarity)
      : with_similarity_(with_similarity) {}

  std::shared_ptr<entry> create(os_access& os, const std::string& name,
                                std::shared_ptr<entry> parent) override {
    const std::string& p = parent ? parent->path() + "/" + name : name;
    struct ::stat st;

    os.lstat(p, &st);

    if (S_ISREG(st.st_mode)) {
      return std::make_shared<file>(name, std::move(parent), st,
                                    with_similarity_);
    } else if (S_ISDIR(st.st_mode)) {
      return std::make_shared<dir>(name, std::move(parent), st);
    } else if (S_ISLNK(st.st_mode)) {
      return std::make_shared<link>(name, std::move(parent), st);
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

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
#include <utility>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/global_entry_data.h"
#include "dwarfs/inode.h"
#include "dwarfs/mmif.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"

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
  auto mode = stat_.st_mode;

  if (S_ISREG(mode)) {
    return "file";
  } else if (S_ISDIR(mode)) {
    return "directory";
  } else if (S_ISLNK(mode)) {
    return "link";
  } else if (S_ISCHR(mode)) {
    return "chardev";
  } else if (S_ISBLK(mode)) {
    return "blockdev";
  } else if (S_ISFIFO(mode)) {
    return "fifo";
#ifndef _WIN32
  } else if (S_ISSOCK(mode)) {
    return "socket";
#endif
  }

  DWARFS_THROW(runtime_error, fmt::format("unknown file type: {:#06x}", mode));
}

void entry::walk(std::function<void(entry*)> const& f) { f(this); }

void entry::walk(std::function<void(const entry*)> const& f) const { f(this); }

void entry::update(global_entry_data& data) const {
  data.add_uid(stat_.st_uid);
  data.add_gid(stat_.st_gid);
  data.add_mode(stat_.st_mode & 0xFFFF);
  data.add_atime(stat_.st_atime);
  data.add_mtime(stat_.st_mtime);
  data.add_ctime(stat_.st_ctime);
}

void entry::pack(thrift::metadata::inode_data& entry_v2,
                 global_entry_data const& data) const {
  entry_v2.mode_index = data.get_mode_index(stat_.st_mode & 0xFFFF);
  entry_v2.owner_index = data.get_uid_index(stat_.st_uid);
  entry_v2.group_index = data.get_gid_index(stat_.st_gid);
  entry_v2.atime_offset = data.get_atime_offset(stat_.st_atime);
  entry_v2.mtime_offset = data.get_mtime_offset(stat_.st_mtime);
  entry_v2.ctime_offset = data.get_ctime_offset(stat_.st_ctime);
}

entry::type_t file::type() const { return E_FILE; }

uint16_t entry::get_permissions() const { return stat_.st_mode & 07777; }

void entry::set_permissions(uint16_t perm) {
  stat_.st_mode &= ~07777;
  stat_.st_mode |= perm;
}

uint16_t entry::get_uid() const { return stat_.st_uid; }

void entry::set_uid(uint16_t uid) { stat_.st_uid = uid; }

uint16_t entry::get_gid() const { return stat_.st_gid; }

void entry::set_gid(uint16_t gid) { stat_.st_gid = gid; }

uint64_t entry::get_atime() const { return stat_.st_atime; }

void entry::set_atime(uint64_t atime) { stat_.st_atime = atime; }

uint64_t entry::get_mtime() const { return stat_.st_mtime; }

void entry::set_mtime(uint64_t mtime) { stat_.st_atime = mtime; }

uint64_t entry::get_ctime() const { return stat_.st_ctime; }

void entry::set_ctime(uint64_t ctime) { stat_.st_atime = ctime; }

std::string_view file::hash() const {
  auto& h = data_->hash;
  return std::string_view(&h[0], h.size());
}

void file::set_inode(std::shared_ptr<inode> ino) {
  if (inode_) {
    DWARFS_THROW(runtime_error, "inode already set for file");
  }

  inode_ = std::move(ino);
}

std::shared_ptr<inode> file::get_inode() const { return inode_; }

void file::accept(entry_visitor& v, bool) { v.visit(this); }

void file::scan(os_access& os, progress& prog) {
  std::shared_ptr<mmif> mm;

  if (size_t s = size(); s > 0) {
    mm = os.map_file(path(), s);
  }

  scan(mm, prog);
}

void file::scan(std::shared_ptr<mmif> const& mm, progress& prog) {
  constexpr auto alg = checksum::algorithm::XXH3_128;
  static_assert(checksum::digest_size(alg) == sizeof(data::hash_type));

  if (size_t s = size(); s > 0) {
    constexpr size_t chunk_size = 32 << 20;
    prog.original_size += s;
    checksum cs(alg);
    size_t offset = 0;

    while (s >= chunk_size) {
      cs.update(mm->as<void>(offset), chunk_size);
      mm->release_until(offset);
      offset += chunk_size;
      s -= chunk_size;
    }

    cs.update(mm->as<void>(offset), s);

    DWARFS_CHECK(cs.finalize(&data_->hash[0]), "checksum computation failed");
  } else {
    DWARFS_CHECK(checksum::compute(alg, nullptr, 0, &data_->hash[0]),
                 "checksum computation failed");
  }
}

uint32_t file::unique_file_id() const { return inode_->num(); }

void file::set_inode_num(uint32_t inode_num) {
  DWARFS_CHECK(data_, "file data unset");
  DWARFS_CHECK(!data_->inode_num, "attempt to set inode number more than once");
  data_->inode_num = inode_num;
}

std::optional<uint32_t> const& file::inode_num() const {
  DWARFS_CHECK(data_, "file data unset");
  return data_->inode_num;
}

void file::create_data() {
  assert(!data_);
  data_ = std::make_shared<data>();
}

void file::hardlink(file* other, progress& prog) {
  assert(!data_);
  assert(other->data_);
  prog.hardlink_size += size();
  ++prog.hardlinks;
  data_ = other->data_;
  ++data_->refcount;
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

void dir::scan(os_access&, progress&) {}

void dir::pack_entry(thrift::metadata::metadata& mv2,
                     global_entry_data const& data) const {
  auto& de = mv2.dir_entries_ref()->emplace_back();
  de.name_index = has_parent() ? data.get_name_index(name()) : 0;
  de.inode_num = DWARFS_NOTHROW(inode_num().value());
  entry::pack(DWARFS_NOTHROW(mv2.inodes.at(de.inode_num)), data);
}

void dir::pack(thrift::metadata::metadata& mv2,
               global_entry_data const& data) const {
  thrift::metadata::directory d;
  if (has_parent()) {
    auto pd = std::dynamic_pointer_cast<dir>(parent());
    DWARFS_CHECK(pd, "unexpected parent entry (not a directory)");
    auto pe = pd->entry_index();
    DWARFS_CHECK(pe, "parent entry index not set");
    d.parent_entry = *pe;
  } else {
    d.parent_entry = 0;
  }
  d.first_entry = mv2.dir_entries_ref()->size();
  mv2.directories.push_back(d);
  for (entry_ptr const& e : entries_) {
    e->set_entry_index(mv2.dir_entries_ref()->size());
    auto& de = mv2.dir_entries_ref()->emplace_back();
    de.name_index = data.get_name_index(e->name());
    de.inode_num = DWARFS_NOTHROW(e->inode_num().value());
    e->pack(DWARFS_NOTHROW(mv2.inodes.at(de.inode_num)), data);
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

void link::accept(entry_visitor& v, bool) { v.visit(this); }

void link::scan(os_access& os, progress& prog) {
  link_ = os.readlink(path(), size());
  prog.original_size += size();
}

entry::type_t device::type() const {
  auto mode = status().st_mode;
  return S_ISCHR(mode) || S_ISBLK(mode) ? E_DEVICE : E_OTHER;
}

void device::accept(entry_visitor& v, bool) { v.visit(this); }

void device::scan(os_access&, progress&) {}

uint64_t device::device_id() const { return status().st_rdev; }

class entry_factory_ : public entry_factory {
 public:
  std::shared_ptr<entry> create(os_access& os, const std::string& name,
                                std::shared_ptr<entry> parent) override {
    const std::string& p = parent ? parent->path() + "/" + name : name;
    struct ::stat st;

    os.lstat(p, &st);
    auto mode = st.st_mode;

    if (S_ISREG(mode)) {
      return std::make_shared<file>(name, std::move(parent), st);
    } else if (S_ISDIR(mode)) {
      return std::make_shared<dir>(name, std::move(parent), st);
    } else if (S_ISLNK(mode)) {
      return std::make_shared<link>(name, std::move(parent), st);
    } else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode)
#ifndef _WIN32
		|| S_ISSOCK(mode)
#endif
	      ) {
      return std::make_shared<device>(name, std::move(parent), st);
    } else {
      // TODO: warn
    }

    return nullptr;
  }
};

std::unique_ptr<entry_factory> entry_factory::create() {
  return std::make_unique<entry_factory_>();
}
} // namespace dwarfs

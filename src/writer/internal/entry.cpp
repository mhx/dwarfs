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

#include <dwarfs/checksum.h>
#include <dwarfs/error.h>
#include <dwarfs/file_type.h>
#include <dwarfs/mmif.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/inode.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/scanner_progress.h>

#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::writer::internal {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view const kHashContext{"[hashing] "};
constexpr char const kLocalPathSeparator{
    static_cast<char>(fs::path::preferred_separator)};

bool is_root_path(std::string_view path) {
#ifdef _WIN32
  return path == "/" || path == "\\";
#else
  return path == "/";
#endif
}

std::string entry_name(fs::path const& path, bool has_parent) {
  if (has_parent) {
    return u8string_to_string(path.filename().u8string());
  }
  return u8string_to_string(path.u8string());
}

} // namespace

entry::entry(fs::path const& path, std::shared_ptr<entry> parent,
             file_stat const& st)
    : name_{entry_name(path, static_cast<bool>(parent))}
    , parent_{std::move(parent)}
    , stat_{st} {}

bool entry::has_parent() const {
  if (parent_.lock()) {
    return true;
  }

  return false;
}

std::shared_ptr<entry> entry::parent() const { return parent_.lock(); }

void entry::set_name(const std::string& name) { name_ = name; }

std::u8string entry::u8name() const { return string_to_u8string(name_); }

fs::path entry::fs_path() const {
  if (auto parent = parent_.lock()) {
    return parent->fs_path() / u8name();
  }

  return fs::path(u8name());
}

std::string entry::path_as_string() const {
  return u8string_to_string(fs_path().u8string());
}

std::string entry::dpath() const {
  auto p = path_as_string();
  if (is_root_path(p)) {
    return std::string(1, kLocalPathSeparator);
  }
  if (type() == E_DIR) {
    p += kLocalPathSeparator;
  }
  return p;
}

std::string entry::unix_dpath() const {
  auto p = name_;

  if (is_root_path(p)) {
    return "/";
  }

  if (type() == E_DIR && !p.empty() && !p.ends_with(kLocalPathSeparator)) {
    p += '/';
  }

  if (auto parent = parent_.lock()) {
    return parent->unix_dpath() + p;
  } else if constexpr (kLocalPathSeparator != '/') {
    std::replace(p.begin(), p.end(), kLocalPathSeparator, '/');
  }

  return p;
}

bool entry::less_revpath(entry const& rhs) const {
  if (name() < rhs.name()) {
    return true;
  }

  if (name() > rhs.name()) {
    return false;
  }

  auto p = parent();
  auto rhs_p = rhs.parent();

  if (p && rhs_p) {
    return p->less_revpath(*rhs_p);
  }

  return static_cast<bool>(rhs_p);
}

bool entry::is_directory() const { return stat_.is_directory(); }

void entry::walk(std::function<void(entry*)> const& f) { f(this); }

void entry::walk(std::function<void(const entry*)> const& f) const { f(this); }

void entry::update(global_entry_data& data) const {
  stat_.ensure_valid(file_stat::uid_valid | file_stat::gid_valid |
                     file_stat::mode_valid | file_stat::atime_valid |
                     file_stat::mtime_valid | file_stat::ctime_valid);
  data.add_uid(stat_.uid_unchecked());
  data.add_gid(stat_.gid_unchecked());
  data.add_mode(stat_.mode_unchecked());
  data.add_atime(stat_.atime_unchecked());
  data.add_mtime(stat_.mtime_unchecked());
  data.add_ctime(stat_.ctime_unchecked());
}

void entry::pack(thrift::metadata::inode_data& entry_v2,
                 global_entry_data const& data) const {
  stat_.ensure_valid(file_stat::uid_valid | file_stat::gid_valid |
                     file_stat::mode_valid | file_stat::atime_valid |
                     file_stat::mtime_valid | file_stat::ctime_valid);
  entry_v2.mode_index() = data.get_mode_index(stat_.mode_unchecked());
  entry_v2.owner_index() = data.get_uid_index(stat_.uid_unchecked());
  entry_v2.group_index() = data.get_gid_index(stat_.gid_unchecked());
  entry_v2.atime_offset() = data.get_atime_offset(stat_.atime_unchecked());
  entry_v2.mtime_offset() = data.get_mtime_offset(stat_.mtime_unchecked());
  entry_v2.ctime_offset() = data.get_ctime_offset(stat_.ctime_unchecked());
}

size_t entry::size() const { return stat_.size(); }

uint64_t entry::raw_inode_num() const { return stat_.ino(); }

uint64_t entry::num_hard_links() const { return stat_.nlink(); }

void entry::override_size(size_t size) { stat_.set_size(size); }

entry::type_t file::type() const { return E_FILE; }

auto entry::get_permissions() const -> mode_type { return stat_.permissions(); }

void entry::set_permissions(mode_type perm) { stat_.set_permissions(perm); }

auto entry::get_uid() const -> uid_type { return stat_.uid(); }

void entry::set_uid(uid_type uid) { stat_.set_uid(uid); }

auto entry::get_gid() const -> gid_type { return stat_.gid(); }

void entry::set_gid(gid_type gid) { stat_.set_gid(gid); }

uint64_t entry::get_atime() const { return stat_.atime(); }

void entry::set_atime(uint64_t atime) { stat_.set_atime(atime); }

uint64_t entry::get_mtime() const { return stat_.mtime(); }

void entry::set_mtime(uint64_t mtime) { stat_.set_mtime(mtime); }

uint64_t entry::get_ctime() const { return stat_.ctime(); }

void entry::set_ctime(uint64_t ctime) { stat_.set_ctime(ctime); }

std::string_view file::hash() const {
  auto& h = data_->hash;
  return std::string_view(h.data(), h.size());
}

void file::set_inode(std::shared_ptr<inode> ino) {
  if (inode_) {
    DWARFS_THROW(runtime_error, "inode already set for file");
  }

  inode_ = std::move(ino);
}

std::shared_ptr<inode> file::get_inode() const { return inode_; }

void file::accept(entry_visitor& v, bool) { v.visit(this); }

void file::scan(os_access const& /*os*/, progress& /*prog*/) {
  DWARFS_THROW(runtime_error, "file::scan() without hash_alg is not used");
}

void file::scan(mmif* mm, progress& prog,
                std::optional<std::string> const& hash_alg) {
  size_t s = size();

  if (hash_alg) {
    progress::scan_updater supd(prog.hash, s);
    checksum cs(*hash_alg);

    if (s > 0) {
      std::shared_ptr<scanner_progress> pctx;
      auto const chunk_size = prog.hash.chunk_size.load();

      if (s >= 4 * chunk_size) {
        pctx = prog.create_context<scanner_progress>(
            termcolor::MAGENTA, kHashContext, path_as_string(), s);
      }

      size_t offset = 0;

      assert(mm);

      while (s >= chunk_size) {
        cs.update(mm->as<void>(offset), chunk_size);
        mm->release_until(offset);
        offset += chunk_size;
        s -= chunk_size;
        if (pctx) {
          pctx->bytes_processed += chunk_size;
        }
      }

      cs.update(mm->as<void>(offset), s);
    }

    data_->hash.resize(cs.digest_size());

    DWARFS_CHECK(cs.finalize(data_->hash.data()),
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

void dir::add(std::shared_ptr<entry> e) {
  if (lookup_) {
    auto r [[maybe_unused]] = lookup_->emplace(e->name(), e);
    assert(r.second);
  }
  entries_.emplace_back(std::move(e));
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

void dir::scan(os_access const&, progress&) {}

void dir::pack_entry(thrift::metadata::metadata& mv2,
                     global_entry_data const& data) const {
  auto& de = mv2.dir_entries()->emplace_back();
  de.name_index() = has_parent() ? data.get_name_index(name()) : 0;
  de.inode_num() = DWARFS_NOTHROW(inode_num().value());
  entry::pack(DWARFS_NOTHROW(mv2.inodes()->at(de.inode_num().value())), data);
}

void dir::pack(thrift::metadata::metadata& mv2,
               global_entry_data const& data) const {
  thrift::metadata::directory d;
  if (has_parent()) {
    auto pd = std::dynamic_pointer_cast<dir>(parent());
    DWARFS_CHECK(pd, "unexpected parent entry (not a directory)");
    auto pe = pd->entry_index();
    DWARFS_CHECK(pe, "parent entry index not set");
    d.parent_entry() = *pe;
  } else {
    d.parent_entry() = 0;
  }
  d.first_entry() = mv2.dir_entries()->size();
  auto se = entry_index();
  DWARFS_CHECK(se, "self entry index not set");
  d.self_entry() = *se;
  mv2.directories()->push_back(d);
  for (entry_ptr const& e : entries_) {
    e->set_entry_index(mv2.dir_entries()->size());
    auto& de = mv2.dir_entries()->emplace_back();
    de.name_index() = data.get_name_index(e->name());
    de.inode_num() = DWARFS_NOTHROW(e->inode_num().value());
    e->pack(DWARFS_NOTHROW(mv2.inodes()->at(de.inode_num().value())), data);
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

  lookup_.reset();
}

std::shared_ptr<entry> dir::find(fs::path const& path) {
  auto name = u8string_to_string(path.filename().u8string());

  if (!lookup_ && entries_.size() >= 16) {
    populate_lookup_table();
  }

  if (lookup_) {
    if (auto it = lookup_->find(name); it != lookup_->end()) {
      return it->second;
    }
  } else {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [name](auto& e) { return e->name() == name; });
    if (it != entries_.end()) {
      return *it;
    }
  }

  return nullptr;
}

void dir::populate_lookup_table() {
  assert(!lookup_);

  lookup_ = std::make_unique<lookup_table>();
  lookup_->reserve(entries_.size());

  for (auto const& e : entries_) {
    auto r [[maybe_unused]] = lookup_->emplace(e->name(), e);
    assert(r.second);
  }
}

entry::type_t link::type() const { return E_LINK; }

const std::string& link::linkname() const { return link_; }

void link::accept(entry_visitor& v, bool) { v.visit(this); }

void link::scan(os_access const& os, progress& prog) {
  link_ = u8string_to_string(os.read_symlink(fs_path()).u8string());
  prog.original_size += size();
  prog.symlink_size += size();
}

entry::type_t device::type() const {
  switch (status().type()) {
  case posix_file_type::character:
  case posix_file_type::block:
    return E_DEVICE;
  default:
    return E_OTHER;
  }
}

void device::accept(entry_visitor& v, bool) { v.visit(this); }

void device::scan(os_access const&, progress&) {}

uint64_t device::device_id() const { return status().rdev(); }

} // namespace dwarfs::writer::internal

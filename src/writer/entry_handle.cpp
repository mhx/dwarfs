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

#include <fmt/format.h>

#include <dwarfs/util.h>
#include <dwarfs/writer/entry_handle.h>
#include <dwarfs/writer/entry_storage.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/global_entry_data.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer {

namespace {

namespace fs = std::filesystem;

constexpr char kLocalPathSeparator{
    static_cast<char>(fs::path::preferred_separator)};

bool is_root_path(std::string_view path) {
#ifdef _WIN32
  return path == "/" || path == "\\";
#else
  return path == "/";
#endif
}

} // namespace

namespace detail {

template <detail::mutability Mut>
auto entry_handle_base<Mut>::base() const -> base_t* {
  return static_cast<base_t*>(storage_->get_entry(self_id_));
}

template <mutability Mut>
bool entry_handle_base<Mut>::has_parent() const {
  return base()->has_parent();
}

template <mutability Mut>
basic_entry_handle<Mut> entry_handle_base<Mut>::parent() const {
  return {*storage_, base()->parent_id()};
}

template <mutability Mut>
fs::path entry_handle_base<Mut>::fs_path() const {
  auto p = base()->name_as_path();

  if (this->has_parent()) {
    p = parent().fs_path() / p;
  }

  return p;
}

template <mutability Mut>
std::string entry_handle_base<Mut>::path_as_string() const {
  return path_to_utf8_string_sanitized(fs_path());
}

template <mutability Mut>
std::string entry_handle_base<Mut>::unix_dpath() const {
  std::string p{base()->name()};

  if (is_root_path(p)) {
    p = "/";
  } else {
    if (this->type() == entry_type::E_DIR && !p.empty() &&
        !p.ends_with(kLocalPathSeparator)) {
      p += '/';
    }

    if (this->has_parent()) {
      p = this->parent().unix_dpath() + p;
    } else if constexpr (kLocalPathSeparator != '/') {
      std::ranges::replace(p, kLocalPathSeparator, '/');
    }
  }

  return p;
}

template <mutability Mut>
std::string_view entry_handle_base<Mut>::name() const {
  return base()->name();
}

template <mutability Mut>
bool entry_handle_base<Mut>::less_revpath(const_entry_handle rhs) const {
  auto const lname = name();
  auto const rname = rhs.name();

  if (lname != rname) {
    return lname < rname;
  }

  auto const lp = parent();
  auto const rp = rhs.parent();

  if (lp && rp) {
    return lp.less_revpath(rp);
  }

  return rp.valid();
}

template <mutability Mut>
file_size_t entry_handle_base<Mut>::size() const {
  return base()->size();
}

template <mutability Mut>
file_size_t entry_handle_base<Mut>::allocated_size() const {
  return base()->allocated_size();
}

template <mutability Mut>
entry_type entry_handle_base<Mut>::type() const {
  return base()->type();
}

template <mutability Mut>
void entry_handle_base<Mut>::update(internal::global_entry_data& data) const {
  base()->update(data);
}

template <mutability Mut>
unique_inode_id entry_handle_base<Mut>::inode_id() const {
  return base()->inode_id();
}

template <mutability Mut>
uint64_t entry_handle_base<Mut>::num_hard_links() const {
  return base()->num_hard_links();
}

template <mutability Mut>
std::optional<uint32_t> const& entry_handle_base<Mut>::inode_num() const {
  return base()->inode_num(*storage_);
}

template <mutability Mut>
void entry_handle_base<Mut>::accept(entry_handle_visitor& v, bool preorder)
  requires is_mutable
{
  switch (base()->type()) {
  case entry_type::E_FILE:
    v.visit(file_handle{*storage_, self_id_});
    break;

  case entry_type::E_DIR: {
    auto dir = dir_handle{*storage_, self_id_};

    if (preorder) {
      v.visit(dir);
    }

    dir.for_each_child([&](entry_handle child) { child.accept(v, preorder); });

    if (!preorder) {
      v.visit(dir);
    }
  } break;

  case entry_type::E_LINK:
    v.visit(link_handle{*storage_, self_id_});
    break;

  case entry_type::E_DEVICE:
  case entry_type::E_OTHER:
    v.visit(device_handle{*storage_, self_id_});
    break;
  }
}

template <mutability Mut>
void entry_handle_base<Mut>::scan(os_access const& os, internal::progress& prog)
  requires is_mutable
{
  base()->scan(*this->storage_, this->self_id_, os, prog);
}

template <mutability Mut>
void entry_handle_base<Mut>::set_empty()
  requires is_mutable
{
  base()->set_empty();
}

template <mutability Mut>
void entry_handle_base<Mut>::set_entry_index(uint32_t index)
  requires is_mutable
{
  base()->set_entry_index(index);
}

template <mutability Mut>
std::optional<uint32_t> const& entry_handle_base<Mut>::entry_index() const {
  return base()->entry_index();
}

template <mutability Mut>
void entry_handle_base<Mut>::set_inode_num(uint32_t ino)
  requires is_mutable
{
  base()->set_inode_num(*storage_, ino);
}

template <mutability Mut>
void entry_handle_base<Mut>::walk(std::function<void(entry_handle)> const& f)
  requires is_mutable
{
  f(entry_handle{*storage_, self_id_});

  if (this->self_id_.is_dir()) {
    dir_handle{*storage_, self_id_}.for_each_child(
        [&](entry_handle child) { child.walk(f); });
  }
}

template <mutability Mut>
void entry_handle_base<Mut>::pack(
    thrift::metadata::inode_data& entry_v2,
    internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres) const {
  base()->pack(entry_v2, data, timeres);
}

template class entry_handle_base<mutability::const_>;
template class entry_handle_base<mutability::mutable_>;

} // namespace detail

// ---------- file_handle ----------

template <detail::mutability Mut>
auto basic_file_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
}

template <detail::mutability Mut>
std::string_view basic_file_handle<Mut>::hash() const {
  return self()->hash(this->storage());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::create_data()
  requires is_mutable
{
  self()->create_data(this->storage());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::scan(file_view const& mm, internal::progress& prog,
                                  std::optional<std::string> const& hash_alg)
  requires is_mutable
{
  self()->scan(this->storage(), this->id(), mm, prog, hash_alg);
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_invalid()
  requires is_mutable
{
  self()->set_invalid(this->storage());
}

template <detail::mutability Mut>
bool basic_file_handle<Mut>::is_invalid() const {
  return self()->is_invalid(this->storage());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_inode(internal::inode* ino)
  requires is_mutable
{
  self()->set_inode(std::move(ino));
}

template <detail::mutability Mut>
internal::inode* basic_file_handle<Mut>::get_inode() const {
  return self()->get_inode();
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::hardlink(file_handle other,
                                      internal::progress& prog)
  requires is_mutable
{
  self()->hardlink(this->storage(), other.self(), prog);
}

template <detail::mutability Mut>
uint32_t basic_file_handle<Mut>::hardlink_count() const {
  return self()->hardlink_count(this->storage());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_order_index(uint32_t index)
  requires is_mutable
{
  return self()->set_order_index(index);
}

template <detail::mutability Mut>
uint32_t basic_file_handle<Mut>::order_index() const {
  return self()->order_index();
}

template <detail::mutability Mut>
uint32_t basic_file_handle<Mut>::unique_file_id() const {
  return self()->unique_file_id();
}

template <detail::mutability Mut>
std::string basic_file_handle<Mut>::ptr_as_string() const {
  return fmt::format("{}", reinterpret_cast<void const*>(self()));
}

// ---------- dir_handle -----------

template <detail::mutability Mut>
auto basic_dir_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::add(entry_handle h)
  requires is_mutable
{
  self()->add(h);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::sort()
  requires is_mutable
{
  self()->sort(this->storage());
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::remove_empty_dirs(internal::progress& prog)
  requires is_mutable
{
  self()->remove_empty_dirs(this->storage(), prog);
}

template <detail::mutability Mut>
entry_handle basic_dir_handle<Mut>::find(fs::path const& path)
  requires is_mutable
{
  return {this->storage(), self()->find(this->storage(), path)};
}

template <detail::mutability Mut>
bool basic_dir_handle<Mut>::empty() const {
  return self()->empty();
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::pack(
    thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres) const {
  thrift::metadata::directory d;
  if (this->has_parent()) {
    auto pd = this->parent().as_dir();
    DWARFS_CHECK(pd, "unexpected parent entry (not a directory)");
    auto pe = pd.entry_index();
    DWARFS_CHECK(pe, "parent entry index not set");
    d.parent_entry() = *pe;
  } else {
    d.parent_entry() = 0;
  }
  d.first_entry() = mv2.dir_entries()->size();
  auto se = this->entry_index();
  DWARFS_CHECK(se, "self entry index not set");
  d.self_entry() = *se;
  mv2.directories()->push_back(d);
  self()->for_each_child([&](entry_id cid) {
    auto e = entry_handle(this->storage(), cid);
    e.set_entry_index(mv2.dir_entries()->size());
    auto& de = mv2.dir_entries()->emplace_back();
    de.name_index() = data.get_name_index(e.name());
    de.inode_num() = DWARFS_NOTHROW(e.inode_num().value());
    e.pack(DWARFS_NOTHROW(mv2.inodes()->at(de.inode_num().value())), data,
           timeres);
  });
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::pack_entry(
    thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres) const {
  self()->pack_entry(this->storage(), mv2, data, timeres);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::for_each_child(
    std::function<void(entry_handle)> const& f)
  requires is_mutable
{
  self()->for_each_child([&](entry_id id) { f({this->storage(), id}); });
}

// ---------- link_handle ----------

template <detail::mutability Mut>
auto basic_link_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
}

template <detail::mutability Mut>
std::string const& basic_link_handle<Mut>::linkname() const {
  return self()->linkname();
}

// --------- device_handle ----------

template <detail::mutability Mut>
auto basic_device_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
}

template <detail::mutability Mut>
bool basic_device_handle<Mut>::is_device() const noexcept {
  return this->id().is_device();
}

template <detail::mutability Mut>
std::uint64_t basic_device_handle<Mut>::device_id() const {
  return self()->device_id();
}

// ---------- entry_handle ----------

template <detail::mutability Mut>
auto basic_entry_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(basic_file_handle<Mut> h)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(basic_dir_handle<Mut> h)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(basic_link_handle<Mut> h)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(basic_device_handle<Mut> h)
    : detail::entry_handle_base<Mut>{h} {}

// TODO: GCC 12 chokes if the parentheses are removed
// NOLINTBEGIN(readability-redundant-parentheses)

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(file_handle h)
  requires(is_const)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(dir_handle h)
  requires(is_const)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(link_handle h)
  requires(is_const)
    : detail::entry_handle_base<Mut>{h} {}

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(device_handle h)
  requires(is_const)
    : detail::entry_handle_base<Mut>{h} {}

// NOLINTEND(readability-redundant-parentheses)

template <detail::mutability Mut>
basic_file_handle<Mut> basic_entry_handle<Mut>::as_file() const noexcept {
  if (is_file()) {
    return this->template base_as<basic_file_handle<Mut>>();
  }
  return {};
}

template <detail::mutability Mut>
basic_dir_handle<Mut> basic_entry_handle<Mut>::as_dir() const noexcept {
  if (is_dir()) {
    return this->template base_as<basic_dir_handle<Mut>>();
  }
  return {};
}

template <detail::mutability Mut>
basic_link_handle<Mut> basic_entry_handle<Mut>::as_link() const noexcept {
  if (is_link()) {
    return this->template base_as<basic_link_handle<Mut>>();
  }
  return {};
}

template <detail::mutability Mut>
basic_device_handle<Mut> basic_entry_handle<Mut>::as_device() const noexcept {
  if (is_device() || is_other()) {
    return this->template base_as<basic_device_handle<Mut>>();
  }
  return {};
}

// explicit instantiations

template class basic_entry_handle<detail::mutability::const_>;
template class basic_entry_handle<detail::mutability::mutable_>;

template class basic_file_handle<detail::mutability::const_>;
template class basic_file_handle<detail::mutability::mutable_>;

template class basic_dir_handle<detail::mutability::const_>;
template class basic_dir_handle<detail::mutability::mutable_>;

template class basic_link_handle<detail::mutability::const_>;
template class basic_link_handle<detail::mutability::mutable_>;

template class basic_device_handle<detail::mutability::const_>;
template class basic_device_handle<detail::mutability::mutable_>;

} // namespace dwarfs::writer

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

#include <dwarfs/writer/entry_handle.h>

#include <dwarfs/writer/internal/entry.h>

namespace dwarfs::writer {

namespace detail {

template <mutability Mut>
bool entry_handle_base<Mut>::has_parent() const {
  return self_->has_parent();
}

template <mutability Mut>
basic_entry_handle<Mut> entry_handle_base<Mut>::parent() const {
  return {*storage_, self_->parent()};
}

template <mutability Mut>
std::filesystem::path entry_handle_base<Mut>::fs_path() const {
  return self_->fs_path();
}

template <mutability Mut>
std::string entry_handle_base<Mut>::path_as_string() const {
  return self_->path_as_string();
}

template <mutability Mut>
std::string entry_handle_base<Mut>::unix_dpath() const {
  return self_->unix_dpath();
}

template <mutability Mut>
std::string_view entry_handle_base<Mut>::name() const {
  return self_->name();
}

template <mutability Mut>
bool entry_handle_base<Mut>::less_revpath(const_entry_handle rhs) const {
  return self_->less_revpath(*rhs.self_);
}

template <mutability Mut>
file_size_t entry_handle_base<Mut>::size() const {
  return self_->size();
}

template <mutability Mut>
file_size_t entry_handle_base<Mut>::allocated_size() const {
  return self_->allocated_size();
}

template <mutability Mut>
entry_type entry_handle_base<Mut>::type() const {
  return self_->type();
}

template <mutability Mut>
bool entry_handle_base<Mut>::is_directory() const {
  return self_->is_directory();
}

template <mutability Mut>
void entry_handle_base<Mut>::update(internal::global_entry_data& data) const {
  self_->update(data);
}

template <mutability Mut>
unique_inode_id entry_handle_base<Mut>::inode_id() const {
  return self_->inode_id();
}

template <mutability Mut>
uint64_t entry_handle_base<Mut>::num_hard_links() const {
  return self_->num_hard_links();
}

template <mutability Mut>
std::optional<uint32_t> const& entry_handle_base<Mut>::inode_num() const {
  return self_->inode_num();
}

template <mutability Mut>
void entry_handle_base<Mut>::accept(entry_handle_visitor& v, bool preorder)
  requires is_mutable
{
  switch (self_->type()) {
  case entry_type::E_FILE:
    v.visit(file_handle{*storage_, self_->as_file()});
    break;

  case entry_type::E_DIR: {
    auto dir = dir_handle{*storage_, self_->as_dir()};

    if (preorder) {
      v.visit(dir);
    }

    dir.for_each_child([&](entry_handle child) { child.accept(v, preorder); });

    if (!preorder) {
      v.visit(dir);
    }
  } break;

  case entry_type::E_LINK:
    v.visit(link_handle{*storage_, self_->as_link()});
    break;

  case entry_type::E_DEVICE:
  case entry_type::E_OTHER:
    v.visit(device_handle{*storage_, self_->as_device()});
    break;
  }
}

template <mutability Mut>
void entry_handle_base<Mut>::scan(os_access const& os, internal::progress& prog)
  requires is_mutable
{
  self_->scan(os, prog);
}

template <mutability Mut>
void entry_handle_base<Mut>::set_empty()
  requires is_mutable
{
  self_->set_empty();
}

template <mutability Mut>
void entry_handle_base<Mut>::set_entry_index(uint32_t index)
  requires is_mutable
{
  self_->set_entry_index(index);
}

template <mutability Mut>
void entry_handle_base<Mut>::set_inode_num(uint32_t ino)
  requires is_mutable
{
  self_->set_inode_num(ino);
}

template <mutability Mut>
void entry_handle_base<Mut>::walk(std::function<void(entry_handle)> const& f)
  requires is_mutable
{
  self_->walk([&](internal::entry* e) { f({*storage_, e}); });
}

template class entry_handle_base<mutability::const_>;
template class entry_handle_base<mutability::mutable_>;

} // namespace detail

// ---------- file_handle ----------

template <detail::mutability Mut>
auto basic_file_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->self_);
}

template <detail::mutability Mut>
std::string_view basic_file_handle<Mut>::hash() const {
  return self()->hash();
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::create_data()
  requires is_mutable
{
  self()->create_data();
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::scan(file_view const& mm, internal::progress& prog,
                                  std::optional<std::string> const& hash_alg)
  requires is_mutable
{
  self()->scan(mm, prog, hash_alg);
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_invalid()
  requires is_mutable
{
  self()->set_invalid();
}

template <detail::mutability Mut>
bool basic_file_handle<Mut>::is_invalid() const {
  return self()->is_invalid();
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_inode(std::shared_ptr<internal::inode> ino)
  requires is_mutable
{
  self()->set_inode(std::move(ino));
}

template <detail::mutability Mut>
std::shared_ptr<internal::inode> basic_file_handle<Mut>::get_inode() const {
  return self()->get_inode();
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::hardlink(file_handle other,
                                      internal::progress& prog)
  requires is_mutable
{
  self()->hardlink(other.self(), prog);
}

template <detail::mutability Mut>
uint32_t basic_file_handle<Mut>::hardlink_count() const {
  return self()->hardlink_count();
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
  return static_cast<self_t*>(this->self_);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::add(entry_handle h)
  requires is_mutable
{
  self()->add(h.self_);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::sort()
  requires is_mutable
{
  self()->sort();
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::remove_empty_dirs(internal::progress& prog)
  requires is_mutable
{
  self()->remove_empty_dirs(prog);
}

template <detail::mutability Mut>
entry_handle basic_dir_handle<Mut>::find(std::filesystem::path const& path)
  requires is_mutable
{
  return {*this->storage_, self()->find(path)};
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::pack(
    thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres) const {
  self()->pack(mv2, data, timeres);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::pack_entry(
    thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres) const {
  self()->pack_entry(mv2, data, timeres);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::for_each_child(
    std::function<void(entry_handle)> const& f)
  requires is_mutable
{
  self()->for_each_child([&](internal::entry* e) { f({*this->storage_, e}); });
}

// ---------- link_handle ----------

template <detail::mutability Mut>
auto basic_link_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->self_);
}

template <detail::mutability Mut>
std::string const& basic_link_handle<Mut>::linkname() const {
  return self()->linkname();
}

// --------- device_handle ----------

template <detail::mutability Mut>
auto basic_device_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->self_);
}

template <detail::mutability Mut>
bool basic_device_handle<Mut>::is_device() const noexcept {
  return self()->is_device();
}

template <detail::mutability Mut>
std::uint64_t basic_device_handle<Mut>::device_id() const {
  return self()->device_id();
}

// ---------- entry_handle ----------

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
bool basic_entry_handle<Mut>::is_file() const noexcept {
  return this->self_->is_file();
}

template <detail::mutability Mut>
bool basic_entry_handle<Mut>::is_dir() const noexcept {
  return this->self_->is_dir();
}

template <detail::mutability Mut>
bool basic_entry_handle<Mut>::is_link() const noexcept {
  return this->self_->is_link();
}

template <detail::mutability Mut>
bool basic_entry_handle<Mut>::is_device() const noexcept {
  return this->self_->is_device();
}

template <detail::mutability Mut>
bool basic_entry_handle<Mut>::is_other() const noexcept {
  return this->self_->is_other();
}

template <detail::mutability Mut>
basic_file_handle<Mut> basic_entry_handle<Mut>::as_file() const noexcept {
  return {*this->storage_, this->self_->as_file()};
}

template <detail::mutability Mut>
basic_dir_handle<Mut> basic_entry_handle<Mut>::as_dir() const noexcept {
  return {*this->storage_, this->self_->as_dir()};
}

template <detail::mutability Mut>
basic_link_handle<Mut> basic_entry_handle<Mut>::as_link() const noexcept {
  return {*this->storage_, this->self_->as_link()};
}

template <detail::mutability Mut>
basic_device_handle<Mut> basic_entry_handle<Mut>::as_device() const noexcept {
  return {*this->storage_, this->self_->as_device()};
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

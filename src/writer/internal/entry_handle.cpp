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

#include <dwarfs/checksum.h>
#include <dwarfs/util.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/entry_handle.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/scanner_progress.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view const kHashContext{"[hashing] "};

} // namespace

namespace detail {

template <detail::mutability Mut>
auto entry_handle_base<Mut>::base() const -> base_t* {
  return static_cast<base_t*>(storage_->get_entry(self_id_));
}

template <mutability Mut>
bool entry_handle_base<Mut>::has_parent() const {
  return storage_->get_parent(self_id_).valid();
}

template <mutability Mut>
basic_entry_handle<Mut> entry_handle_base<Mut>::parent() const {
  return {*storage_, storage_->get_parent(self_id_)};
}

template <mutability Mut>
fs::path entry_handle_base<Mut>::fs_path() const {
  return storage_->get_path(self_id_);
}

template <mutability Mut>
std::string entry_handle_base<Mut>::path_as_string() const {
  return path_to_utf8_string_sanitized(fs_path());
}

template <mutability Mut>
std::string entry_handle_base<Mut>::unix_dpath() const {
  return storage_->get_unix_dpath(self_id_);
}

template <mutability Mut>
std::string_view entry_handle_base<Mut>::name() const {
  return storage_->get_name(self_id_);
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
  return this->storage().get_entry_size(self_id_);
}

template <mutability Mut>
file_size_t entry_handle_base<Mut>::allocated_size() const {
  return this->storage().get_entry_allocated_size(self_id_);
}

template <mutability Mut>
entry_type entry_handle_base<Mut>::type() const {
  return self_id_.type();
}

template <mutability Mut>
void entry_handle_base<Mut>::update(internal::global_entry_data& data) const {
  storage_->update_global_entry_data(self_id_, data);
}

template <mutability Mut>
unique_inode_id entry_handle_base<Mut>::get_unique_inode_id() const {
  return storage_->get_unique_inode_id(self_id_);
}

template <mutability Mut>
file_stat::nlink_type entry_handle_base<Mut>::num_hard_links() const {
  return storage_->get_nlink(self_id_);
}

template <mutability Mut>
std::optional<std::uint64_t> entry_handle_base<Mut>::inode_num() const {
  return storage_->get_inode_num_for_entry(self_id_);
}

template <mutability Mut>
void entry_handle_base<Mut>::accept(entry_handle_visitor& v, bool preorder)
  requires is_mutable
{
  switch (type()) {
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
    v.visit(device_handle{*storage_, self_id_});
    break;

  case entry_type::E_OTHER:
    v.visit(other_handle{*storage_, self_id_});
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
  this->storage().set_entry_empty(self_id_);
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
void entry_handle_base<Mut>::set_inode_num(std::uint64_t ino)
  requires is_mutable
{
  storage_->set_inode_num_for_entry(self_id_, ino);
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
  storage_->pack_entry(self_id_, entry_v2, data, timeres);
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
  return this->storage().get_file_hash(this->id());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::create_data()
  requires is_mutable
{
  this->storage().create_packed_file_data(this->id());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::scan(file_view const& mm, internal::progress& prog,
                                  std::optional<std::string> const& hash_alg)
  requires is_mutable
{
  auto const s = this->size();

  if (hash_alg) {
    progress::scan_updater supd(prog.hash, s);
    checksum cs(*hash_alg);

    if (s > 0) {
      std::shared_ptr<scanner_progress> pctx;
      auto const chunk_size = prog.hash.chunk_size.load();

      if (std::cmp_greater_equal(s, 4 * chunk_size)) {
        pctx = prog.create_context<scanner_progress>(
            termcolor::MAGENTA, kHashContext, this->path_as_string(), s);
      }

      assert(mm);

      for (auto const& ext : mm.extents()) {
        // TODO; See if we need to handle hole extents differently.
        //       I guess not, since we can just make holes generate
        //       zeroes efficiently in the file_view abstraction.
        for (auto const& seg : ext.segments(chunk_size)) {
          auto data = seg.span();
          cs.update(data);
          if (pctx) {
            pctx->advance(data.size());
          }
        }
      }
    }

    auto buffer =
        this->storage().get_file_hash_buffer(this->id(), cs.digest_size());

    DWARFS_CHECK(cs.finalize(buffer.data()), "checksum computation failed");
  }
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_invalid()
  requires is_mutable
{
  this->storage().set_file_invalid(this->id());
}

template <detail::mutability Mut>
bool basic_file_handle<Mut>::is_invalid() const {
  return this->storage().is_file_invalid(this->id());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::set_inode(inode_id ino)
  requires is_mutable
{
  this->storage().set_file_inode(this->id(), ino);
}

template <detail::mutability Mut>
inode_id basic_file_handle<Mut>::get_inode() const {
  return this->storage().get_file_inode(this->id());
}

template <detail::mutability Mut>
void basic_file_handle<Mut>::hardlink(file_handle other,
                                      internal::progress& prog)
  requires is_mutable
{
  this->storage().create_hardlink(this->id(), other.id(), prog);
}

template <detail::mutability Mut>
uint32_t basic_file_handle<Mut>::hardlink_count() const {
  return this->storage().hardlink_count(this->id());
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
  return inode_handle{this->storage(), this->get_inode()}.num();
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
entry_handle basic_dir_handle<Mut>::find(fs::path const& path)
  requires is_mutable
{
  return {this->storage(),
          this->storage().find_in_dir(
              this->id(), path_to_utf8_string_sanitized(path.filename()))};
}

template <detail::mutability Mut>
bool basic_dir_handle<Mut>::empty() const {
  return this->storage().is_dir_empty(this->id());
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::pack(
    thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
    internal::time_resolution_converter const& timeres)
  requires is_mutable
{
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
  this->for_each_child([&](entry_handle e) {
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
  auto& de = mv2.dir_entries()->emplace_back();
  de.name_index() = this->has_parent() ? data.get_name_index(this->name()) : 0;
  auto const inode_num = DWARFS_NOTHROW(this->inode_num().value());
  de.inode_num() = inode_num;
  detail::entry_handle_base<Mut>::pack(
      DWARFS_NOTHROW(mv2.inodes()->at(inode_num)), data, timeres);
}

template <detail::mutability Mut>
void basic_dir_handle<Mut>::for_each_child(
    std::function<void(entry_handle)> const& f)
  requires is_mutable
{
  this->storage().for_each_entry_in_dir(
      this->id(), [&](entry_id id) { f({this->storage(), id}); });
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
std::uint64_t basic_device_handle<Mut>::posix_device_id() const {
  return this->storage().get_represented_device(this->id());
}

// --------- other_handle ----------

template <detail::mutability Mut>
auto basic_other_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(this->base());
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

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(basic_other_handle<Mut> h)
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

template <detail::mutability Mut>
basic_entry_handle<Mut>::basic_entry_handle(other_handle h)
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
  if (is_device()) {
    return this->template base_as<basic_device_handle<Mut>>();
  }
  return {};
}

template <detail::mutability Mut>
basic_other_handle<Mut> basic_entry_handle<Mut>::as_other() const noexcept {
  if (is_other()) {
    return this->template base_as<basic_other_handle<Mut>>();
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

template class basic_other_handle<detail::mutability::const_>;
template class basic_other_handle<detail::mutability::mutable_>;

} // namespace dwarfs::writer::internal

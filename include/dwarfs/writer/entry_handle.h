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

#pragma once

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <boost/container_hash/hash.hpp>

#include <dwarfs/file_stat.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/entry_id.h>
#include <dwarfs/writer/entry_type.h>
#include <dwarfs/writer/unique_inode_id.h>

namespace dwarfs {

class file_view;
class os_access;

namespace thrift::metadata {

class inode_data;
class metadata;

} // namespace thrift::metadata

namespace writer {

namespace internal {

class entry;
class file;
class dir;
class link;
class device;
class other;

class inode;
class global_entry_data;
class progress;
class provisional_entry;
class time_resolution_converter;

} // namespace internal

class entry_storage;

namespace detail {

enum class mutability { mutable_, const_ };

} // namespace detail

template <detail::mutability Mut>
class basic_entry_handle;

template <detail::mutability Mut>
class basic_file_handle;

template <detail::mutability Mut>
class basic_dir_handle;

template <detail::mutability Mut>
class basic_link_handle;

template <detail::mutability Mut>
class basic_device_handle;

template <detail::mutability Mut>
class basic_other_handle;

using const_entry_handle = basic_entry_handle<detail::mutability::const_>;
using entry_handle = basic_entry_handle<detail::mutability::mutable_>;

using const_file_handle = basic_file_handle<detail::mutability::const_>;
using file_handle = basic_file_handle<detail::mutability::mutable_>;

using const_dir_handle = basic_dir_handle<detail::mutability::const_>;
using dir_handle = basic_dir_handle<detail::mutability::mutable_>;

using const_link_handle = basic_link_handle<detail::mutability::const_>;
using link_handle = basic_link_handle<detail::mutability::mutable_>;

using const_device_handle = basic_device_handle<detail::mutability::const_>;
using device_handle = basic_device_handle<detail::mutability::mutable_>;

using const_other_handle = basic_other_handle<detail::mutability::const_>;
using other_handle = basic_other_handle<detail::mutability::mutable_>;

class entry_handle_visitor {
 public:
  virtual ~entry_handle_visitor() = default;
  virtual void visit(file_handle h) = 0;
  virtual void visit(dir_handle h) = 0;
  virtual void visit(link_handle h) = 0;
  virtual void visit(device_handle h) = 0;
  virtual void visit(other_handle h) = 0;
};

namespace detail {

template <typename T, mutability Mut>
using mutability_t =
    std::conditional_t<Mut == mutability::const_, std::add_const_t<T>, T>;

template <mutability Mut>
class entry_handle_base {
 public:
  static constexpr bool is_mutable = Mut == mutability::mutable_;

  friend class entry_handle_base<
      Mut == mutability::const_ ? mutability::mutable_ : mutability::const_>;

  entry_handle_base() = default;
  entry_handle_base(entry_storage& storage, entry_id id)
      : storage_{&storage}
      , self_id_{id} {}

  bool valid() const { return self_id_.valid(); }
  explicit operator bool() const { return valid(); }

  entry_id id() const { return self_id_; }

  bool has_parent() const;
  basic_entry_handle<Mut> parent() const;
  std::filesystem::path fs_path() const;
  std::string path_as_string() const;
  std::string unix_dpath() const;
  std::string_view name() const;
  bool less_revpath(basic_entry_handle<mutability::const_> rhs) const;
  file_size_t size() const;
  file_size_t allocated_size() const;
  entry_type type() const;
  void update(internal::global_entry_data& data) const;
  unique_inode_id inode_id() const;
  uint64_t num_hard_links() const;
  std::optional<uint32_t> const& inode_num() const;

  void accept(entry_handle_visitor& v, bool preorder = false)
    requires is_mutable;
  void scan(os_access const& os, internal::progress& prog)
    requires is_mutable;
  void set_empty()
    requires is_mutable;
  void set_entry_index(uint32_t index)
    requires is_mutable;
  void set_inode_num(uint32_t ino)
    requires is_mutable;
  void
  walk(std::function<void(basic_entry_handle<mutability::mutable_>)> const& f)
    requires is_mutable;

  std::optional<uint32_t> const& entry_index() const;

  void pack(thrift::metadata::inode_data& entry_v2,
            internal::global_entry_data const& data,
            internal::time_resolution_converter const& timeres) const;

  friend bool
  operator==(entry_handle_base const&, entry_handle_base const&) = default;
  friend bool
  operator!=(entry_handle_base const&, entry_handle_base const&) = default;

  std::size_t object_hash() const {
    std::size_t seed = 0;
    boost::hash_combine(seed, storage_);
    boost::hash_combine(seed, self_id_.hash());
    return seed;
  }

 protected:
  using base_t = detail::mutability_t<internal::entry, Mut>;

  base_t* base() const;

  template <typename DerivedHandle>
  DerivedHandle base_as() const {
    return DerivedHandle{*storage_, self_id_};
  }

  entry_storage& storage() const { return *storage_; }

 private:
  entry_storage* storage_{nullptr};
  entry_id self_id_;
};

} // namespace detail

template <detail::mutability Mut>
class basic_entry_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_const = Mut == detail::mutability::const_;
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  friend class basic_dir_handle<Mut>; // for dir.add(entry)
  friend class entry_storage;

  basic_entry_handle() = default;
  basic_entry_handle(entry_storage& storage, entry_id id)
      : detail::entry_handle_base<Mut>{storage, id} {}

  explicit(false) operator const_entry_handle() const
    requires is_mutable
  {
    return this->template base_as<const_entry_handle>();
  }

  explicit(false) basic_entry_handle(basic_file_handle<Mut> h);
  explicit(false) basic_entry_handle(basic_dir_handle<Mut> h);
  explicit(false) basic_entry_handle(basic_link_handle<Mut> h);
  explicit(false) basic_entry_handle(basic_device_handle<Mut> h);
  explicit(false) basic_entry_handle(basic_other_handle<Mut> h);

  // TODO: GCC 12 chokes if the parentheses are removed
  // NOLINTBEGIN(readability-redundant-parentheses)
  explicit(false) basic_entry_handle(file_handle h)
    requires(is_const);
  explicit(false) basic_entry_handle(dir_handle h)
    requires(is_const);
  explicit(false) basic_entry_handle(link_handle h)
    requires(is_const);
  explicit(false) basic_entry_handle(device_handle h)
    requires(is_const);
  explicit(false) basic_entry_handle(other_handle h)
    requires(is_const);
  // NOLINTEND(readability-redundant-parentheses)

  bool is_file() const noexcept { return this->id().is_file(); }
  bool is_dir() const noexcept { return this->id().is_dir(); }
  bool is_link() const noexcept { return this->id().is_link(); }
  bool is_device() const noexcept { return this->id().is_device(); }
  bool is_other() const noexcept { return this->id().is_other(); }

  basic_file_handle<Mut> as_file() const noexcept;
  basic_dir_handle<Mut> as_dir() const noexcept;
  basic_link_handle<Mut> as_link() const noexcept;
  basic_device_handle<Mut> as_device() const noexcept;
  basic_other_handle<Mut> as_other() const noexcept;

 private:
  using self_t = detail::mutability_t<internal::entry, Mut>;

  self_t* self() const;
};

template <detail::mutability Mut>
class basic_file_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  using detail::entry_handle_base<Mut>::entry_handle_base;

  explicit(false) operator const_file_handle() const
    requires is_mutable
  {
    return this->template base_as<const_file_handle>();
  }

  std::string_view hash() const;

  void create_data()
    requires is_mutable;

  void scan(file_view const& mm, internal::progress& prog,
            std::optional<std::string> const& hash_alg)
    requires is_mutable;

  void set_invalid()
    requires is_mutable;
  bool is_invalid() const;

  void set_inode(internal::inode* ino)
    requires is_mutable;
  internal::inode* get_inode() const;

  void hardlink(basic_file_handle<detail::mutability::mutable_> other,
                internal::progress& prog)
    requires is_mutable;
  uint32_t hardlink_count() const;

  void set_order_index(uint32_t index)
    requires is_mutable;
  uint32_t order_index() const;

  uint32_t unique_file_id() const;

  std::string ptr_as_string() const; // TODO

 private:
  using self_t = detail::mutability_t<internal::file, Mut>;

  self_t* self() const;
};

template <detail::mutability Mut>
class basic_dir_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  using detail::entry_handle_base<Mut>::entry_handle_base;

  explicit(false) operator const_dir_handle() const
    requires is_mutable
  {
    return this->template base_as<const_dir_handle>();
  }

  void add(entry_handle h)
    requires is_mutable;

  void sort()
    requires is_mutable;
  void remove_empty_dirs(internal::progress& prog)
    requires is_mutable;

  entry_handle find(std::filesystem::path const& path)
    requires is_mutable;

  [[nodiscard]] bool empty() const;

  void
  pack(thrift::metadata::metadata& mv2, internal::global_entry_data const& data,
       internal::time_resolution_converter const& timeres) const;
  void pack_entry(thrift::metadata::metadata& mv2,
                  internal::global_entry_data const& data,
                  internal::time_resolution_converter const& timeres) const;

  // const version can be implemented if needed
  void for_each_child(std::function<void(entry_handle)> const& f)
    requires is_mutable;

 private:
  using self_t = detail::mutability_t<internal::dir, Mut>;

  self_t* self() const;
};

template <detail::mutability Mut>
class basic_link_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  using detail::entry_handle_base<Mut>::entry_handle_base;

  explicit(false) operator const_link_handle() const
    requires is_mutable
  {
    return this->template base_as<const_link_handle>();
  }

  // TODO: string_view
  std::string const& linkname() const;

 private:
  using self_t = detail::mutability_t<internal::link, Mut>;

  self_t* self() const;
};

template <detail::mutability Mut>
class basic_device_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  using detail::entry_handle_base<Mut>::entry_handle_base;

  explicit(false) operator const_device_handle() const
    requires is_mutable
  {
    return this->template base_as<const_device_handle>();
  }

  std::uint64_t device_id() const;

 private:
  using self_t = detail::mutability_t<internal::device, Mut>;

  self_t* self() const;
};

template <detail::mutability Mut>
class basic_other_handle final : public detail::entry_handle_base<Mut> {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  using detail::entry_handle_base<Mut>::entry_handle_base;

  explicit(false) operator const_other_handle() const
    requires is_mutable
  {
    return this->template base_as<const_other_handle>();
  }

 private:
  using self_t = detail::mutability_t<internal::other, Mut>;

  self_t* self() const;
};

} // namespace writer
} // namespace dwarfs

// NOLINTBEGIN(cert-dcl58-cpp)
namespace std {

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_entry_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_entry_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_file_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_file_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_dir_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_dir_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_link_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_link_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_device_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_device_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_other_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_other_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

} // namespace std
// NOLINTEND(cert-dcl58-cpp)

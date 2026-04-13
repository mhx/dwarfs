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

#include <cstdint>

#include <boost/container_hash/hash.hpp>

#include <dwarfs/types.h>

#include <dwarfs/writer/detail/mutability.h>

namespace dwarfs::writer {

namespace internal {

class inode;

} // namespace internal

class entry_storage;

template <detail::mutability Mut>
class basic_inode_handle;

using const_inode_handle = basic_inode_handle<detail::mutability::const_>;
using inode_handle = basic_inode_handle<detail::mutability::mutable_>;

template <detail::mutability Mut>
class basic_inode_handle final {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  basic_inode_handle() = default;

  basic_inode_handle(entry_storage& storage, std::uint64_t id)
      : storage_{&storage}
      , self_id_{id} {}

  explicit(false) operator const_inode_handle() const
    requires is_mutable
  {
    return const_inode_handle{*storage_, self_id_};
  }

  std::size_t object_hash() const {
    std::size_t seed = 0;
    boost::hash_combine(seed, storage_);
    boost::hash_combine(seed, self_id_);
    return seed;
  }

#if 0
  void set_files(file_handle_vector const& fv) requires is_mutable;
  void populate(file_size_t size) requires is_mutable;
  void
  scan(file_view const& mm, inode_options const& options, progress& prog) requires is_mutable;
  void set_num(uint32_t num) requires is_mutable;
  uint32_t num() const;
  bool has_category(fragment_category cat) const;
  std::optional<uint32_t>
  similarity_hash(fragment_category cat) const;
  nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const;
  file_size_t size() const;
  const_file_handle any() const;
  file_handle_vector all() const;
  bool
  append_chunks_to(std::vector<thrift::metadata::chunk>& vec,
                   std::optional<inode_hole_mapper>& hole_mapper) const;
  inode_fragments& fragments() requires is_mutable;
  inode_fragments const& fragments();
  void dump(std::ostream& os, inode_options const& options) const;
  void set_scan_error(const_file_handle fp, std::exception_ptr ep);
  std::optional<std::pair<const_file_handle, std::exception_ptr>>
  get_scan_error() const;
  inode_mmap_any_result
  mmap_any(os_access const& os, open_file_options const& of_opts) const;
#endif

 private:
  using self_t = detail::mutability_t<internal::inode, Mut>;
  self_t* self() const;

  entry_storage* storage_{nullptr};
  std::uint64_t self_id_;
};

} // namespace dwarfs::writer

// NOLINTBEGIN(cert-dcl58-cpp)
namespace std {

template <dwarfs::writer::detail::mutability Mut>
struct hash<dwarfs::writer::basic_inode_handle<Mut>> {
  size_t operator()(dwarfs::writer::basic_inode_handle<Mut> const& h) const {
    return h.object_hash();
  }
};

} // namespace std
// NOLINTEND(cert-dcl58-cpp)

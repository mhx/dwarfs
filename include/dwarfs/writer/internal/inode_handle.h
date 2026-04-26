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
#include <ranges>

#include <dwarfs/types.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/detail/mutability.h>
#include <dwarfs/writer/internal/entry_id_vector.h>
#include <dwarfs/writer/internal/inode_fragments_view.h>
#include <dwarfs/writer/internal/inode_hole_mapper.h>
#include <dwarfs/writer/internal/inode_id.h>
#include <dwarfs/writer/internal/inode_types.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs {

struct open_file_options;

} // namespace dwarfs

namespace dwarfs::thrift::metadata {

class chunk;

} // namespace dwarfs::thrift::metadata

namespace dwarfs::writer::internal {

class entry_storage;
class inode_hole_mapper;

template <detail::mutability Mut>
class basic_inode_handle;

using const_inode_handle = basic_inode_handle<detail::mutability::const_>;
using inode_handle = basic_inode_handle<detail::mutability::mutable_>;

template <detail::mutability Mut>
class basic_inode_handle final {
 public:
  static constexpr bool is_mutable = Mut == detail::mutability::mutable_;

  basic_inode_handle() = default;

  basic_inode_handle(entry_storage& storage, inode_id id)
      : storage_{&storage}
      , self_id_{id} {}

  explicit(false) operator const_inode_handle() const
    requires is_mutable
  {
    return const_inode_handle{*storage_, self_id_};
  }

  bool valid() const { return self_id_.valid(); }
  explicit operator bool() const { return valid(); }

  inode_id id() const { return self_id_; }

  void set_files(file_id_vector const& fv)
    requires is_mutable;
  void populate(file_size_t size)
    requires is_mutable;
  void scan(file_view const& mm, inode_options const& options, progress& prog)
    requires is_mutable;
  void set_num(uint32_t num)
    requires is_mutable;
  uint32_t num() const;
  bool has_category(fragment_category cat) const;
  std::optional<uint32_t> similarity_hash(fragment_category cat) const;
  nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const;
  file_size_t size() const;
  const_file_handle first_file() const;
  const_file_handle any() const;
  file_id_vector const& all_file_ids() const;
  auto all() const {
    return std::views::all(all_file_ids()) |
           std::views::transform(
               [this](file_id id) { return const_file_handle{*storage_, id}; });
  }
  bool append_chunks_to(thrift::metadata::metadata::chunks_member_type& vec,
                        std::optional<inode_hole_mapper>& hole_mapper) const;
  inode_fragments_view fragments_view() const;
  void dump(std::ostream& os, inode_options const& options) const;
  void set_scan_error(const_file_handle fp, std::exception_ptr ep)
    requires is_mutable;
  std::optional<std::pair<const_file_handle, std::exception_ptr>>
  get_scan_error() const;
  inode_mmap_any_result
  mmap_any(os_access const& os, open_file_options const& of_opts) const;

 private:
  entry_storage* storage_{nullptr};
  inode_id self_id_;
};

} // namespace dwarfs::writer::internal

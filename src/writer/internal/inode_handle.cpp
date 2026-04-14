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

#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/inode_handle.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal {

// template <detail::mutability Mut>
// auto basic_inode_handle<Mut>::self() const -> self_t* {
//   return static_cast<self_t*>(this->base());
// }

template <detail::mutability Mut>
auto basic_inode_handle<Mut>::self() const -> self_t* {
  return static_cast<self_t*>(storage_->get_inode(self_id_));
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_files(file_id_vector const& fv)
  requires is_mutable
{
  self()->set_files(fv);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::populate(file_size_t size)
  requires is_mutable
{
  self()->populate(size);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::scan(file_view const& mm,
                                   inode_options const& options, progress& prog)
  requires is_mutable
{
  self()->scan(mm, options, prog);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_num(uint32_t num)
  requires is_mutable
{
  self()->set_num(num);
}

template <detail::mutability Mut>
uint32_t basic_inode_handle<Mut>::num() const {
  return self()->num();
}

template <detail::mutability Mut>
bool basic_inode_handle<Mut>::has_category(fragment_category cat) const {
  return self()->has_category(cat);
}

template <detail::mutability Mut>
std::optional<uint32_t>
basic_inode_handle<Mut>::similarity_hash(fragment_category cat) const {
  return self()->similarity_hash(cat);
}

template <detail::mutability Mut>
nilsimsa::hash_type const*
basic_inode_handle<Mut>::nilsimsa_similarity_hash(fragment_category cat) const {
  return self()->nilsimsa_similarity_hash(cat);
}

template <detail::mutability Mut>
file_size_t basic_inode_handle<Mut>::size() const {
  return self()->size(*this->storage_);
}

template <detail::mutability Mut>
const_file_handle basic_inode_handle<Mut>::any() const {
  return self()->any(*this->storage_);
}

template <detail::mutability Mut>
file_id_vector const& basic_inode_handle<Mut>::all_file_ids() const {
  return self()->all();
}

template <detail::mutability Mut>
bool basic_inode_handle<Mut>::append_chunks_to(
    std::vector<thrift::metadata::chunk>& vec,
    std::optional<inode_hole_mapper>& hole_mapper) const {
  return self()->append_chunks_to(vec, hole_mapper);
}

template <detail::mutability Mut>
inode_fragments const& basic_inode_handle<Mut>::fragments() const {
  return self()->fragments();
}

template <detail::mutability Mut>
inode_fragments& basic_inode_handle<Mut>::fragments()
  requires is_mutable
{
  return self()->fragments();
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::dump(std::ostream& os,
                                   inode_options const& options) const {
  self()->dump(*this->storage_, os, options);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_scan_error(const_file_handle fp,
                                             std::exception_ptr ep)
  requires is_mutable
{
  self()->set_scan_error(fp, ep);
}

template <detail::mutability Mut>
std::optional<std::pair<const_file_handle, std::exception_ptr>>
basic_inode_handle<Mut>::get_scan_error() const {
  return self()->get_scan_error();
}

template <detail::mutability Mut>
inode_mmap_any_result
basic_inode_handle<Mut>::mmap_any(os_access const& os,
                                  open_file_options const& of_opts) const {
  return self()->mmap_any(*this->storage_, os, of_opts);
}

template class basic_inode_handle<detail::mutability::const_>;
template class basic_inode_handle<detail::mutability::mutable_>;

} // namespace dwarfs::writer::internal

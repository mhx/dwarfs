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

#include <algorithm>

#include <dwarfs/os_access.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/detail/inode_scanner.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/inode_handle.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal {

namespace {

// TODO: maybe move this into entry_storage?
bool append_chunks_to_impl(entry_storage& storage, inode_id id,
                           std::vector<thrift::metadata::chunk>& vec,
                           std::optional<inode_hole_mapper>& hole_mapper) {
  auto fragments = inode_fragments_view(storage, id);

  for (auto const& frag : fragments) {
    if (!frag.chunks_are_consistent()) {
      return false;
    }
  }

  for (auto const& frag : fragments) {
    for (auto const& src : frag.chunks()) {
      auto& chk = vec.emplace_back();
      if (src.is_hole()) {
        DWARFS_CHECK(hole_mapper.has_value(),
                     "inode has hole chunk but there's no hole mapper");
        auto& hm = hole_mapper.value();
        hm.map_hole(chk, src.size());
      } else {
        chk.block() = src.block();
        chk.offset() = src.offset();
        chk.size() = src.size();
      }
    }
  }

  return true;
}

void dump_impl(entry_storage& storage, inode_id self_id, std::ostream& os,
               inode_options const& options) {
  auto catlabel =
      [catmgr = options.categorizer_mgr](fragment_category cat) -> std::string {
    std::ostringstream oss;
    if (catmgr) {
      oss << "[" << catmgr->category_name(cat.value());
      if (cat.has_subcategory()) {
        oss << "/" << cat.subcategory();
      }
      oss << "] ";
    }
    return oss.str();
  };

  auto self = storage.handle(self_id);
  auto const& files = self.all_file_ids();

  std::string ino_num{"?"};

  if (auto maybe_num = storage.get_inode_num(self_id)) {
    ino_num = std::to_string(*maybe_num);
  }

  os << "inode " << ino_num << " (" << self.size() << " bytes):\n";
  os << "  files:\n";

  for (auto const& f : files) {
    auto fh = storage.handle(f);
    os << "    " << fh.path_as_string();
    if (fh.is_invalid()) {
      os << " (invalid)";
    }
    os << "\n";
  }

  os << "  fragments:\n";

  for (auto const& f : self.fragments_view()) {
    os << "    " << catlabel(f.category()) << "(" << f.size() << " bytes)\n";
    for (auto const& c : f.chunks()) {
      os << "      (" << c.block() << ", " << c.offset() << ", " << c.size()
         << ")\n";
    }
  }

  storage.dump_inode_similarity(self_id, os, catlabel);
}

auto mmap_any_impl(entry_storage& storage, inode_id self_id,
                   os_access const& os, open_file_options const& of_opts)
    -> inode_mmap_any_result {
  auto self = storage.handle(self_id);
  auto const& files = self.all_file_ids();
  file_view mm;
  const_file_handle rfh;
  std::vector<std::pair<const_file_handle, std::exception_ptr>> errors;

  for (auto fp : files) {
    auto fh = storage.handle(fp);

    if (!fh.is_invalid()) {
      try {
        mm = os.open_file_with_options(fh.fs_path(), of_opts);
        if (mm.size() != fh.size()) {
          auto const now_size = mm.size();
          mm.reset();
          throw std::runtime_error(fmt::format(
              "file size changed: was {}, now {}", fh.size(), now_size));
        }
        rfh = fh;
        break;
      } catch (...) {
        fh.set_invalid();
        errors.emplace_back(fh, std::current_exception());
      }
    }
  }

  return {std::move(mm), rfh, std::move(errors)};
}

} // namespace

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_files(file_id_vector const& fv)
  requires is_mutable
{
  storage_->set_files_for_inode(self_id_, fv);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::populate(file_size_t size)
  requires is_mutable
{
  detail::inode_scanner(*storage_, self_id_).populate(size);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::scan(file_view const& mm,
                                   inode_options const& options, progress& prog)
  requires is_mutable
{
  detail::inode_scanner(*storage_, self_id_).scan(mm, options, prog);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_num(uint32_t num)
  requires is_mutable
{
  storage_->set_inode_num(self_id_, num);
}

template <detail::mutability Mut>
uint32_t basic_inode_handle<Mut>::num() const {
  auto maybe_num = storage_->get_inode_num(self_id_);
  DWARFS_CHECK(maybe_num.has_value(), "inode number is not set");
  return *maybe_num;
}

template <detail::mutability Mut>
bool basic_inode_handle<Mut>::has_category(fragment_category cat) const {
  auto fragments = fragments_view();
  DWARFS_CHECK(!fragments.empty(), "has_category() called with no fragments");
  return std::ranges::any_of(
      fragments, [cat](auto const& f) { return f.category() == cat; });
}

template <detail::mutability Mut>
std::optional<uint32_t>
basic_inode_handle<Mut>::similarity_hash(fragment_category cat) const {
  return storage_->get_inode_similarity_hash(self_id_, cat);
}

template <detail::mutability Mut>
nilsimsa::hash_type const*
basic_inode_handle<Mut>::nilsimsa_similarity_hash(fragment_category cat) const {
  return storage_->get_inode_nilsimsa_hash(self_id_, cat);
}

template <detail::mutability Mut>
file_size_t basic_inode_handle<Mut>::size() const {
  return any().size();
}

template <detail::mutability Mut>
const_file_handle basic_inode_handle<Mut>::any() const {
  auto const& files = storage_->get_files_for_inode(self_id_);

  DWARFS_CHECK(!files.empty(), "inode has no file (any)");

  for (auto const& f : files) {
    auto fh = storage_->handle(f);
    if (!fh.is_invalid()) {
      return fh;
    }
  }

  return storage_->handle(files.front());
}

template <detail::mutability Mut>
file_id_vector const& basic_inode_handle<Mut>::all_file_ids() const {
  return storage_->get_files_for_inode(self_id_);
}

template <detail::mutability Mut>
bool basic_inode_handle<Mut>::append_chunks_to(
    std::vector<thrift::metadata::chunk>& vec,
    std::optional<inode_hole_mapper>& hole_mapper) const {
  return append_chunks_to_impl(*storage_, self_id_, vec, hole_mapper);
}

template <detail::mutability Mut>
inode_fragments_view basic_inode_handle<Mut>::fragments_view() const {
  return {*storage_, self_id_};
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::dump(std::ostream& os,
                                   inode_options const& options) const {
  dump_impl(*storage_, self_id_, os, options);
}

template <detail::mutability Mut>
void basic_inode_handle<Mut>::set_scan_error(const_file_handle fp,
                                             std::exception_ptr ep)
  requires is_mutable
{
  storage_->set_inode_scan_error(self_id_, fp.id(), std::move(ep));
}

template <detail::mutability Mut>
std::optional<std::pair<const_file_handle, std::exception_ptr>>
basic_inode_handle<Mut>::get_scan_error() const {
  std::optional<std::pair<const_file_handle, std::exception_ptr>> rv;

  if (auto err = storage_->get_inode_scan_error(self_id_)) {
    rv.emplace(storage_->handle(err->first), std::move(err->second));
  }

  return rv;
}

template <detail::mutability Mut>
inode_mmap_any_result
basic_inode_handle<Mut>::mmap_any(os_access const& os,
                                  open_file_options const& of_opts) const {
  return mmap_any_impl(*storage_, self_id_, os, of_opts);
}

template class basic_inode_handle<detail::mutability::const_>;
template class basic_inode_handle<detail::mutability::mutable_>;

} // namespace dwarfs::writer::internal

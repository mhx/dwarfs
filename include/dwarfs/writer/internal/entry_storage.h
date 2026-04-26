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

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>

#include <dwarfs/file_stat.h>

#include <dwarfs/writer/internal/entry_handle.h>
#include <dwarfs/writer/internal/entry_id.h>
#include <dwarfs/writer/internal/inode_handle.h>
#include <dwarfs/writer/internal/inode_id.h>
#include <dwarfs/writer/internal/unique_inode_id.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer {

class inode_fragments;
struct metadata_options;

namespace internal {

class global_entry_data;
class progress;
class provisional_entry;
class time_resolution_converter;

template <typename LoggerPolicy>
class inode_manager_;

class entry;
class file;
class dir;
class link;
class device;

class entry_storage {
 public:
  entry_storage();
  explicit entry_storage(metadata_options const& options);
  ~entry_storage();

  entry_storage(entry_storage&&) noexcept;
  entry_storage& operator=(entry_storage&&) noexcept;

  entry_storage(entry_storage const&) = delete;
  entry_storage& operator=(entry_storage const&) = delete;

  [[nodiscard]] entry_handle handle(entry_id id) noexcept {
    return {*this, id};
  }
  [[nodiscard]] file_handle handle(file_id id) noexcept { return {*this, id}; }
  [[nodiscard]] dir_handle handle(dir_id id) noexcept { return {*this, id}; }
  [[nodiscard]] link_handle handle(link_id id) noexcept { return {*this, id}; }
  [[nodiscard]] device_handle handle(device_id id) noexcept {
    return {*this, id};
  }
  [[nodiscard]] other_handle handle(other_id id) noexcept {
    return {*this, id};
  }

  [[nodiscard]] inode_handle handle(inode_id id) noexcept {
    return {*this, id};
  }

  [[nodiscard]] bool empty() const noexcept { return entry_impl_->empty(); }

  void set_entry_index(entry_id id, std::size_t index) {
    entry_impl_->set_entry_index(id, index);
  }

  [[nodiscard]] std::optional<std::size_t> get_entry_index(entry_id id) const {
    return entry_impl_->get_entry_index(id);
  }

  void set_file_order_index(file_id id, std::size_t index) {
    entry_impl_->set_file_order_index(id, index);
  }

  [[nodiscard]] std::size_t get_file_order_index(file_id id) const {
    return entry_impl_->get_file_order_index(id);
  }

  void set_link_target(link_id id, std::string link_target, progress& prog) {
    entry_impl_->set_link_target(id, std::move(link_target), prog);
  }

  [[nodiscard]] std::string get_link_target(link_id id) const {
    return entry_impl_->get_link_target(id);
  }

  [[nodiscard]] dir_id get_parent(entry_id id) const {
    return entry_impl_->get_parent(id);
  }

  [[nodiscard]] std::filesystem::path get_path(entry_id id) const {
    return entry_impl_->get_path(id);
  }

  [[nodiscard]] std::string get_unix_dpath(entry_id id) const {
    return entry_impl_->get_unix_dpath(id);
  }

  [[nodiscard]] std::string get_name(entry_id id) const {
    return entry_impl_->get_name(id);
  }

  void remove_empty_dirs(progress& prog) {
    entry_impl_->remove_empty_dirs(prog);
  }

  void for_each_entry_in_dir(dir_id id,
                             std::function<void(entry_id)> const& f) const {
    entry_impl_->for_each_entry_in_dir(id, f);
  }

  entry_id find_in_dir(dir_id id, std::string_view name) const {
    return entry_impl_->find_in_dir(id, name);
  }

  bool entry_less_revpath(entry_id lhs, entry_id rhs) const {
    return entry_impl_->entry_less_revpath(lhs, rhs);
  }

  std::vector<std::string> get_sorted_path_components() const {
    return entry_impl_->get_sorted_path_components();
  }

  std::size_t get_path_component_index(entry_id id) const {
    return entry_impl_->get_path_component_index(id);
  }

  void update_global_entry_data(entry_id id, global_entry_data& data) const {
    entry_impl_->update_global_entry_data(id, data);
  }

  void
  pack_entry(entry_id id,
             thrift::metadata::metadata::inodes_member_type::reference entry_v2,
             global_entry_data const& data,
             time_resolution_converter const& timeres) const {
    entry_impl_->pack_entry(id, entry_v2, data, timeres);
  }

  unique_inode_id get_unique_inode_id(entry_id id) const {
    return entry_impl_->get_unique_inode_id(id);
  }

  file_stat::nlink_type get_nlink(entry_id id) const {
    return entry_impl_->get_nlink(id);
  }

  void create_hardlink(file_id target, file_id source, progress& prog) {
    entry_impl_->create_hardlink(target, source, prog);
  }

  std::size_t hardlink_count(file_id id) const {
    return entry_impl_->hardlink_count(id);
  }

  void set_file_invalid(file_id id) { entry_impl_->set_file_invalid(id); }

  bool is_file_invalid(file_id id) const {
    return entry_impl_->is_file_invalid(id);
  }

  std::span<std::byte>
  get_file_hash_buffer(file_id id, std::size_t buffer_size) {
    return entry_impl_->get_file_hash_buffer(id, buffer_size);
  }

  std::string_view get_file_hash(file_id id) const {
    return entry_impl_->get_file_hash(id);
  }

  file_size_t get_entry_size(entry_id id) const {
    return entry_impl_->get_entry_size(id);
  }

  file_size_info get_entry_size_info(entry_id id) const {
    return entry_impl_->get_entry_size_info(id);
  }

  void set_entry_empty(entry_id id) { entry_impl_->set_entry_empty(id); }

  void set_inode_num_for_entry(entry_id id, std::uint64_t ino) {
    entry_impl_->set_inode_num_for_entry(id, ino);
  }

  std::optional<std::uint64_t> get_inode_num_for_entry(entry_id id) const {
    return entry_impl_->get_inode_num_for_entry(id);
  }

  file_stat::dev_type get_represented_device(device_id id) const {
    return entry_impl_->get_represented_device(id);
  }

  void create_packed_file_data(file_id id) {
    entry_impl_->create_packed_file_data(id);
  }

  void set_file_inode(file_id id, inode_id ino) {
    entry_impl_->set_file_inode(id, ino);
  }

  inode_id get_file_inode(file_id id) const {
    return entry_impl_->get_file_inode(id);
  }

  void drop_file_hashes() { entry_impl_->drop_file_hashes(); }

  inode_handle create_inode();

  [[nodiscard]] std::size_t inode_count() const noexcept {
    return inode_impl_->inode_count();
  }

  [[nodiscard]] file_id_vector const& get_files_for_inode(inode_id id) const {
    return inode_impl_->get_files_for_inode(id);
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) {
    inode_impl_->set_files_for_inode(id, std::move(fv));
  }

  void set_inode_scan_error(inode_id id, file_id fid, std::exception_ptr ep) {
    inode_impl_->set_inode_scan_error(id, fid, std::move(ep));
  }

  std::optional<std::pair<file_id, std::exception_ptr>>
  get_inode_scan_error(inode_id id) const {
    return inode_impl_->get_inode_scan_error(id);
  }

  void set_inode_num(inode_id id, std::uint64_t num) {
    inode_impl_->set_inode_num(id, num);
  }

  std::optional<std::uint64_t> get_inode_num(inode_id id) const {
    return inode_impl_->get_inode_num(id);
  }

  void set_inode_fragments(inode_id id, inode_fragments const& fragments) {
    inode_impl_->set_inode_fragments(id, fragments);
  }

  void inode_fragment_add_data_chunk(inode_id id, std::size_t fragment_index,
                                     size_t block, size_t offset, size_t size) {
    inode_impl_->inode_fragment_add_data_chunk(id, fragment_index, block,
                                               offset, size);
  }

  void inode_fragment_add_hole_chunk(inode_id id, std::size_t fragment_index,
                                     file_size_t size) {
    inode_impl_->inode_fragment_add_hole_chunk(id, fragment_index, size);
  }

  std::size_t get_inode_fragment_count(inode_id id) const {
    return inode_impl_->get_inode_fragment_count(id);
  }

  fragment_category
  get_inode_fragment_category(inode_id id, std::size_t index) const {
    return inode_impl_->get_inode_fragment_category(id, index);
  }

  file_size_t get_inode_fragment_size(inode_id id, std::size_t index) const {
    return inode_impl_->get_inode_fragment_size(id, index);
  }

  packed_chunk_vector const&
  get_inode_fragment_packed_chunks(inode_id id, std::size_t index) const {
    return inode_impl_->get_inode_fragment_packed_chunks(id, index);
  }

  void set_inode_similarity(inode_id id,
                            std::span<inode_similarity_hash_data const> data) {
    inode_impl_->set_inode_similarity(id, data);
  }

  std::optional<std::uint32_t>
  get_inode_similarity_hash(inode_id id, fragment_category cat) const {
    return inode_impl_->get_inode_similarity_hash(id, cat);
  }

  nilsimsa::hash_type const*
  get_inode_nilsimsa_hash(inode_id id, fragment_category cat) const {
    return inode_impl_->get_inode_nilsimsa_hash(id, cat);
  }

  void dump_inode_similarity(
      inode_id id, std::ostream& os,
      std::function<std::string(fragment_category)> const& catlabel) const {
    inode_impl_->dump_inode_similarity(id, os, catlabel);
  }

  void dump(std::ostream& os) const;
  std::string dump() const;

  void dump_entries(std::ostream& os) const;
  std::string dump_entries() const;

  void dump_inodes(std::ostream& os) const;
  std::string dump_inodes() const;

  void freeze_entries() noexcept;
  void freeze_inodes() noexcept;

  class entry_impl {
   public:
    virtual ~entry_impl() = default;

    virtual entry_id make_file(std::filesystem::path const& path,
                               file_stat const& st, dir_id parent) = 0;
    virtual entry_id make_dir(std::filesystem::path const& path,
                              file_stat const& st, dir_id parent) = 0;
    virtual entry_id make_link(std::filesystem::path const& path,
                               file_stat const& st, dir_id parent) = 0;
    virtual entry_id make_device(std::filesystem::path const& path,
                                 file_stat const& st, dir_id parent) = 0;
    virtual entry_id make_other(std::filesystem::path const& path,
                                file_stat const& st, dir_id parent) = 0;

    virtual void create_packed_file_data(file_id id) = 0;

    virtual void set_file_inode(file_id id, inode_id ino) = 0;
    virtual inode_id get_file_inode(file_id id) const = 0;

    virtual void set_entry_index(entry_id id, std::size_t index) = 0;
    virtual std::optional<std::size_t> get_entry_index(entry_id id) const = 0;

    virtual void set_file_order_index(file_id id, std::size_t index) = 0;
    virtual std::size_t get_file_order_index(file_id id) const = 0;

    virtual void
    set_link_target(link_id id, std::string link_target, progress& prog) = 0;
    virtual std::string get_link_target(link_id id) const = 0;

    virtual dir_id get_parent(entry_id id) const = 0;
    virtual std::filesystem::path get_path(entry_id id) const = 0;
    virtual std::string get_unix_dpath(entry_id id) const = 0;
    virtual std::string get_name(entry_id id) const = 0;
    virtual void remove_empty_dirs(progress& prog) = 0;
    virtual void
    for_each_entry_in_dir(dir_id id,
                          std::function<void(entry_id)> const& f) const = 0;
    virtual entry_id find_in_dir(dir_id id, std::string_view name) const = 0;
    virtual bool entry_less_revpath(entry_id lhs, entry_id rhs) const = 0;
    virtual std::vector<std::string> get_sorted_path_components() const = 0;
    virtual std::size_t get_path_component_index(entry_id id) const = 0;

    virtual void
    update_global_entry_data(entry_id id, global_entry_data& data) const = 0;
    virtual void pack_entry(
        entry_id id,
        thrift::metadata::metadata::inodes_member_type::reference entry_v2,
        global_entry_data const& data,
        time_resolution_converter const& timeres) const = 0;

    virtual unique_inode_id get_unique_inode_id(entry_id id) const = 0;

    virtual file_stat::nlink_type get_nlink(entry_id id) const = 0;

    virtual void
    create_hardlink(file_id target, file_id source, progress& prog) = 0;
    virtual std::size_t hardlink_count(file_id id) const = 0;

    virtual void set_file_invalid(file_id id) = 0;
    virtual bool is_file_invalid(file_id id) const = 0;

    virtual std::span<std::byte>
    get_file_hash_buffer(file_id id, std::size_t buffer_size) = 0;
    virtual std::string_view get_file_hash(file_id id) const = 0;

    virtual file_size_t get_entry_size(entry_id id) const = 0;
    virtual file_size_info get_entry_size_info(entry_id id) const = 0;
    virtual void set_entry_empty(entry_id id) = 0;

    virtual void set_inode_num_for_entry(entry_id id, std::uint64_t ino) = 0;
    virtual std::optional<std::uint64_t>
    get_inode_num_for_entry(entry_id id) const = 0;

    virtual file_stat::dev_type get_represented_device(device_id id) const = 0;

    virtual bool empty() const = 0;
    virtual void dump(std::ostream& os) const = 0;
    virtual void dump_events(std::ostream& os) const = 0;

    virtual void drop_file_hashes() = 0;

    virtual std::unique_ptr<entry_impl> freeze() = 0;
  };

  class inode_impl {
   public:
    virtual ~inode_impl() = default;

    virtual inode_id make_inode() = 0;
    virtual std::size_t inode_count() const = 0;

    virtual file_id_vector const& get_files_for_inode(inode_id id) const = 0;
    virtual void set_files_for_inode(inode_id id, file_id_vector fv) = 0;

    virtual void
    set_inode_scan_error(inode_id id, file_id fid, std::exception_ptr ep) = 0;
    virtual std::optional<std::pair<file_id, std::exception_ptr>>
    get_inode_scan_error(inode_id id) const = 0;

    virtual void set_inode_num(inode_id id, std::uint64_t num) = 0;
    virtual std::optional<std::uint64_t> get_inode_num(inode_id id) const = 0;

    virtual void
    set_inode_fragments(inode_id id, inode_fragments const& fragments) = 0;

    virtual void
    inode_fragment_add_data_chunk(inode_id id, std::size_t fragment_index,
                                  size_t block, size_t offset, size_t size) = 0;
    virtual void
    inode_fragment_add_hole_chunk(inode_id id, std::size_t fragment_index,
                                  file_size_t size) = 0;

    virtual std::size_t get_inode_fragment_count(inode_id id) const = 0;
    virtual fragment_category
    get_inode_fragment_category(inode_id id, std::size_t index) const = 0;
    virtual file_size_t
    get_inode_fragment_size(inode_id id, std::size_t index) const = 0;
    virtual packed_chunk_vector const&
    get_inode_fragment_packed_chunks(inode_id id, std::size_t index) const = 0;

    virtual void
    set_inode_similarity(inode_id id,
                         std::span<inode_similarity_hash_data const> data) = 0;
    virtual std::optional<std::uint32_t>
    get_inode_similarity_hash(inode_id id, fragment_category cat) const = 0;
    virtual nilsimsa::hash_type const*
    get_inode_nilsimsa_hash(inode_id id, fragment_category cat) const = 0;
    virtual void
    dump_inode_similarity(inode_id id, std::ostream& os,
                          std::function<std::string(fragment_category)> const&
                              catlabel) const = 0;

    virtual void dump(std::ostream& os) const = 0;
    virtual void dump_events(std::ostream& os) const = 0;

    virtual std::unique_ptr<inode_impl> freeze() = 0;
  };

 private:
  friend class provisional_entry;

  dir_handle
  create_root_dir(std::filesystem::path const& path, file_stat const& st);

  file_handle create_file(std::filesystem::path const& path, dir_handle parent,
                          file_stat const& st);

  dir_handle create_dir(std::filesystem::path const& path, dir_handle parent,
                        file_stat const& st);

  link_handle create_link(std::filesystem::path const& path, dir_handle parent,
                          file_stat const& st);

  device_handle create_device(std::filesystem::path const& path,
                              dir_handle parent, file_stat const& st);

  other_handle create_other(std::filesystem::path const& path,
                            dir_handle parent, file_stat const& st);

  std::unique_ptr<entry_impl> entry_impl_;
  std::unique_ptr<inode_impl> inode_impl_;
};

} // namespace internal
} // namespace dwarfs::writer

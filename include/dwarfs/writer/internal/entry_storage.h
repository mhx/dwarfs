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

namespace dwarfs::writer::internal {

struct file_data;
class progress;
class provisional_entry;

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

  [[nodiscard]] entry_handle root() noexcept {
    return {*this, entry_id(entry_type::E_DIR, 0)};
  }

  [[nodiscard]] bool empty() const noexcept { return impl_->empty(); }

  // TODO: this must go
  [[nodiscard]] entry* get_entry(entry_id id) { return impl_->get_entry(id); }

  inode_handle create_inode();

  [[nodiscard]] std::size_t inode_count() const noexcept {
    return impl_->inode_count();
  }

  // TODO: this must go
  [[nodiscard]] inode* get_inode(inode_id id) { return impl_->get_inode(id); }

  [[nodiscard]] entry_id get_parent(entry_id id) const {
    return impl_->get_parent(id);
  }

  [[nodiscard]] std::filesystem::path get_path(entry_id id) const {
    return impl_->get_path(id);
  }

  [[nodiscard]] std::string get_unix_dpath(entry_id id) const {
    return impl_->get_unix_dpath(id);
  }

  [[nodiscard]] std::string_view get_name(entry_id id) const {
    return impl_->get_name(id);
  }

  [[nodiscard]] bool is_dir_empty(entry_id id) const {
    return impl_->is_dir_empty(id);
  }

  void remove_empty_dirs(progress& prog) {
    return impl_->remove_empty_dirs(prog);
  }

  void for_each_entry_in_dir(entry_id id,
                             std::function<void(entry_id)> const& f) const {
    return impl_->for_each_entry_in_dir(id, f);
  }

  entry_id find_in_dir(entry_id id, std::string_view name) const {
    return impl_->find_in_dir(id, name);
  }

  size_t create_file_data() { return impl_->create_file_data(); }

  // TODO: this is probably not needed long-term
  [[nodiscard]] file_data& get_file_data(size_t id) {
    return impl_->get_file_data(id);
  }

  [[nodiscard]] file_id_vector const& get_files_for_inode(inode_id id) const {
    return impl_->get_files_for_inode(id);
  }

  void set_files_for_inode(inode_id id, file_id_vector fv) {
    impl_->set_files_for_inode(id, fv);
  }

  void dump(std::ostream& os) const { impl_->dump(os); }

  std::string dump() const;

  void freeze() noexcept;

  class impl {
   public:
    virtual ~impl() = default;

    virtual entry_id make_file(std::filesystem::path const& path,
                               file_stat const& st, entry_id parent) = 0;
    virtual entry_id make_dir(std::filesystem::path const& path,
                              file_stat const& st, entry_id parent) = 0;
    virtual entry_id make_link(std::filesystem::path const& path,
                               file_stat const& st, entry_id parent) = 0;
    virtual entry_id make_device(std::filesystem::path const& path,
                                 file_stat const& st, entry_id parent) = 0;
    virtual entry_id make_other(std::filesystem::path const& path,
                                file_stat const& st, entry_id parent) = 0;
    virtual inode_id make_inode() = 0;

    virtual size_t create_file_data() = 0;
    virtual file_data& get_file_data(size_t id) = 0;
    virtual entry* get_entry(entry_id id) = 0;
    virtual std::size_t inode_count() const = 0;
    virtual inode* get_inode(inode_id id) = 0;

    virtual file_id_vector const& get_files_for_inode(inode_id id) const = 0;
    virtual void set_files_for_inode(inode_id id, file_id_vector fv) = 0;

    virtual entry_id get_parent(entry_id id) const = 0;
    virtual std::filesystem::path get_path(entry_id id) const = 0;
    virtual std::string get_unix_dpath(entry_id id) const = 0;
    virtual std::string_view get_name(entry_id id) const = 0;
    virtual bool is_dir_empty(entry_id id) const = 0;
    virtual void remove_empty_dirs(progress& prog) = 0;
    virtual void
    for_each_entry_in_dir(entry_id id,
                          std::function<void(entry_id)> const& f) const = 0;
    virtual entry_id find_in_dir(entry_id id, std::string_view name) const = 0;

    virtual bool empty() const = 0;
    virtual void dump(std::ostream& os) const = 0;

    virtual std::unique_ptr<impl> freeze() = 0;
  };

 private:
  friend class provisional_entry;

  dir_handle
  create_root_dir(std::filesystem::path const& path, file_stat const& st);

  file_handle create_file(std::filesystem::path const& path,
                          entry_handle parent, file_stat const& st);

  dir_handle create_dir(std::filesystem::path const& path, entry_handle parent,
                        file_stat const& st);

  link_handle create_link(std::filesystem::path const& path,
                          entry_handle parent, file_stat const& st);

  device_handle create_device(std::filesystem::path const& path,
                              entry_handle parent, file_stat const& st);

  other_handle create_other(std::filesystem::path const& path,
                            entry_handle parent, file_stat const& st);

  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::writer::internal

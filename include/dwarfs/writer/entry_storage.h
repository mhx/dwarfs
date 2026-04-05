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
#include <iosfwd>
#include <memory>

#include <dwarfs/file_stat.h>
#include <dwarfs/writer/entry_handle.h>
#include <dwarfs/writer/entry_id.h>

namespace dwarfs::writer {

namespace internal {

struct file_data;
class provisional_entry;

class entry;
class file;
class dir;
class link;
class device;

} // namespace internal

class entry_storage {
 public:
  entry_storage();
  ~entry_storage();

  entry_storage(entry_storage&&) noexcept;
  entry_storage& operator=(entry_storage&&) noexcept;

  entry_storage(entry_storage const&) = delete;
  entry_storage& operator=(entry_storage const&) = delete;

  [[nodiscard]] entry_handle root() noexcept {
    return {*this, entry_id(entry_type::E_DIR, 0)};
  }

  [[nodiscard]] bool empty() const noexcept { return impl_->empty(); }

  [[nodiscard]] internal::entry* get_entry(entry_id id) {
    return impl_->get_entry(id);
  }

  size_t create_file_data() { return impl_->create_file_data(); }

  [[nodiscard]] internal::file_data& get_file_data(size_t id) {
    return impl_->get_file_data(id);
  }

  void dump(std::ostream& os) const { impl_->dump(os); }

  std::string dump() const;

  void freeze() noexcept;

  class impl {
   public:
    virtual ~impl() = default;

    virtual entry_id make_file(std::filesystem::path const& path,
                               file_stat const& st, entry_id id) = 0;
    virtual entry_id make_dir(std::filesystem::path const& path,
                              file_stat const& st, entry_id id) = 0;
    virtual entry_id make_link(std::filesystem::path const& path,
                               file_stat const& st, entry_id id) = 0;
    virtual entry_id make_device(std::filesystem::path const& path,
                                 file_stat const& st, entry_id id) = 0;

    virtual size_t create_file_data() = 0;
    virtual internal::file_data& get_file_data(size_t id) = 0;
    virtual internal::entry* get_entry(entry_id id) = 0;

    virtual bool empty() const = 0;
    virtual void dump(std::ostream& os) const = 0;

    virtual std::unique_ptr<impl> freeze() = 0;
  };

 private:
  friend class internal::provisional_entry;

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

  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::writer

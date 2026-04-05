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

#include <cassert>
#include <ostream>
#include <sstream>
#include <utility>

#include <dwarfs/error.h>
#include <dwarfs/writer/entry_storage.h>

#include <dwarfs/internal/chunked_append_only_vector.h>
#include <dwarfs/internal/synchronized.h>
#include <dwarfs/writer/internal/entry.h>

namespace dwarfs::writer {

class entry_storage::impl {
 public:
  entry_id make_file(std::filesystem::path const& path, internal::entry* parent,
                     file_stat const& st, entry_id const id) {
    auto const ix = files_.with_lock([&](auto& files) {
      auto ix = files.size();
      files.emplace_back(path, parent, st, id);
      return ix;
    });
    return {entry_type::E_FILE, ix};
  }

  entry_id make_dir(std::filesystem::path const& path, internal::entry* parent,
                    file_stat const& st, entry_id const id) {
    auto const ix = dirs_.with_lock([&](auto& dirs) {
      auto ix = dirs.size();
      dirs.emplace_back(path, parent, st, id);
      return ix;
    });
    return {entry_type::E_DIR, ix};
  }

  entry_id make_link(std::filesystem::path const& path, internal::entry* parent,
                     file_stat const& st, entry_id const id) {
    auto const ix = links_.with_lock([&](auto& links) {
      auto ix = links.size();
      links.emplace_back(path, parent, st, id);
      return ix;
    });
    return {entry_type::E_LINK, ix};
  }

  entry_id
  make_device(std::filesystem::path const& path, internal::entry* parent,
              file_stat const& st, entry_id const id) {
    return devices_.with_lock([&](auto& devices) -> entry_id {
      auto ix = devices.size();
      auto const& dev = devices.emplace_back(path, parent, st, id);
      return {dev.is_device() ? entry_type::E_DEVICE : entry_type::E_OTHER, ix};
    });
  }

  bool empty() const noexcept { return dirs_.lock()->empty(); }

  internal::entry* root() noexcept {
    return dirs_.with_lock([](auto& dirs) -> internal::entry* {
      return dirs.empty() ? nullptr : &dirs.front();
    });
  }

  void dump(std::ostream& os) const;

  size_t create_file_data() {
    return file_data_.with_lock([](auto& fd) {
      auto id = fd.size();
      fd.emplace_back();
      return id;
    });
  }

  [[nodiscard]] internal::file_data& get_file_data(size_t const id) {
    return file_data_.lock()->at(id);
  }

  internal::entry* get_entry(entry_id const id) {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return &files_.lock()->at(id.index());
    case entry_type::E_DIR:
      return &dirs_.lock()->at(id.index());
    case entry_type::E_LINK:
      return &links_.lock()->at(id.index());
    case entry_type::E_DEVICE:
    case entry_type::E_OTHER:
      return &devices_.lock()->at(id.index());
    default:
      throw std::runtime_error("invalid entry type");
    }
  }

 private:
  dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<internal::file>>
      files_;
  dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<internal::dir>>
      dirs_;
  dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<internal::link>>
      links_;
  dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<internal::device>>
      devices_;
  dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<internal::file_data>>
      file_data_;
};

void entry_storage::impl::dump(std::ostream& os) const {
  os << "num dirs: " << dirs_.lock()->size() << "\n";
  os << "num files: " << files_.lock()->size() << "\n";
  os << "num file data: " << file_data_.lock()->size() << "\n";
  os << "num links: " << links_.lock()->size() << "\n";
  os << "num devices: " << devices_.lock()->size() << "\n";
}

entry_storage::entry_storage()
    : impl_(std::make_unique<impl>()) {}

entry_storage::~entry_storage() = default;
entry_storage::entry_storage(entry_storage&&) noexcept = default;
entry_storage& entry_storage::operator=(entry_storage&&) noexcept = default;

entry_handle entry_storage::root() noexcept {
  return {*this, entry_id(entry_type::E_DIR, 0)};
}

bool entry_storage::empty() const noexcept { return impl_->empty(); }

void entry_storage::dump(std::ostream& os) const { impl_->dump(os); }

std::string entry_storage::dump() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

dir_handle entry_storage::create_root_dir(std::filesystem::path const& path,
                                          file_stat const& st) {
  DWARFS_CHECK(empty(), "entry_storage root already set");
  return {*this, impl_->make_dir(path, nullptr, st, entry_id())};
}

file_handle
entry_storage::create_file(std::filesystem::path const& path,
                           entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_file(path, parent.base(), st, parent.id())};
}

dir_handle entry_storage::create_dir(std::filesystem::path const& path,
                                     entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_dir(path, parent.base(), st, parent.id())};
}

link_handle
entry_storage::create_link(std::filesystem::path const& path,
                           entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_link(path, parent.base(), st, parent.id())};
}

device_handle
entry_storage::create_device(std::filesystem::path const& path,
                             entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_device(path, parent.base(), st, parent.id())};
}

size_t entry_storage::create_file_data() { return impl_->create_file_data(); }

internal::file_data& entry_storage::get_file_data(size_t id) {
  return impl_->get_file_data(id);
}

internal::entry* entry_storage::get_entry(entry_id const id) {
  return impl_->get_entry(id);
}

} // namespace dwarfs::writer

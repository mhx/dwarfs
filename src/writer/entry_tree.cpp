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

#include <utility>
#include <vector>

#include <dwarfs/error.h>
#include <dwarfs/writer/entry_tree.h>
#include <dwarfs/writer/internal/entry.h>

namespace dwarfs::writer {

class entry_tree::impl {
 public:
  template <typename T, typename... Args>
  T* make(Args&&... args) {
    auto p = std::make_unique<T>(std::forward<Args>(args)...);
    auto* raw = p.get();
    entries_.emplace_back(std::move(p));
    return raw;
  }

 private:
  std::vector<std::unique_ptr<internal::entry>> entries_;
};

entry_tree::entry_tree()
    : impl_(std::make_unique<impl>()) {}

entry_tree::~entry_tree() = default;
entry_tree::entry_tree(entry_tree&&) noexcept = default;
entry_tree& entry_tree::operator=(entry_tree&&) noexcept = default;

internal::dir* entry_tree::create_root_dir(std::filesystem::path const& path,
                                           file_stat const& st) {
  DWARFS_CHECK(!root_, "entry_tree root already set");
  auto* d = impl_->make<internal::dir>(path, nullptr, st);
  root_ = d;
  return d;
}

internal::file*
entry_tree::create_file(std::filesystem::path const& path,
                        internal::entry* parent, file_stat const& st) {
  return impl_->make<internal::file>(path, parent, st);
}

internal::dir*
entry_tree::create_dir(std::filesystem::path const& path,
                       internal::entry* parent, file_stat const& st) {
  return impl_->make<internal::dir>(path, parent, st);
}

internal::link*
entry_tree::create_link(std::filesystem::path const& path,
                        internal::entry* parent, file_stat const& st) {
  return impl_->make<internal::link>(path, parent, st);
}

internal::device*
entry_tree::create_device(std::filesystem::path const& path,
                          internal::entry* parent, file_stat const& st) {
  return impl_->make<internal::device>(path, parent, st);
}

} // namespace dwarfs::writer

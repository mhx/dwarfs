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

namespace fs = std::filesystem;

namespace dwarfs::writer {

namespace {

[[noreturn]] void frozen_panic() { DWARFS_PANIC("entry_storage is frozen"); }

} // namespace

template <bool Frozen>
class entry_storage_ final : public entry_storage::impl {
 public:
  static constexpr bool is_mutable = !Frozen;
  template <typename T>
  using cao_vector = dwarfs::internal::chunked_append_only_vector<T>;

  friend class entry_storage_<true>;

  entry_storage_()
    requires is_mutable
  = default;

  entry_storage_(entry_storage_<false>& other) noexcept
    requires Frozen
      : files_{std::move(other.files_)}
      , dirs_{std::move(other.dirs_)}
      , links_{std::move(other.links_)}
      , devices_{std::move(other.devices_)}
      , others_{std::move(other.others_)}
      , file_data_{std::move(other.file_data_)} {}

  std::unique_ptr<impl> freeze() override {
    if constexpr (is_mutable) {
      return std::make_unique<entry_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  template <typename T>
  static entry_id
  make_obj_(cao_vector<T>& vec, entry_type type, fs::path const& path,
            file_stat const& st, entry_id const parent) {
    if constexpr (is_mutable) {
      auto ix = vec.size();
      vec.emplace_back(path, st, parent);
      return {type, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_file(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return make_obj_(files_, entry_type::E_FILE, path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    entry_id const parent) override {
    return make_obj_(dirs_, entry_type::E_DIR, path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return make_obj_(links_, entry_type::E_LINK, path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       entry_id const parent) override {
    return make_obj_(devices_, entry_type::E_DEVICE, path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      entry_id const parent) override {
    return make_obj_(others_, entry_type::E_OTHER, path, st, parent);
  }

  bool empty() const noexcept override { return dirs_.empty(); }

  void dump(std::ostream& os) const override;

  size_t create_file_data() override {
    if constexpr (is_mutable) {
      auto id = file_data_.size();
      file_data_.emplace_back();
      return id;
    } else {
      frozen_panic();
    }
  }

  [[nodiscard]] internal::file_data& get_file_data(size_t const id) override {
    return file_data_.at(id);
  }

  internal::entry* get_entry(entry_id const id) override {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return &files_.at(id.index());
    case entry_type::E_DIR:
      return &dirs_.at(id.index());
    case entry_type::E_LINK:
      return &links_.at(id.index());
    case entry_type::E_DEVICE:
      return &devices_.at(id.index());
    case entry_type::E_OTHER:
      return &others_.at(id.index());
    default:
      throw std::runtime_error("invalid entry type");
    }
  }

 private:
  cao_vector<internal::file> files_;
  cao_vector<internal::dir> dirs_;
  cao_vector<internal::link> links_;
  cao_vector<internal::device> devices_;
  cao_vector<internal::other> others_;
  cao_vector<internal::file_data> file_data_;
};

template <bool Frozen>
void entry_storage_<Frozen>::dump(std::ostream& os) const {
  os << "num dirs: " << dirs_.size() << "\n";
  os << "num files: " << files_.size() << "\n";
  os << "num file data: " << file_data_.size() << "\n";
  os << "num links: " << links_.size() << "\n";
  os << "num devices: " << devices_.size() << "\n";
  os << "num others: " << others_.size() << "\n";
}

class synchronized_entry_storage_ final : public entry_storage::impl {
 public:
  entry_id make_file(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return impl_.lock()->make_file(path, st, parent);
  }

  entry_id make_dir(fs::path const& path, file_stat const& st,
                    entry_id const parent) override {
    return impl_.lock()->make_dir(path, st, parent);
  }

  entry_id make_link(fs::path const& path, file_stat const& st,
                     entry_id const parent) override {
    return impl_.lock()->make_link(path, st, parent);
  }

  entry_id make_device(fs::path const& path, file_stat const& st,
                       entry_id const parent) override {
    return impl_.lock()->make_device(path, st, parent);
  }

  entry_id make_other(fs::path const& path, file_stat const& st,
                      entry_id const parent) override {
    return impl_.lock()->make_other(path, st, parent);
  }

  size_t create_file_data() override {
    return impl_.lock()->create_file_data();
  }

  internal::file_data& get_file_data(size_t const id) override {
    return impl_.lock()->get_file_data(id);
  }

  internal::entry* get_entry(entry_id const id) override {
    return impl_.lock()->get_entry(id);
  }

  bool empty() const noexcept override { return impl_.lock()->empty(); }
  void dump(std::ostream& os) const override { impl_.lock()->dump(os); }

  std::unique_ptr<impl> freeze() override { return impl_.lock()->freeze(); }

 private:
  dwarfs::internal::synchronized<entry_storage_<false>> impl_;
};

entry_storage::entry_storage()
    : impl_(std::make_unique<synchronized_entry_storage_>()) {}

entry_storage::~entry_storage() = default;
entry_storage::entry_storage(entry_storage&&) noexcept = default;
entry_storage& entry_storage::operator=(entry_storage&&) noexcept = default;

std::string entry_storage::dump() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

void entry_storage::freeze() noexcept { impl_ = impl_->freeze(); }

dir_handle
entry_storage::create_root_dir(fs::path const& path, file_stat const& st) {
  DWARFS_CHECK(empty(), "entry_storage root already set");
  return {*this, impl_->make_dir(path, st, entry_id())};
}

file_handle
entry_storage::create_file(fs::path const& path, entry_handle parent,
                           file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_file(path, st, parent.id())};
}

dir_handle entry_storage::create_dir(fs::path const& path, entry_handle parent,
                                     file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_dir(path, st, parent.id())};
}

link_handle
entry_storage::create_link(fs::path const& path, entry_handle parent,
                           file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_link(path, st, parent.id())};
}

device_handle
entry_storage::create_device(fs::path const& path, entry_handle parent,
                             file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_device(path, st, parent.id())};
}

other_handle
entry_storage::create_other(fs::path const& path, entry_handle parent,
                            file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_other(path, st, parent.id())};
}

} // namespace dwarfs::writer

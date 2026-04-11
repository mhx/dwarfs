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

namespace {

[[noreturn]] void frozen_panic() { DWARFS_PANIC("entry_storage is frozen"); }

} // namespace

template <bool Frozen>
class entry_storage_ final : public entry_storage::impl {
 public:
  static constexpr bool is_mutable = !Frozen;
  using mutex_type =
      std::conditional_t<Frozen, dwarfs::internal::no_mutex, std::mutex>;
  template <typename T>
  using synchronized_vector = dwarfs::internal::synchronized<
      dwarfs::internal::chunked_append_only_vector<T>, mutex_type>;

  friend class entry_storage_<true>;

  entry_storage_()
    requires is_mutable
  = default;

  entry_storage_(entry_storage_<false>& other) noexcept
    requires Frozen
      : files_{other.files_.release()}
      , dirs_{other.dirs_.release()}
      , links_{other.links_.release()}
      , devices_{other.devices_.release()}
      , others_{other.others_.release()}
      , file_data_{other.file_data_.release()} {}

  std::unique_ptr<impl> freeze() override {
    if constexpr (is_mutable) {
      return std::make_unique<entry_storage_<true>>(*this);
    } else {
      frozen_panic();
    }
  }

  entry_id make_file(std::filesystem::path const& path, file_stat const& st,
                     entry_id const id) override {
    if constexpr (is_mutable) {
      auto const ix = files_.with_lock([&](auto& files) {
        auto ix = files.size();
        files.emplace_back(path, st, id);
        return ix;
      });
      return {entry_type::E_FILE, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_dir(std::filesystem::path const& path, file_stat const& st,
                    entry_id const id) override {
    if constexpr (is_mutable) {
      auto const ix = dirs_.with_lock([&](auto& dirs) {
        auto ix = dirs.size();
        dirs.emplace_back(path, st, id);
        return ix;
      });
      return {entry_type::E_DIR, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_link(std::filesystem::path const& path, file_stat const& st,
                     entry_id const id) override {
    if constexpr (is_mutable) {
      auto const ix = links_.with_lock([&](auto& links) {
        auto ix = links.size();
        links.emplace_back(path, st, id);
        return ix;
      });
      return {entry_type::E_LINK, ix};
    } else {
      frozen_panic();
    }
  }

  entry_id make_device(std::filesystem::path const& path, file_stat const& st,
                       entry_id const id) override {
    if constexpr (is_mutable) {
      return devices_.with_lock([&](auto& devices) -> entry_id {
        auto ix = devices.size();
        auto const& dev = devices.emplace_back(path, st, id);
        return {dev.type(), ix};
      });
    } else {
      frozen_panic();
    }
  }

  entry_id make_other(std::filesystem::path const& path, file_stat const& st,
                      entry_id const id) override {
    if constexpr (is_mutable) {
      return others_.with_lock([&](auto& others) -> entry_id {
        auto ix = others.size();
        auto const& dev = others.emplace_back(path, st, id);
        return {dev.type(), ix};
      });
    } else {
      frozen_panic();
    }
  }

  bool empty() const noexcept override { return dirs_.lock()->empty(); }

  void dump(std::ostream& os) const override;

  size_t create_file_data() override {
    if constexpr (is_mutable) {
      return file_data_.with_lock([](auto& fd) {
        auto id = fd.size();
        fd.emplace_back();
        return id;
      });
    } else {
      frozen_panic();
    }
  }

  [[nodiscard]] internal::file_data& get_file_data(size_t const id) override {
    return file_data_.lock()->at(id);
  }

  internal::entry* get_entry(entry_id const id) override {
    assert(id.valid());
    switch (id.type()) {
    case entry_type::E_FILE:
      return &files_.lock()->at(id.index());
    case entry_type::E_DIR:
      return &dirs_.lock()->at(id.index());
    case entry_type::E_LINK:
      return &links_.lock()->at(id.index());
    case entry_type::E_DEVICE:
      return &devices_.lock()->at(id.index());
    case entry_type::E_OTHER:
      return &others_.lock()->at(id.index());
    default:
      throw std::runtime_error("invalid entry type");
    }
  }

 private:
  synchronized_vector<internal::file> files_;
  synchronized_vector<internal::dir> dirs_;
  synchronized_vector<internal::link> links_;
  synchronized_vector<internal::device> devices_;
  synchronized_vector<internal::other> others_;
  synchronized_vector<internal::file_data> file_data_;
};

template <bool Frozen>
void entry_storage_<Frozen>::dump(std::ostream& os) const {
  os << "num dirs: " << dirs_.lock()->size() << "\n";
  os << "num files: " << files_.lock()->size() << "\n";
  os << "num file data: " << file_data_.lock()->size() << "\n";
  os << "num links: " << links_.lock()->size() << "\n";
  os << "num devices: " << devices_.lock()->size() << "\n";
  os << "num others: " << others_.lock()->size() << "\n";
}

entry_storage::entry_storage()
    : impl_(std::make_unique<entry_storage_<false>>()) {}

entry_storage::~entry_storage() = default;
entry_storage::entry_storage(entry_storage&&) noexcept = default;
entry_storage& entry_storage::operator=(entry_storage&&) noexcept = default;

std::string entry_storage::dump() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

void entry_storage::freeze() noexcept { impl_ = impl_->freeze(); }

dir_handle entry_storage::create_root_dir(std::filesystem::path const& path,
                                          file_stat const& st) {
  DWARFS_CHECK(empty(), "entry_storage root already set");
  return {*this, impl_->make_dir(path, st, entry_id())};
}

file_handle
entry_storage::create_file(std::filesystem::path const& path,
                           entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_file(path, st, parent.id())};
}

dir_handle entry_storage::create_dir(std::filesystem::path const& path,
                                     entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_dir(path, st, parent.id())};
}

link_handle
entry_storage::create_link(std::filesystem::path const& path,
                           entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_link(path, st, parent.id())};
}

device_handle
entry_storage::create_device(std::filesystem::path const& path,
                             entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_device(path, st, parent.id())};
}

other_handle
entry_storage::create_other(std::filesystem::path const& path,
                            entry_handle parent, file_stat const& st) {
  assert(!empty());
  return {*this, impl_->make_other(path, st, parent.id())};
}

} // namespace dwarfs::writer

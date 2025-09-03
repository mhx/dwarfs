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

#include <vector>

#include "mmap_mock.h"

namespace dwarfs::test {

class mmap_mock : public detail::file_view_impl,
                  public std::enable_shared_from_this<mmap_mock> {
 public:
  mmap_mock(std::string data)
      : mmap_mock{std::move(data), "<mock-file>"} {}

  mmap_mock(std::string data, std::filesystem::path const& path)
      : data_{std::move(data)}
      , path_{path}
      , extents_{
            {extent_kind::data, 0, static_cast<file_size_t>(data_.size())}} {}

  mmap_mock(std::string const& data, size_t size)
      : mmap_mock{data, size, "<mock-file>"} {}

  mmap_mock(std::string const& data, size_t size,
            std::filesystem::path const& path)
      : data_{data, 0, std::min(size, data.size())}
      , path_{path}
      , extents_{
            {extent_kind::data, 0, static_cast<file_size_t>(data_.size())}} {}

  file_segment segment_at(file_off_t offset, size_t size) const override;

  file_extents_iterable extents() const override {
    return file_extents_iterable(shared_from_this(), extents_);
  }

  bool supports_raw_bytes() const noexcept override { return true; }

  std::span<std::byte const> raw_bytes() const override {
    return {reinterpret_cast<std::byte const*>(data_.data()), data_.size()};
  }

  void copy_bytes(void* dest, file_off_t offset, size_t size,
                  std::error_code& ec) const override;

  size_t size() const override { return data_.size(); }

  std::filesystem::path const& path() const override { return path_; }

  std::error_code release_until(file_off_t) const override {
    return std::error_code();
  }

  void const* addr() const { return data_.data(); }

  std::error_code advise(io_advice, file_off_t, size_t) const {
    return std::error_code();
  }

  std::error_code lock(file_off_t, size_t) const { return std::error_code(); }

 private:
  std::string const data_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
};

class mmap_mock_file_segment : public detail::file_segment_impl {
 public:
  mmap_mock_file_segment(std::shared_ptr<mmap_mock const> const& mm,
                         file_off_t offset, size_t size)
      : mm_{mm}
      , offset_{offset}
      , size_{size} {}

  file_off_t offset() const noexcept override { return offset_; }
  size_t size() const noexcept override { return size_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return {static_cast<std::byte const*>(mm_->addr()) + offset_, size_};
  }

  void advise(io_advice adv, file_off_t offset, size_t size,
              std::error_code& ec) const override {
    ec = mm_->advise(adv, offset_ + offset, size);
  }

  void lock(std::error_code& ec) const override {
    ec = mm_->lock(offset_, size_);
  }

 private:
  std::shared_ptr<mmap_mock const> mm_;
  file_off_t offset_;
  size_t size_;
};

file_segment mmap_mock::segment_at(file_off_t offset, size_t size) const {
  if (offset < 0 || size == 0 || offset + size > data_.size()) {
    return {};
  }

  return file_segment(std::make_shared<mmap_mock_file_segment>(
      shared_from_this(), offset, size));
}

void mmap_mock::copy_bytes(void* dest, file_off_t offset, size_t size,
                           std::error_code& ec) const {
  if (size == 0) {
    return;
  }

  if (dest == nullptr || offset < 0) {
    ec = make_error_code(std::errc::invalid_argument);
    return;
  }

  if (offset + size > data_.size()) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  std::memcpy(dest, &data_[offset], size);
}

// TODO: clean this stuff up

file_view make_mock_file_view(std::string data) {
  return file_view{std::make_shared<mmap_mock>(std::move(data))};
}

file_view
make_mock_file_view(std::string data, std::filesystem::path const& path) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), path)};
}

file_view make_mock_file_view(std::string const& data, size_t size) {
  return file_view{std::make_shared<mmap_mock>(data, size)};
}

file_view make_mock_file_view(std::string const& data, size_t size,
                              std::filesystem::path const& path) {
  return file_view{std::make_shared<mmap_mock>(data, size, path)};
}

} // namespace dwarfs::test

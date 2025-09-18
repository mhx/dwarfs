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

#include <stdexcept>
#include <utility>
#include <vector>

#include <xxhash.h>

#include <dwarfs/binary_literals.h>

#include "mmap_mock.h"

using namespace dwarfs::binary_literals;

namespace dwarfs::test {

class mmap_mock final : public detail::file_view_impl,
                        public std::enable_shared_from_this<mmap_mock> {
 public:
  mmap_mock(std::string data, mock_file_view_options const& opts)
      : mmap_mock{std::move(data), "<mock-file>", opts} {}

  mmap_mock(std::string data, std::filesystem::path const& path,
            mock_file_view_options const& opts)
      : mmap_mock(std::move(data), path, {}, opts) {}

  mmap_mock(std::string data, std::vector<detail::file_extent_info> extents,
            mock_file_view_options const& opts)
      : mmap_mock(std::move(data), "<mock-file>", std::move(extents), opts) {}

  mmap_mock(std::string const& data, file_size_t size,
            mock_file_view_options const& opts)
      : mmap_mock{data, size, "<mock-file>", opts} {}

  mmap_mock(std::string data, std::filesystem::path const& path,
            std::vector<detail::file_extent_info> extents,
            mock_file_view_options const& opts)
      : data_{std::move(data)}
      , path_{path}
      , extents_{default_extent(std::move(extents), data_.size())}
      , opts_{opts}
      , supports_raw_bytes_{get_supports_raw_bytes(data_, opts_)} {}

  mmap_mock(std::string const& data, file_size_t size,
            std::filesystem::path const& path,
            mock_file_view_options const& opts)
      : data_{data, 0, std::min(static_cast<size_t>(size), data.size())}
      , path_{path}
      , extents_{{extent_kind::data,
                  file_range{0, static_cast<file_size_t>(data_.size())}}}
      , opts_{opts}
      , supports_raw_bytes_{get_supports_raw_bytes(data_, opts_)} {}

  file_segment segment_at(file_range range) const override;

  file_extents_iterable
  extents(std::optional<file_range> range) const override {
    if (!range.has_value()) {
      range.emplace(0, size());
    }
    return file_extents_iterable(shared_from_this(), extents_, *range);
  }

  bool supports_raw_bytes() const noexcept override {
    return supports_raw_bytes_;
  }

  std::span<std::byte const> raw_bytes() const override {
    // assert(supports_raw_bytes_);
    return {reinterpret_cast<std::byte const*>(data_.data()), data_.size()};
  }

  void
  copy_bytes(void* dest, file_range range, std::error_code& ec) const override;

  file_size_t size() const override { return data_.size(); }

  std::filesystem::path const& path() const override { return path_; }

  std::error_code release_until(file_off_t) const override {
    return std::error_code();
  }

  void const* addr() const { return data_.data(); }

  std::error_code advise(io_advice, file_range) const {
    return std::error_code();
  }

  std::error_code lock(file_range) const { return std::error_code(); }

  size_t default_segment_size() const override { return 64_KiB; }

 private:
  static std::vector<detail::file_extent_info>
  default_extent(std::vector<detail::file_extent_info> ext, file_size_t size) {
    if (ext.empty()) {
      ext.emplace_back(extent_kind::data, file_range{0, size});
    }
    return ext;
  }

  static bool get_supports_raw_bytes(std::string const& data,
                                     mock_file_view_options const& opts) {
    if (opts.support_raw_bytes.has_value()) {
      return *opts.support_raw_bytes;
    }
    auto hash = XXH3_64bits(data.data(), data.size());
    return (hash % 3) == 0;
  }

  std::string const data_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
  mock_file_view_options const opts_;
  bool const supports_raw_bytes_{false};
};

class mmap_mock_file_segment final : public detail::file_segment_impl {
 public:
  mmap_mock_file_segment(std::shared_ptr<mmap_mock const> const& mm,
                         file_range range)
      : mm_{mm}
      , range_{range} {}

  file_off_t offset() const noexcept override { return range_.offset(); }

  file_size_t size() const noexcept override { return range_.size(); }

  file_range range() const noexcept override { return range_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return {static_cast<std::byte const*>(mm_->addr()) + range_.offset(),
            static_cast<size_t>(range_.size())};
  }

  void
  advise(io_advice adv, file_range range, std::error_code& ec) const override {
    ec = mm_->advise(adv, range);
  }

  void lock(std::error_code& ec) const override { ec = mm_->lock(range_); }

 private:
  std::shared_ptr<mmap_mock const> mm_;
  file_range range_;
};

file_segment mmap_mock::segment_at(file_range range) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (offset < 0 || size == 0 ||
      std::cmp_greater(offset + size, data_.size())) {
    return {};
  }

  return file_segment(
      std::make_shared<mmap_mock_file_segment>(shared_from_this(), range));
}

void mmap_mock::copy_bytes(void* dest, file_range range,
                           std::error_code& ec) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (size == 0) {
    return;
  }

  if (dest == nullptr || offset < 0) {
    ec = make_error_code(std::errc::invalid_argument);
    return;
  }

  if (std::cmp_greater(offset + size, data_.size())) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  std::memcpy(dest, &data_[offset], size);
}

// TODO: clean this stuff up

file_view
make_mock_file_view(std::string data, mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), opts)};
}

file_view make_mock_file_view(std::string data,
                              std::vector<detail::file_extent_info> extents,
                              mock_file_view_options const& opts) {
  return file_view{
      std::make_shared<mmap_mock>(std::move(data), std::move(extents), opts)};
}

file_view
make_mock_file_view(std::string data, std::filesystem::path const& path,
                    mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), path, opts)};
}

file_view
make_mock_file_view(std::string data, std::filesystem::path const& path,
                    std::vector<detail::file_extent_info> extents,
                    mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), path,
                                               std::move(extents), opts)};
}

file_view make_mock_file_view(std::string const& data, file_size_t size,
                              mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(data, size, opts)};
}

file_view make_mock_file_view(std::string const& data, file_size_t size,
                              std::filesystem::path const& path,
                              mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(data, size, path, opts)};
}

} // namespace dwarfs::test

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
#include <iostream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <xxhash.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/match.h>

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

  mmap_mock(std::string data, std::filesystem::path const& path,
            std::vector<detail::file_extent_info> extents,
            mock_file_view_options const& opts)
      : data_{std::move(data)}
      , path_{path}
      , extents_{default_extent(std::move(extents),
                                std::get<std::string>(data_).size())}
      , opts_{opts}
      , supports_raw_bytes_{
            get_supports_raw_bytes(std::get<std::string>(data_), opts_)} {
    DWARFS_CHECK(check_extents(extents_, this->size()), "invalid extents");
  }

  mmap_mock(test_file_data data, std::filesystem::path const& path,
            mock_file_view_options const& opts)
      : data_{std::move(data)}
      , path_{path}
      , extents_{extents_from_data(std::get<test_file_data>(data_))}
      , opts_{opts}
      , supports_raw_bytes_{false} {
    DWARFS_CHECK(check_data(std::get<test_file_data>(data_)), "invalid data");
    DWARFS_CHECK(check_extents(extents_, this->size()), "invalid extents");
  }

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
    assert(supports_raw_bytes_);
    auto const& data = std::get<std::string>(data_);
    return {reinterpret_cast<std::byte const*>(data.data()), data.size()};
  }

  void
  copy_bytes(void* dest, file_range range, std::error_code& ec) const override;

  file_size_t size() const override {
    return data_ | match{[](auto const& s) -> file_size_t { return s.size(); }};
  }

  std::filesystem::path const& path() const override { return path_; }

  void release_until(file_off_t, std::error_code&) const override {}

  std::error_code advise(io_advice, file_range) const {
    return std::error_code();
  }

  std::error_code lock(file_range) const { return std::error_code(); }

  size_t default_segment_size() const override { return 64_KiB; }

 private:
  static bool
  check_extents(std::vector<detail::file_extent_info> const& extents,
                file_size_t size) {
    file_off_t pos{0};

    for (auto const& e : extents) {
      if (e.range.size() == 0) {
        std::cerr << "extent has zero size\n";
        return false;
      }

      if (e.range.offset() != pos) {
        std::cerr << "extent expected to start at " << pos << " but starts at "
                  << e.range.offset() << "\n";
        return false;
      }

      pos += e.range.size();
    }

    if (pos != size) {
      std::cerr << "extents end at " << pos << " but file size is " << size
                << "\n";
      return false;
    }

    return true;
  }

  static bool check_data(test_file_data const& data) {
    file_off_t pos{0};

    for (auto const& e : data.extents) {
      if (e.info.range.offset() != pos) {
        std::cerr << "extent expected to start at " << pos << " but starts at "
                  << e.info.range.offset() << "\n";
        return false;
      }

      if (e.info.kind == extent_kind::data &&
          std::cmp_not_equal(e.data.size(), e.info.range.size())) {
        std::cerr << "data extent has size " << e.info.range.size()
                  << " but contains " << e.data.size() << " bytes of data\n";
        return false;
      }

      if (e.info.kind == extent_kind::hole && !e.data.empty()) {
        std::cerr << "hole extent contains data\n";
        return false;
      }

      pos += e.info.range.size();
    }

    return true;
  }

  static std::vector<detail::file_extent_info>
  extents_from_data(test_file_data const& data) {
    std::vector<detail::file_extent_info> extents;
    extents.reserve(data.extents.size());
    for (auto const& e : data.extents) {
      extents.push_back(e.info);
    }
    return extents;
  }

  static std::vector<detail::file_extent_info>
  default_extent(std::vector<detail::file_extent_info> ext, file_size_t size) {
    if (ext.empty() && size > 0) {
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

  std::variant<std::string, test_file_data> const data_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
  mock_file_view_options const opts_;
  bool const supports_raw_bytes_{false};
};

class mmap_mock_file_segment final : public detail::file_segment_impl {
 public:
  mmap_mock_file_segment(std::shared_ptr<mmap_mock const> const& mm,
                         std::string data, file_range range)
      : mm_{mm}
      , data_{std::move(data)}
      , range_{range} {}

  file_off_t offset() const noexcept override { return range_.offset(); }

  file_size_t size() const noexcept override { return range_.size(); }

  file_range range() const noexcept override { return range_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return {reinterpret_cast<std::byte const*>(data_.data()), data_.size()};
  }

  void advise(io_advice adv, std::error_code& ec) const override {
    ec = mm_->advise(adv, range_);
  }

  void lock(std::error_code& ec) const override { ec = mm_->lock(range_); }

 private:
  std::shared_ptr<mmap_mock const> mm_;
  std::string data_;
  file_range range_;
};

file_segment mmap_mock::segment_at(file_range range) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (offset < 0 || size == 0 ||
      std::cmp_greater(offset + size, this->size())) {
    return {};
  }

  std::string segment_data;
  segment_data.resize(size);
  std::error_code ec;
  copy_bytes(segment_data.data(), range, ec);

  if (ec) {
    std::abort();
  }

  return file_segment(std::make_shared<mmap_mock_file_segment>(
      shared_from_this(), std::move(segment_data), range));
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

  if (std::cmp_greater(offset + size, this->size())) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  data_ | match{
              [&](test_file_data const& data) {
                // find first extent that contains offset
                auto it = std::lower_bound(
                    data.extents.begin(), data.extents.end(), offset,
                    [](test_file_extent const& e, file_off_t off) {
                      return e.info.range.end() <= off;
                    });

                file_off_t pos = offset;
                file_size_t remaining = size;
                auto outp = static_cast<std::byte*>(dest);

                while (remaining > 0 && it != data.extents.end()) {
                  auto const to_copy =
                      std::min(remaining, it->info.range.end() - pos);

                  if (it->info.kind == extent_kind::hole) {
                    std::memset(outp, 0, to_copy);
                  } else {
                    auto const data_offset = pos - it->info.range.offset();
                    std::memcpy(outp, it->data.data() + data_offset, to_copy);
                  }

                  outp += to_copy;
                  pos += to_copy;
                  remaining -= to_copy;
                  ++it;
                }

                assert(remaining == 0);
              },
              [&](std::string const& data) {
                std::memcpy(dest, &data[offset], size);
              },
          };
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
make_mock_file_view(test_file_data data, std::filesystem::path const& path,
                    mock_file_view_options const& opts) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), path, opts)};
}

file_view
make_mock_file_view(test_file_data data, mock_file_view_options const& opts) {
  return file_view{
      std::make_shared<mmap_mock>(std::move(data), "<mock-file>", opts)};
}

} // namespace dwarfs::test

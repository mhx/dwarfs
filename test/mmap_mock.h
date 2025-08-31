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

#include <dwarfs/file_view.h>

namespace dwarfs {
namespace test {

class mmap_mock : public file_view::impl {
 public:
  mmap_mock(std::string data)
      : mmap_mock{std::move(data), "<mock-file>"} {}

  mmap_mock(std::string data, std::filesystem::path const& path)
      : data_{std::move(data)}
      , path_{path} {}

  mmap_mock(std::string const& data, size_t size)
      : mmap_mock{data, size, "<mock-file>"} {}

  mmap_mock(std::string const& data, size_t size,
            std::filesystem::path const& path)
      : data_{data, 0, std::min(size, data.size())}
      , path_{path} {}

  void const* addr() const override { return data_.data(); }

  size_t size() const override { return data_.size(); }

  std::filesystem::path const& path() const override { return path_; }

  std::error_code lock(file_off_t, size_t) const override {
    return std::error_code();
  }
  std::error_code release(file_off_t, size_t) const override {
    return std::error_code();
  }
  std::error_code release_until(file_off_t) const override {
    return std::error_code();
  }

  std::error_code advise(advice) const override { return std::error_code(); }
  std::error_code advise(advice, file_off_t, size_t) const override {
    return std::error_code();
  }

 private:
  std::string const data_;
  std::filesystem::path const path_;
};

// TODO: clean this stuff up

inline file_view make_mock_file_view(std::string data) {
  return file_view{std::make_shared<mmap_mock>(std::move(data))};
}

inline file_view
make_mock_file_view(std::string data, std::filesystem::path const& path) {
  return file_view{std::make_shared<mmap_mock>(std::move(data), path)};
}

inline file_view make_mock_file_view(std::string const& data, size_t size) {
  return file_view{std::make_shared<mmap_mock>(data, size)};
}

inline file_view make_mock_file_view(std::string const& data, size_t size,
                                     std::filesystem::path const& path) {
  return file_view{std::make_shared<mmap_mock>(data, size, path)};
}

} // namespace test
} // namespace dwarfs

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
 */

#include "dwarfs/mmif.h"

namespace dwarfs {
namespace test {

class mmap_mock : public mmif {
 public:
  mmap_mock(const std::string& data)
      : mmap_mock{data, "<mock-file>"} {}

  mmap_mock(const std::string& data, std::filesystem::path const& path)
      : data_{data}
      , path_{path} {}

  mmap_mock(const std::string& data, size_t size)
      : mmap_mock{data, size, "<mock-file>"} {}

  mmap_mock(const std::string& data, size_t size,
            std::filesystem::path const& path)
      : data_{data, 0, std::min(size, data.size())}
      , path_{path} {}

  void const* addr() const override { return data_.data(); }

  size_t size() const override { return data_.size(); }

  std::filesystem::path const& path() const override { return path_; }

  std::error_code lock(file_off_t, size_t) override {
    return std::error_code();
  }
  std::error_code release(file_off_t, size_t) override {
    return std::error_code();
  }
  std::error_code release_until(file_off_t) override {
    return std::error_code();
  }

 private:
  std::string const data_;
  std::filesystem::path const path_;
};

} // namespace test
} // namespace dwarfs

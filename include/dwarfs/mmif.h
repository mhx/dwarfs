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

#pragma once

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>

#include <boost/noncopyable.hpp>

#include <dwarfs/types.h>

namespace dwarfs {

enum class advice {
  normal,
  random,
  sequential,
  willneed,
  dontneed,
};

class mmif : public boost::noncopyable {
 public:
  virtual ~mmif() = default;

  template <typename T, std::integral U>
  T const* as(U offset = 0) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto raw = static_cast<std::byte const*>(this->addr()) + offset;
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    return static_cast<T const*>(static_cast<void const*>(raw));
  }

  template <typename T = uint8_t, std::integral U>
  std::span<T const> span(U offset, size_t length) const {
    return std::span(this->as<T>(offset), length);
  }

  template <typename T = uint8_t, std::integral U>
  std::span<T const> span(U offset) const {
    return span<T>(offset, size() - offset);
  }

  template <typename T = uint8_t>
  std::span<T const> span() const {
    return span<T>(0);
  }

  virtual void const* addr() const = 0;
  virtual size_t size() const = 0;

  virtual std::error_code lock(file_off_t offset, size_t size) = 0;
  virtual std::error_code release(file_off_t offset, size_t size) = 0;
  virtual std::error_code release_until(file_off_t offset) = 0;

  virtual std::error_code advise(advice adv) = 0;
  virtual std::error_code
  advise(advice adv, file_off_t offset, size_t size) = 0;

  virtual std::filesystem::path const& path() const = 0;
};

} // namespace dwarfs

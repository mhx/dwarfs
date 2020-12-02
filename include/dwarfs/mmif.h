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

#include <string>

#include <boost/noncopyable.hpp>
#include <boost/system/system_error.hpp>

#include <folly/Range.h>

namespace dwarfs {

class mmif : public boost::noncopyable {
 public:
  virtual ~mmif() = default;

  template <typename T>
  T const* as(off_t offset = 0) const {
    return reinterpret_cast<T const*>(
        reinterpret_cast<char const*>(this->addr()) + offset);
  }

  folly::ByteRange range(off_t offset, size_t length) const {
    return folly::ByteRange(this->as<uint8_t>(offset), length);
  }

  virtual void const* addr() const = 0;
  virtual size_t size() const = 0;

  virtual boost::system::error_code lock(off_t offset, size_t size) = 0;
  virtual boost::system::error_code release(off_t offset, size_t size) = 0;
};
} // namespace dwarfs

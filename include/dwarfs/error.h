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

#include <exception>

namespace dwarfs {

class error : public std::exception {
 public:
  error(const std::string& str, int err_no) noexcept
      : what_(str)
      , errno_(err_no) {}

  error(const error& e) noexcept
      : what_(e.what_)
      , errno_(e.errno_) {}

  error& operator=(const error& e) noexcept {
    if (&e != this) {
      what_ = e.what_;
      errno_ = e.errno_;
    }
    return *this;
  }

  const char* what() const noexcept override { return what_.c_str(); }

  int get_errno() const { return errno_; }

 private:
  std::string what_;
  int errno_;
};

} // namespace dwarfs

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

#include <memory>

namespace dwarfs {

template <typename T, typename E = std::exception_ptr>
class receiver {
 public:
  using value_type = T;
  using error_type = E;

  class impl;

  receiver(std::unique_ptr<impl> i)
      : impl_{std::move(i)} {}

  void set_value(value_type value) { impl_->set_value(std::move(value)); }
  void set_error(error_type error) { impl_->set_error(std::move(error)); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_value(value_type value) = 0;
    virtual void set_error(error_type error) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

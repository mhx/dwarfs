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

#include <future>

#include "dwarfs/receiver.h"

namespace dwarfs {

template <typename T>
class promise_receiver : public receiver<T>::impl {
 public:
  promise_receiver(std::promise<T>&& p)
      : p_{std::move(p)} {}

  static receiver<T> create(std::promise<T>&& p) {
    return receiver<T>(std::make_unique<promise_receiver>(std::move(p)));
  }

 private:
  void set_value(T value) override { p_.set_value(std::move(value)); }
  void set_error(std::exception_ptr error) override {
    p_.set_exception(std::move(error));
  }

  std::promise<T> p_;
};

template <typename T>
receiver<T> make_receiver(std::promise<T>&& p) {
  return promise_receiver<T>::create(std::move(p));
}

} // namespace dwarfs

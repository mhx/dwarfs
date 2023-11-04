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

#include <functional>
#include <variant>

namespace dwarfs {

template <typename T>
class lazy_value {
 public:
  using function_type = std::function<T()>;

  lazy_value(function_type f)
      : v_{f} {}

  T const& get() {
    if (std::holds_alternative<function_type>(v_))
#if __has_cpp_attribute(unlikely)
    [[unlikely]]
#endif
    {
      v_ = std::get<function_type>(v_)();
    }
    return std::get<T>(v_);
  }

  T const& operator()() { return get(); }

 private:
  std::variant<function_type, T> v_;
};

} // namespace dwarfs

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

#include "dwarfs/contextual_option.h"
#include "dwarfs/fragment_category.h"

namespace dwarfs {

namespace detail {

template <typename T>
struct categorized_option_policy {
  using ContextArgumentType = fragment_category;
  using ContextType = fragment_category::value_type;
  using ValueType = T;

  static ContextType context_from_arg(ContextArgumentType const& arg) {
    return arg.value();
  }
};

} // namespace detail

template <typename ValueType>
using categorized_option =
    contextual_option<detail::categorized_option_policy<ValueType>>;

} // namespace dwarfs

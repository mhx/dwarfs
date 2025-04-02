/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <concepts>
#include <iterator>
#include <type_traits>
#include <utility>

#include <range/v3/algorithm/copy.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/split.hpp>

#include <dwarfs/conv.h>

namespace dwarfs {

template <typename T, typename Input, typename Delim>
auto split_view(Input&& input, Delim&& delim)
  requires(std::same_as<T, std::string> || std::same_as<T, std::string_view>)
{
  return std::forward<Input>(input) |
         ranges::views::split(std::forward<Delim>(delim)) |
         ranges::views::transform([](auto&& rng) {
           return T(&*rng.begin(), ranges::distance(rng));
         });
}

template <typename T, typename Input, typename Delim>
auto split_view(Input&& input, Delim&& delim)
  requires std::is_arithmetic_v<T>
{
  return std::forward<Input>(input) |
         ranges::views::split(std::forward<Delim>(delim)) |
         ranges::views::transform([](auto&& rng) {
           return to<T>(std::string_view(&*rng.begin(), ranges::distance(rng)));
         });
}

template <typename T, typename Delim>
auto split_view(char const* input, Delim&& delim) {
  return split_view<T>(std::string_view(input), std::forward<Delim>(delim));
}

template <typename R, typename Input, typename Delim>
R split_to(Input&& input, Delim&& delim) {
  return split_view<typename R::value_type>(std::forward<Input>(input),
                                            std::forward<Delim>(delim)) |
         ranges::to<R>;
}

template <typename R, typename Delim>
R split_to(char const* input, Delim&& delim) {
  return split_to<R>(std::string_view(input), std::forward<Delim>(delim));
}

template <typename Input, typename Delim, typename Container>
void split_to(Input&& input, Delim&& delim, Container& container) {
  ranges::copy(split_view<typename Container::value_type>(
                   std::forward<Input>(input), std::forward<Delim>(delim)),
               std::inserter(container, container.end()));
}

template <typename Delim, typename Container>
void split_to(char const* input, Delim&& delim, Container& container) {
  split_to(std::string_view(input), std::forward<Delim>(delim), container);
}

} // namespace dwarfs

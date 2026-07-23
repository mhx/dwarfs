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

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dwarfs {

class logger;

namespace detail {

template <class T>
struct unique_ptr_policy {
  using return_type = std::unique_ptr<T>;

  template <class U, class... Args>
  static return_type create(Args&&... args) {
    return std::make_unique<U>(std::forward<Args>(args)...);
  }
};

template <class T>
struct shared_ptr_policy {
  using return_type = std::shared_ptr<T>;

  template <class U, class... Args>
  static return_type create(Args&&... args) {
    return std::make_shared<U>(std::forward<Args>(args)...);
  }
};

/**
 * Index of the first policy whose min_log_level covers the logger's current
 * threshold. Panics if no policy covers it; callers are expected to have
 * validated the threshold against max_supported_log_level beforehand.
 */
size_t select_logger_policy(logger const& lgr,
                            std::span<unsigned const> min_log_levels);

/**
 * A policy list is well-formed if it is non-empty and its min_log_level values
 * are strictly ascending. Strictness matters: two policies with the same level
 * would make the second one unreachable.
 */
template <typename... Policies>
consteval bool policies_are_ordered() {
  if constexpr (sizeof...(Policies) == 0) {
    return false;
  } else {
    constexpr std::array<unsigned, sizeof...(Policies)> levels{
        Policies::min_log_level...};
    return std::ranges::is_sorted(levels) &&
           std::ranges::adjacent_find(levels) == levels.end();
  }
}

class logging_class_factory {
 public:
  template <template <class> class T, class CreatePolicy,
            class LoggerPolicyList, class... Args>
  static CreatePolicy::return_type create(logger& lgr, Args&&... args) {
    return create_unwrap<T, CreatePolicy>(
        lgr, std::type_identity<LoggerPolicyList>{},
        std::forward<Args>(args)...);
  }

 private:
  template <template <class> class T, class CreatePolicy,
            class... LoggerPolicies, class... Args>
  static CreatePolicy::return_type
  create_unwrap(logger& lgr, std::type_identity<std::tuple<LoggerPolicies...>>,
                Args&&... args) {
    static_assert(sizeof...(LoggerPolicies) > 0,
                  "logger policy list must not be empty");
    static_assert(policies_are_ordered<LoggerPolicies...>(),
                  "logger policies must be sorted by strictly ascending "
                  "min_log_level");

    using return_type = typename CreatePolicy::return_type;
    using factory_type = return_type (*)(logger&, Args&&...);

    static constexpr std::array<unsigned, sizeof...(LoggerPolicies)> levels{
        LoggerPolicies::min_log_level...};

    static constexpr std::array<factory_type, sizeof...(LoggerPolicies)>
        factories{+[](logger& l, Args&&... a) -> return_type {
          return CreatePolicy::template create<T<LoggerPolicies>>(
              l, std::forward<Args>(a)...);
        }...};

    return factories[select_logger_policy(lgr, levels)](
        lgr, std::forward<Args>(args)...);
  }
};

} // namespace detail
} // namespace dwarfs

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

#include <memory>
#include <tuple>
#include <type_traits>

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

class logging_class_factory {
 public:
  template <template <class> class T, class CreatePolicy,
            class LoggerPolicyList, class... Args>
  static typename CreatePolicy::return_type
  create(logger& lgr, Args&&... args) {
    return create_unwrap<T, CreatePolicy>(
        lgr, std::type_identity<LoggerPolicyList>{},
        std::forward<Args>(args)...);
  }

 private:
  template <template <class> class T, class CreatePolicy,
            class... LoggerPolicies, class... Args>
  static typename CreatePolicy::return_type
  create_unwrap(logger& lgr, std::type_identity<std::tuple<LoggerPolicies...>>,
                Args&&... args) {
    return create_impl<T, CreatePolicy, LoggerPolicies...>(
        lgr, std::forward<Args>(args)...);
  }

  template <template <class> class T, class CreatePolicy, class LoggerPolicy,
            class... Rest, class... Args>
  static typename CreatePolicy::return_type
  create_impl(logger& lgr, Args&&... args) {
    if (is_policy_name(lgr, LoggerPolicy::name())) {
      return CreatePolicy::template create<T<LoggerPolicy>>(
          lgr, std::forward<Args>(args)...);
    }
    if constexpr (sizeof...(Rest) > 0) {
      return create_impl<T, CreatePolicy, Rest...>(lgr,
                                                   std::forward<Args>(args)...);
    }
    on_policy_not_found(lgr);
  }

  static bool is_policy_name(logger const& lgr, std::string_view name);
  [[noreturn]] static void on_policy_not_found(logger const& lgr);
};

} // namespace detail
} // namespace dwarfs

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
    } else if constexpr (sizeof...(Rest) > 0) {
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

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

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <dwarfs/conv.h>
#include <dwarfs/detail/string_like_hash.h>

namespace dwarfs {

class option_map {
 public:
  explicit option_map(std::string_view spec);

  std::string const& choice() const { return choice_; }

  bool has_options() const { return !opt_.empty(); }

  // om.get<T>(key)                    -> value *must* be present
  //
  //   ""          -> error: mandatory option/value combo
  //   "key"       -> error (no value)
  //   "key=value" -> value
  //
  // om.get<T>(key, default)           -> value or default if not present
  //
  //   ""          -> default
  //   "key"       -> error (no value)
  //   "key=value" -> value
  //
  // om.get<bool>(key)                 -> true if present, false if not present;
  //                                      must not have a value
  //
  //   ""          -> false
  //   "key"       -> true
  //   "key=value" -> error (value not allowed)
  //
  // om.get_optional<T>(key)           -> either not present (-> std::nullopt)
  //                                      or present with mandatory value
  //
  //   ""          -> std::nullopt
  //   "key"       -> error (value required)
  //   "key=value" -> value
  //
  // om.get_optional<T>(key, default)  -> either not present (-> std::nullopt),
  //                                      present without value (-> default) or
  //                                      present with value (-> value)
  //
  //   ""          -> std::nullopt
  //   "key"       -> default
  //   "key=value" -> value
  //
  // om.get_size(key, default)         -> value as size_t or default if not
  //                                      present
  //
  //   ""          -> default
  //   "key"       -> error (no value)
  //   "key=value" -> value

  template <typename T>
  T get(std::string_view key) {
    auto const tv = take(key);
    T result;

    if constexpr (std::is_same_v<T, bool>) {
      if (tv && tv->has_value()) {
        throw_value_not_allowed(key);
      }

      result = tv.has_value();
    } else {
      if (!tv || !tv->has_value()) {
        throw_missing_value(key);
      }

      result = to<T>(**tv);
    }

    return result;
  }

  template <typename T>
    requires(!std::is_same_v<T, bool>)
  T get(std::string_view key, T const& default_value) {
    auto const tv = take(key);

    if (!tv) {
      return default_value;
    }

    if (!tv->has_value()) {
      throw_missing_value(key);
    }

    return to<T>(**tv);
  }

  template <typename T>
    requires(!std::is_same_v<T, bool>)
  std::optional<T> get_optional(std::string_view key) {
    auto const tv = take(key);

    if (!tv) {
      return std::nullopt;
    }

    if (!tv->has_value()) {
      throw_missing_value(key);
    }

    return to<T>(**tv);
  }

  template <typename T>
    requires(!std::is_same_v<T, bool>)
  std::optional<T> get_optional(std::string_view key, T const& default_value) {
    auto const tv = take(key);

    if (!tv) {
      return std::nullopt;
    }

    if (!tv->has_value()) {
      return default_value;
    }

    return to<T>(**tv);
  }

  size_t get_size(std::string_view key, size_t default_value);

  void report();

 private:
  std::optional<std::optional<std::string>> take(std::string_view key) {
    auto it = opt_.find(key);

    check_consumed(key);

    if (it != opt_.end()) {
      auto val = it->second;
      opt_.erase(it);
      return val;
    }

    return std::nullopt;
  }

  [[noreturn]] void throw_missing_value(std::string_view key);
  [[noreturn]] void throw_value_not_allowed(std::string_view key);
  void check_consumed(std::string_view key);

  std::unordered_map<std::string, std::optional<std::string>,
                     detail::string_like_hash, std::equal_to<>>
      opt_;
  std::unordered_set<std::string, detail::string_like_hash, std::equal_to<>>
      consumed_keys_;
  std::string choice_;
};

} // namespace dwarfs

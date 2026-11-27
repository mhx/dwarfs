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
#include <deque>
#include <functional>
#include <iosfwd>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/conv.h>
#include <dwarfs/sys_string.h>

namespace dwarfs::cli {

class error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

namespace detail {

inline bool assign(std::string& value, std::string_view text) {
  value = text;
  return true;
}

#ifdef _WIN32
inline bool assign(std::wstring& value, std::string_view text) {
  value = string_to_sys_string(std::string(text));
  return true;
}
#endif

template <typename T>
  requires std::is_arithmetic_v<T>
bool assign(T& value, std::string_view text) {
  if (auto parsed = try_to<T>(text)) {
    value = *parsed;
    return true;
  }
  return false;
}

template <typename T>
bool assign(std::optional<T>& value, std::string_view text) {
  T parsed{};
  if (!assign(parsed, text)) {
    return false;
  }
  value = std::move(parsed);
  return true;
}

inline std::string display(std::string const& value) { return value; }
#ifdef _WIN32
inline std::string display(std::wstring const& value) {
  return sys_string_to_string(value);
}
#endif

template <typename T>
  requires std::is_arithmetic_v<T>
std::string display(T value) {
  return std::to_string(value);
}

} // namespace detail

class options;

class option {
 public:
  option& hidden() {
    hidden_ = true;
    return *this;
  }
  option& value_name(std::string_view name);
  option& default_text(std::string_view text);
  option& implicit_value(std::string_view text);

 private:
  friend class options;
  enum class kind { flag, single, repeated, multi };
  using setter = std::function<bool(std::string_view)>;

  option(std::string_view names, std::string_view description,
         std::string_view group, kind type, setter set);
  std::string label() const;
  bool repeatable() const {
    return type_ == kind::repeated || type_ == kind::multi;
  }

  std::string name_;
  char alias_{};
  std::string description_;
  std::string group_;
  kind type_;
  setter set_;
  std::string value_name_{"arg"};
  std::optional<std::string> default_text_;
  std::optional<std::string> implicit_;
  bool seen_{false};
  bool hidden_{false};
};

class options {
 public:
  explicit options(std::string_view caption = "Command line options")
      : group_(caption) {}

  options(options const&) = delete;
  options& operator=(options const&) = delete;

  void group(std::string_view caption) { group_ = caption; }

  template <typename T>
  option& add(std::string_view names, T& value, std::string_view description) {
    return add_value(names, description, [&value](std::string_view text) {
      return detail::assign(value, text);
    });
  }

  template <typename T>
  option& add(std::string_view names, T& value, std::string_view description,
              std::type_identity_t<T> default_value) {
    value = std::move(default_value);
    return add(names, value, description).default_text(detail::display(value));
  }

  option& add_value(std::string_view names, std::string_view description,
                    std::function<bool(std::string_view)> set);

  template <typename T>
    requires(std::same_as<T, bool> || std::same_as<T, std::optional<bool>>)
  option& flag(std::string_view names, T& value, std::string_view description) {
    return insert(names, description, option::kind::flag,
                  [&value](std::string_view) {
                    value = true;
                    return true;
                  });
  }

  template <typename T>
  option& add_repeated(std::string_view names, std::vector<T>& values,
                       std::string_view description) {
    return add_vector(names, values, description, option::kind::repeated);
  }

  template <typename T>
  option& add_multi(std::string_view names, std::vector<T>& values,
                    std::string_view description) {
    return add_vector(names, values, description, option::kind::multi);
  }

  void positional(std::string_view name);
  bool contains(std::string_view name) const;

  void parse(int argc, sys_char const* const* argv);
  void parse(std::span<std::string_view const> args);
  void help(std::ostream& out, size_t width = 80) const;
  void help(std::ostream& out, std::string_view group, size_t width = 80) const;

 private:
  template <typename T>
  option& add_vector(std::string_view names, std::vector<T>& values,
                     std::string_view description, option::kind type) {
    return insert(names, description, type, [&values](std::string_view text) {
      T value{};
      if (!detail::assign(value, text)) {
        return false;
      }
      values.push_back(std::move(value));
      return true;
    });
  }

  option& insert(std::string_view names, std::string_view description,
                 option::kind type, option::setter set);
  option* find_long(std::string_view name);
  option* find_short(char alias);
  option* find_exact(std::string_view name);

  void print_help(std::ostream& out, size_t width,
                  std::optional<std::string_view> group) const;

  std::deque<option> options_;
  std::string group_;
  option* positional_{};
  bool parsed_{false};
};

} // namespace dwarfs::cli

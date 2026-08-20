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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dwarfs::test {

template <template <typename> typename Policy>
struct policy_wrapper {
  template <typename T>
  using policy = Policy<T>;
};

struct throwing_index_set_control {
  static inline bool fail_next_insert = false;

  static void reset() noexcept { fail_next_insert = false; }
};

template <typename Hash, typename Equal>
class throwing_index_set {
 public:
  using underlying_type = std::unordered_set<std::size_t, Hash, Equal>;
  using value_type = typename underlying_type::value_type;
  using iterator = typename underlying_type::iterator;
  using const_iterator = typename underlying_type::const_iterator;

  throwing_index_set(std::size_t bucket_count, Hash hash, Equal equal)
      : set_(bucket_count, std::move(hash), std::move(equal)) {}

  std::pair<iterator, bool> insert(value_type value) {
    if (throwing_index_set_control::fail_next_insert) {
      throwing_index_set_control::fail_next_insert = false;
      throw std::runtime_error("injected insert failure");
    }
    return set_.insert(value);
  }

  void clear() { set_.clear(); }

  void reserve(std::size_t n) { set_.reserve(n); }

  iterator end() noexcept { return set_.end(); }
  const_iterator end() const noexcept { return set_.end(); }

  template <typename K>
  iterator find(K const& key) {
    return set_.find(key);
  }

  template <typename K>
  const_iterator find(K const& key) const {
    return set_.find(key);
  }

 private:
  underlying_type set_;
};

template <class T>
class append_only_store {
 public:
  using value_type = T;
  using underlying_type = std::vector<T>;
  using reference = typename underlying_type::reference;
  using const_reference = typename underlying_type::const_reference;

  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

  template <class... Args>
  decltype(auto) emplace_back(Args&&... args) {
    return values_.emplace_back(std::forward<Args>(args)...);
  }

  [[nodiscard]] reference operator[](std::size_t index) noexcept {
    return values_[index];
  }

  [[nodiscard]] const_reference operator[](std::size_t index) const noexcept {
    return values_[index];
  }

  [[nodiscard]] reference at(std::size_t index) { return values_.at(index); }

  [[nodiscard]] const_reference at(std::size_t index) const {
    return values_.at(index);
  }

  [[nodiscard]] underlying_type const& values() const noexcept {
    return values_;
  }

 private:
  underlying_type values_;
};

template <class Stored, class View = Stored>
class proxy_store {
 public:
  using value_type = View;
  using reference = View;
  using const_reference = View;

  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

  template <class... Args>
  void emplace_back(Args&&... args) {
    values_.emplace_back(std::forward<Args>(args)...);
  }

  void pop_back() { values_.pop_back(); }

  [[nodiscard]] const_reference operator[](std::size_t index) const noexcept {
    return values_[index];
  }

  [[nodiscard]] const_reference at(std::size_t index) const {
    return values_.at(index);
  }

 private:
  std::deque<Stored> values_;
};

struct throwing_value {
  static inline bool fail_construction = false;

  static void reset() noexcept { fail_construction = false; }

  int value;

  explicit throwing_value(int v)
      : value(v) {
    if (fail_construction) {
      throw std::runtime_error("injected construction failure");
    }
  }
};

struct throwing_value_hash {
  using is_transparent = void;

  std::size_t operator()(throwing_value const& v) const noexcept {
    return std::hash<int>{}(v.value);
  }

  std::size_t operator()(int v) const noexcept { return std::hash<int>{}(v); }
};

struct throwing_value_equal {
  using is_transparent = void;

  bool operator()(throwing_value const& lhs,
                  throwing_value const& rhs) const noexcept {
    return lhs.value == rhs.value;
  }

  bool operator()(throwing_value const& lhs, int rhs) const noexcept {
    return lhs.value == rhs;
  }

  bool operator()(int lhs, throwing_value const& rhs) const noexcept {
    return lhs == rhs.value;
  }
};

struct counted_value {
  static inline int constructions = 0;

  static void reset() noexcept { constructions = 0; }

  int value{};

  counted_value() { ++constructions; }

  explicit counted_value(int v)
      : value{v} {
    ++constructions;
  }

  counted_value(counted_value const& other)
      : value{other.value} {
    ++constructions;
  }

  counted_value(counted_value&& other) noexcept
      : value{other.value} {
    ++constructions;
  }

  counted_value& operator=(counted_value const&) = default;
  counted_value& operator=(counted_value&&) = default;

  friend bool operator==(counted_value const&, counted_value const&) = default;
};

struct counted_string {
  std::string value;

  counted_string() = default;

  counted_string(char const* s)
      : value(s) {}

  counted_string(std::string s)
      : value(std::move(s)) {}

  friend bool
  operator==(counted_string const&, counted_string const&) = default;
};

struct ascii_case_hash {
  using is_transparent = void;

  std::size_t operator()(std::string_view sv) const noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : sv) {
      auto const lower = static_cast<unsigned char>(std::tolower(c));
      h ^= lower;
      h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
  }

  std::size_t operator()(std::string const& s) const noexcept {
    return (*this)(std::string_view{s});
  }
};

struct ascii_case_equal {
  using is_transparent = void;

  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
      auto const a = static_cast<unsigned char>(lhs[i]);
      auto const b = static_cast<unsigned char>(rhs[i]);
      if (std::tolower(a) != std::tolower(b)) {
        return false;
      }
    }

    return true;
  }

  bool
  operator()(std::string const& lhs, std::string const& rhs) const noexcept {
    return (*this)(std::string_view{lhs}, std::string_view{rhs});
  }

  bool operator()(std::string const& lhs, std::string_view rhs) const noexcept {
    return (*this)(std::string_view{lhs}, rhs);
  }

  bool operator()(std::string_view lhs, std::string const& rhs) const noexcept {
    return (*this)(lhs, std::string_view{rhs});
  }
};

} // namespace dwarfs::test

template <>
struct std::hash<dwarfs::test::counted_string> {
  std::size_t operator()(dwarfs::test::counted_string const& v) const noexcept {
    return std::hash<std::string_view>{}(v.value);
  }
};

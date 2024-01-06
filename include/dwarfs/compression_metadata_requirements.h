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

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <fmt/format.h>

#include <folly/dynamic.h>

namespace dwarfs {

namespace detail {

template <typename T>
std::vector<T> ordered_set(std::unordered_set<T> const& set) {
  std::vector<T> vec;
  vec.reserve(set.size());
  std::copy(set.begin(), set.end(), std::back_inserter(vec));
  std::sort(vec.begin(), vec.end());
  return vec;
}

template <typename T>
std::optional<T> value_parser(folly::dynamic const& v) {
  if constexpr (std::is_same_v<T, std::string>) {
    return v.asString();
  } else {
    static_assert(std::is_integral_v<T>);
    return v.asInt();
  }
}

void check_dynamic_common(folly::dynamic const& dyn,
                          std::string_view expected_type, size_t expected_size,
                          std::string_view name);

void check_unsupported_metadata_requirements(folly::dynamic& req);

template <typename T, typename ValueParser>
bool parse_metadata_requirements_set(T& container, folly::dynamic& req,
                                     std::string_view name,
                                     ValueParser const& value_parser) {
  if (auto it = req.find(name); it != req.items().end()) {
    detail::check_dynamic_common(it->second, "set", 2, name);

    if (it->second[1].type() != folly::dynamic::ARRAY) {
      throw std::runtime_error(
          fmt::format("non-array type argument for requirement '{}', got '{}'",
                      name, it->second[1].typeName()));
    }

    if (it->second[1].empty()) {
      throw std::runtime_error(
          fmt::format("unexpected empty set for requirement '{}'", name));
    }

    for (auto v : it->second[1]) {
      std::optional<typename T::value_type> maybe_value;

      try {
        maybe_value = value_parser(v);
      } catch (std::exception const& e) {
        throw std::runtime_error(fmt::format(
            "could not parse set value '{}' for requirement '{}': {}",
            v.asString(), name, e.what()));
      }

      if (auto maybe_value = value_parser(v)) {
        if (!container.emplace(*maybe_value).second) {
          throw std::runtime_error(fmt::format(
              "duplicate value '{}' for requirement '{}'", v.asString(), name));
        }
      }
    }

    if (container.empty()) {
      throw std::runtime_error(
          fmt::format("no supported values for requirement '{}'", name));
    }

    req.erase(it);

    return true;
  }

  return false;
}

template <typename T, typename ValueParser>
bool parse_metadata_requirements_range(T& min, T& max, folly::dynamic& req,
                                       std::string_view name,
                                       ValueParser const& value_parser) {
  if (auto it = req.find(name); it != req.items().end()) {
    detail::check_dynamic_common(it->second, "range", 3, name);

    auto get_value = [&](std::string_view what, int index) {
      try {
        if (auto maybe_value = value_parser(it->second[index])) {
          return *maybe_value;
        }
      } catch (std::exception const& e) {
        throw std::runtime_error(fmt::format(
            "could not parse {} value '{}' for requirement '{}': {}", what,
            it->second[index].asString(), name, e.what()));
      }
      throw std::runtime_error(
          fmt::format("could not parse {} value '{}' for requirement '{}'",
                      what, it->second[index].asString(), name));
    };

    min = get_value("minimum", 1);
    max = get_value("maximum", 2);

    if (min > max) {
      throw std::runtime_error(fmt::format(
          "expected minimum '{}' to be less than or equal "
          "to maximum '{}' for requirement '{}'",
          it->second[1].asString(), it->second[2].asString(), name));
    }

    req.erase(it);

    return true;
  }

  return false;
}

class metadata_requirement_base {
 public:
  virtual ~metadata_requirement_base() = default;

  metadata_requirement_base(std::string const& name)
      : name_{name} {}

  virtual void parse(folly::dynamic& req) = 0;

  std::string_view name() const { return name_; }

 private:
  std::string const name_;
};

template <typename Meta>
class checked_metadata_requirement_base : public metadata_requirement_base {
 public:
  using metadata_requirement_base::metadata_requirement_base;

  virtual void check(Meta const& m) const = 0;
};

class dynamic_metadata_requirement_base {
 public:
  virtual ~dynamic_metadata_requirement_base() = default;

  dynamic_metadata_requirement_base(std::string const& name)
      : name_{name} {}

  virtual void check(folly::dynamic const& m) const = 0;

  std::string_view name() const { return name_; }

 private:
  std::string const name_;
};

template <typename Meta, typename T, typename U>
class typed_metadata_requirement_base
    : public checked_metadata_requirement_base<Meta> {
 public:
  using value_parser_type =
      std::function<std::optional<T>(folly::dynamic const& v)>;
  using member_ptr_type = U(Meta::*);

  typed_metadata_requirement_base(std::string const& name, member_ptr_type mp)
      : checked_metadata_requirement_base<Meta>(name)
      , mp_{mp}
      , value_parser_{detail::value_parser<T>} {}

  typed_metadata_requirement_base(std::string const& name, member_ptr_type mp,
                                  value_parser_type value_parser)
      : checked_metadata_requirement_base<Meta>(name)
      , mp_{mp}
      , value_parser_{value_parser} {}

  void check(Meta const& m) const override { check_value(m.*mp_); }

  value_parser_type const& value_parser() const { return value_parser_; }

 protected:
  virtual void check_value(T const& value) const = 0;

 private:
  member_ptr_type mp_;
  value_parser_type value_parser_;
};

template <typename Meta, typename T, typename U = T>
class metadata_requirement_set
    : public typed_metadata_requirement_base<Meta, T, U> {
 public:
  using typed_metadata_requirement_base<Meta, T,
                                        U>::typed_metadata_requirement_base;

  void parse(folly::dynamic& req) override {
    set_.reset();
    std::unordered_set<T> tmp;
    if (parse_metadata_requirements_set(tmp, req, this->name(),
                                        this->value_parser())) {
      set_.emplace(std::move(tmp));
    }
  }

 protected:
  void check_value(T const& value) const override {
    if (set_ && set_->count(value) == 0) {
      throw std::range_error(
          fmt::format("{} '{}' does not meet requirements [{}]", this->name(),
                      value, fmt::join(ordered_set(*set_), ", ")));
    }
  }

 private:
  std::optional<std::unordered_set<T>> set_;
};

template <typename Meta, typename T, typename U = T>
class metadata_requirement_range
    : public typed_metadata_requirement_base<Meta, T, U> {
 public:
  using typed_metadata_requirement_base<Meta, T,
                                        U>::typed_metadata_requirement_base;

  void parse(folly::dynamic& req) override {
    range_.reset();
    T min, max;
    if (parse_metadata_requirements_range(min, max, req, this->name(),
                                          this->value_parser())) {
      range_.emplace(min, max);
    }
  }

 protected:
  void check_value(T const& value) const override {
    if (range_ && (value < range_->first || value > range_->second)) {
      throw std::range_error(
          fmt::format("{} '{}' does not meet requirements [{}..{}]",
                      this->name(), value, range_->first, range_->second));
    }
  }

 private:
  std::optional<std::pair<T, T>> range_;
};

} // namespace detail

template <typename Meta = void>
class compression_metadata_requirements {
 public:
  compression_metadata_requirements() = default;

  template <
      typename F, typename U,
      typename T = typename std::invoke_result_t<F, folly::dynamic>::value_type>
  void add_set(std::string const& name, U(Meta::*mp), F&& value_parser) {
    req_.emplace_back(
        std::make_unique<detail::metadata_requirement_set<Meta, T, U>>(
            name, mp, std::forward<F>(value_parser)));
  }

  template <typename T, typename U>
  void add_set(std::string const& name, U(Meta::*mp)) {
    add_set(name, mp, detail::value_parser<T>);
  }

  template <
      typename F, typename U,
      typename T = typename std::invoke_result_t<F, folly::dynamic>::value_type>
  void add_range(std::string const& name, U(Meta::*mp), F&& value_parser) {
    req_.emplace_back(
        std::make_unique<detail::metadata_requirement_range<Meta, T, U>>(
            name, mp, std::forward<F>(value_parser)));
  }

  template <typename T, typename U>
  void add_range(std::string const& name, U(Meta::*mp)) {
    add_range(name, mp, detail::value_parser<T>);
  }

  void parse(folly::dynamic req) const {
    for (auto const& r : req_) {
      r->parse(req);
    }

    detail::check_unsupported_metadata_requirements(req);
  }

  void check(Meta const& meta) const {
    for (auto const& r : req_) {
      r->check(meta);
    }
  }

 private:
  std::vector<std::unique_ptr<detail::checked_metadata_requirement_base<Meta>>>
      req_;
};

template <>
class compression_metadata_requirements<void> {
 public:
  void parse(folly::dynamic req) const {
    detail::check_unsupported_metadata_requirements(req);
  }
};

template <>
class compression_metadata_requirements<folly::dynamic> {
 public:
  compression_metadata_requirements(std::string const& req);
  compression_metadata_requirements(folly::dynamic const& req);

  void check(std::optional<std::string> const& meta) const;
  void check(std::string const& meta) const;
  void check(folly::dynamic const& meta) const;

 private:
  std::vector<std::unique_ptr<detail::dynamic_metadata_requirement_base>> req_;
};

} // namespace dwarfs

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

#include <folly/json.h>

#include "dwarfs/compression_metadata_requirements.h"

namespace dwarfs {

namespace detail {

void check_dynamic_common(folly::dynamic const& dyn,
                          std::string_view expected_type, size_t expected_size,
                          std::string_view name) {
  if (dyn.type() != folly::dynamic::ARRAY) {
    throw std::runtime_error(
        fmt::format("found non-array type for requirement '{}', got type '{}'",
                    name, dyn.typeName()));
  }
  if (dyn.empty()) {
    throw std::runtime_error(
        fmt::format("unexpected empty value for requirement '{}'", name));
  }
  if (auto type = dyn[0].asString(); type != expected_type) {
    throw std::runtime_error(
        fmt::format("invalid type '{}' for requirement '{}', expected '{}'",
                    type, name, expected_type));
  }
  if (dyn.size() != expected_size) {
    throw std::runtime_error(fmt::format(
        "unexpected array size {} for requirement '{}', expected {}",
        dyn.size(), name, expected_size));
  }
}

void check_unsupported_metadata_requirements(folly::dynamic& req) {
  if (!req.empty()) {
    std::vector<std::string> keys;
    for (auto k : req.keys()) {
      keys.emplace_back(k.asString());
    }
    std::sort(keys.begin(), keys.end());
    throw std::runtime_error(fmt::format(
        "unsupported metadata requirements: {}", fmt::join(keys, ", ")));
  }
}

template <typename T>
class dynamic_metadata_requirement_set
    : public dynamic_metadata_requirement_base {
 public:
  static_assert(std::is_same_v<T, std::string> || std::is_integral_v<T>);

  dynamic_metadata_requirement_set(std::string const& name,
                                   folly::dynamic const& req)
      : dynamic_metadata_requirement_base{name} {
    auto tmp = req;
    if (!parse_metadata_requirements_set(set_, tmp, name,
                                         detail::value_parser<T>)) {
      throw std::runtime_error(
          fmt::format("could not parse set requirement '{}'", name));
    }
  }

  void check(folly::dynamic const& dyn) const override {
    if constexpr (std::is_same_v<T, std::string>) {
      if (!dyn.isString()) {
        throw std::runtime_error(
            fmt::format("non-string type for requirement '{}', got type '{}'",
                        name(), dyn.typeName()));
      }

      if (set_.find(dyn.asString()) == set_.end()) {
        throw std::runtime_error(
            fmt::format("{} '{}' does not meet requirements [{}]", name(),
                        dyn.asString(), fmt::join(ordered_set(set_), ", ")));
      }
    } else {
      if (!dyn.isInt()) {
        throw std::runtime_error(
            fmt::format("non-integral type for requirement '{}', got type '{}'",
                        name(), dyn.typeName()));
      }

      if (set_.find(dyn.asInt()) == set_.end()) {
        throw std::runtime_error(
            fmt::format("{} '{}' does not meet requirements [{}]", name(),
                        dyn.asInt(), fmt::join(ordered_set(set_), ", ")));
      }
    }
  }

 private:
  std::unordered_set<T> set_;
};

class dynamic_metadata_requirement_range
    : public dynamic_metadata_requirement_base {
 public:
  dynamic_metadata_requirement_range(std::string const& name,
                                     folly::dynamic const& req)
      : dynamic_metadata_requirement_base{name} {
    auto tmp = req;
    if (!parse_metadata_requirements_range(min_, max_, tmp, name,
                                           detail::value_parser<int64_t>)) {
      throw std::runtime_error(
          fmt::format("could not parse range requirement '{}'", name));
    }
  }

  void check(folly::dynamic const& dyn) const override {
    if (!dyn.isInt()) {
      throw std::runtime_error(
          fmt::format("non-integral type for requirement '{}', got type '{}'",
                      name(), dyn.typeName()));
    }

    auto v = dyn.asInt();

    if (v < min_ || v > max_) {
      throw std::runtime_error(
          fmt::format("{} '{}' does not meet requirements [{}, {}]", name(), v,
                      min_, max_));
    }
  }

 private:
  int64_t min_, max_;
};

} // namespace detail

compression_metadata_requirements<
    folly::dynamic>::compression_metadata_requirements(std::string const& req)
    : compression_metadata_requirements(folly::parseJson(req)) {}

compression_metadata_requirements<folly::dynamic>::
    compression_metadata_requirements(folly::dynamic const& req) {
  if (req.type() != folly::dynamic::OBJECT) {
    throw std::runtime_error(
        fmt::format("metadata requirements must be an object, got type '{}'",
                    req.typeName()));
  }

  for (auto const& [k, v] : req.items()) {
    if (v.type() != folly::dynamic::ARRAY) {
      throw std::runtime_error(
          fmt::format("requirement '{}' must be an array, got type '{}'",
                      k.asString(), v.typeName()));
    }

    if (v.size() < 2) {
      throw std::runtime_error(
          fmt::format("requirement '{}' must be an array of at least 2 "
                      "elements, got only {}",
                      k.asString(), v.size()));
    }

    if (v[0].type() != folly::dynamic::STRING) {
      throw std::runtime_error(fmt::format(
          "type for requirement '{}' must be a string, got type '{}'",
          k.asString(), v[0].typeName()));
    }

    if (v[0].asString() == "set") {
      if (v[1].type() != folly::dynamic::ARRAY) {
        throw std::runtime_error(fmt::format(
            "set for requirement '{}' must be an array, got type '{}'",
            k.asString(), v[1].typeName()));
      }
      if (v[1].empty()) {
        throw std::runtime_error(fmt::format(
            "set for requirement '{}' must not be empty", k.asString()));
      }
      if (v[1][0].isString()) {
        req_.emplace_back(
            std::make_unique<
                detail::dynamic_metadata_requirement_set<std::string>>(
                k.asString(), req));
      } else {
        req_.emplace_back(
            std::make_unique<detail::dynamic_metadata_requirement_set<int64_t>>(
                k.asString(), req));
      }
    } else if (v[0].asString() == "range") {
      req_.emplace_back(
          std::make_unique<detail::dynamic_metadata_requirement_range>(
              k.asString(), req));
    } else {
      throw std::runtime_error(
          fmt::format("unsupported requirement type '{}'", v[0].asString()));
    }
  }
}

void compression_metadata_requirements<folly::dynamic>::check(
    folly::dynamic const& dyn) const {
  for (auto const& r : req_) {
    if (auto it = dyn.find(r->name()); it != dyn.items().end()) {
      r->check(it->second);
    } else {
      throw std::runtime_error(
          fmt::format("missing requirement '{}'", r->name()));
    }
  }
}

void compression_metadata_requirements<folly::dynamic>::check(
    std::string const& metadata) const {
  check(folly::parseJson(metadata));
}

void compression_metadata_requirements<folly::dynamic>::check(
    std::optional<std::string> const& metadata) const {
  folly::dynamic obj = folly::dynamic::object;
  if (metadata) {
    obj = folly::parseJson(*metadata);
  }
  check(obj);
}

} // namespace dwarfs

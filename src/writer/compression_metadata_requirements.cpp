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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <dwarfs/writer/compression_metadata_requirements.h>

namespace dwarfs::writer {

namespace detail {

void check_json_common(nlohmann::json const& jsn,
                       std::string_view expected_type, size_t expected_size,
                       std::string_view name) {
  if (!jsn.is_array()) {
    throw std::runtime_error(
        fmt::format("found non-array type for requirement '{}', got type '{}'",
                    name, jsn.type_name()));
  }
  if (jsn.empty()) {
    throw std::runtime_error(
        fmt::format("unexpected empty value for requirement '{}'", name));
  }
  if (!jsn[0].is_string()) {
    throw std::runtime_error(
        fmt::format("non-string type for requirement '{}', got type '{}'", name,
                    jsn[0].type_name()));
  }
  if (auto type = jsn[0].get<std::string>(); type != expected_type) {
    throw std::runtime_error(
        fmt::format("invalid type '{}' for requirement '{}', expected '{}'",
                    type, name, expected_type));
  }
  if (jsn.size() != expected_size) {
    throw std::runtime_error(fmt::format(
        "unexpected array size {} for requirement '{}', expected {}",
        jsn.size(), name, expected_size));
  }
}

void check_unsupported_metadata_requirements(nlohmann::json& req) {
  if (!req.empty()) {
    std::vector<std::string> keys;
    for (auto const& [k, v] : req.items()) {
      keys.emplace_back(k);
    }
    std::ranges::sort(keys);
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
                                   nlohmann::json const& req)
      : dynamic_metadata_requirement_base{name} {
    auto tmp = req;
    if (!parse_metadata_requirements_set(set_, tmp, name,
                                         detail::value_parser<T>)) {
      throw std::runtime_error(
          fmt::format("could not parse set requirement '{}'", name));
    }
  }

  void check(nlohmann::json const& jsn) const override {
    if constexpr (std::is_same_v<T, std::string>) {
      if (!jsn.is_string()) {
        throw std::runtime_error(
            fmt::format("non-string type for requirement '{}', got type '{}'",
                        name(), jsn.type_name()));
      }

      if (set_.find(jsn.get<std::string>()) == set_.end()) {
        throw std::runtime_error(fmt::format(
            "{} '{}' does not meet requirements [{}]", name(),
            jsn.get<std::string>(), fmt::join(ordered_set(set_), ", ")));
      }
    } else {
      if (!jsn.is_number_integer()) {
        throw std::runtime_error(
            fmt::format("non-integral type for requirement '{}', got type '{}'",
                        name(), jsn.type_name()));
      }

      if (set_.find(jsn.get<int>()) == set_.end()) {
        throw std::runtime_error(
            fmt::format("{} '{}' does not meet requirements [{}]", name(),
                        jsn.get<int>(), fmt::join(ordered_set(set_), ", ")));
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
                                     nlohmann::json const& req)
      : dynamic_metadata_requirement_base{name} {
    auto tmp = req;
    if (!parse_metadata_requirements_range(min_, max_, tmp, name,
                                           detail::value_parser<int64_t>)) {
      throw std::runtime_error(
          fmt::format("could not parse range requirement '{}'", name));
    }
  }

  void check(nlohmann::json const& jsn) const override {
    if (!jsn.is_number_integer()) {
      throw std::runtime_error(
          fmt::format("non-integral type for requirement '{}', got type '{}'",
                      name(), jsn.type_name()));
    }

    auto v = jsn.get<int>();

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
    nlohmann::json>::compression_metadata_requirements(std::string const& req)
    : compression_metadata_requirements(nlohmann::json::parse(req)) {}

compression_metadata_requirements<nlohmann::json>::
    compression_metadata_requirements(nlohmann::json const& req) {
  if (!req.is_object()) {
    throw std::runtime_error(
        fmt::format("metadata requirements must be an object, got type '{}'",
                    req.type_name()));
  }

  for (auto const& [k, v] : req.items()) {
    if (!v.is_array()) {
      throw std::runtime_error(
          fmt::format("requirement '{}' must be an array, got type '{}'", k,
                      v.type_name()));
    }

    if (v.size() < 2) {
      throw std::runtime_error(
          fmt::format("requirement '{}' must be an array of at least 2 "
                      "elements, got only {}",
                      k, v.size()));
    }

    if (!v[0].is_string()) {
      throw std::runtime_error(fmt::format(
          "type for requirement '{}' must be a string, got type '{}'", k,
          v[0].type_name()));
    }

    if (v[0].get<std::string>() == "set") {
      if (!v[1].is_array()) {
        throw std::runtime_error(fmt::format(
            "set for requirement '{}' must be an array, got type '{}'", k,
            v[1].type_name()));
      }
      if (v[1].empty()) {
        throw std::runtime_error(
            fmt::format("set for requirement '{}' must not be empty", k));
      }
      if (v[1][0].is_string()) {
        req_.emplace_back(
            std::make_unique<
                detail::dynamic_metadata_requirement_set<std::string>>(k, req));
      } else {
        req_.emplace_back(
            std::make_unique<detail::dynamic_metadata_requirement_set<int64_t>>(
                k, req));
      }
    } else if (v[0].get<std::string>() == "range") {
      req_.emplace_back(
          std::make_unique<detail::dynamic_metadata_requirement_range>(k, req));
    } else {
      throw std::runtime_error(
          fmt::format("unsupported requirement type {}", v[0].dump()));
    }
  }
}

void compression_metadata_requirements<nlohmann::json>::check(
    nlohmann::json const& jsn) const {
  for (auto const& r : req_) {
    if (auto it = jsn.find(r->name()); it != jsn.end()) {
      r->check(*it);
    } else {
      throw std::runtime_error(
          fmt::format("missing requirement '{}'", r->name()));
    }
  }
}

void compression_metadata_requirements<nlohmann::json>::check(
    std::string const& metadata) const {
  check(nlohmann::json::parse(metadata));
}

void compression_metadata_requirements<nlohmann::json>::check(
    std::optional<std::string> const& metadata) const {
  nlohmann::json obj;
  if (metadata) {
    obj = nlohmann::json::parse(*metadata);
  }
  check(obj);
}

} // namespace dwarfs::writer

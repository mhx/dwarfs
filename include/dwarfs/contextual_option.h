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

#include <iosfwd>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

#include <fmt/format.h>

namespace dwarfs {

enum class contextual_option_policy {
  normal,
  fallback,
};

template <typename OptionType, typename ContextParser, typename OptionParser>
class contextual_option_parser;

template <typename Policy>
class contextual_option {
 public:
  using policy_type = Policy;
  using context_argument_type = typename policy_type::ContextArgumentType;
  using context_type = typename policy_type::ContextType;
  using value_type = typename policy_type::ValueType;

  template <typename OptionType, typename ContextParser, typename OptionParser>
  friend class contextual_option_parser;

  contextual_option() = default;
  explicit contextual_option(value_type const& def)
      : default_{def} {}

  void set_default(value_type const& val) { default_ = val; }

  bool add_contextual(
      context_type const& ctx, value_type const& val,
      contextual_option_policy policy = contextual_option_policy::normal) {
    return contextual_.emplace(ctx, val).second ||
           policy == contextual_option_policy::fallback;
  }

  std::optional<value_type>
  get_optional(context_argument_type const& arg) const {
    if constexpr (std::is_same_v<context_type, context_argument_type>) {
      return get_optional_impl(arg);
    } else {
      return get_optional_impl(policy_type::context_from_arg(arg));
    }
  }

  value_type get(context_argument_type const& arg) const {
    if constexpr (std::is_same_v<context_type, context_argument_type>) {
      return get_impl(arg);
    } else {
      return get_impl(policy_type::context_from_arg(arg));
    }
  }

  std::optional<value_type> get_optional() const { return default_; }

  value_type get() const { return default_.value(); }

  template <typename T>
  bool any_is(T&& pred) const {
    for (auto e : contextual_) {
      if (pred(e.second)) {
        return true;
      }
    }
    return default_ && pred(*default_);
  }

  template <typename T>
  void visit_contextual(T&& visitor) const {
    for (auto const& [ctx, val] : contextual_) {
      visitor(ctx, val);
    }
  }

 private:
  std::optional<value_type> get_optional_impl(context_type const& ctx) const {
    if (auto it = contextual_.find(ctx); it != contextual_.end()) {
      return it->second;
    }
    return default_;
  }

  value_type get_impl(context_type const& ctx) const {
    if (auto it = contextual_.find(ctx); it != contextual_.end()) {
      return it->second;
    }
    return default_.value();
  }

  std::optional<value_type> default_;
  std::unordered_map<context_type, value_type> contextual_;
};

template <typename OptionType, typename ContextParser, typename OptionParser>
class contextual_option_parser {
 public:
  using option_type = OptionType;
  using policy_type = typename option_type::policy_type;

  contextual_option_parser(std::string_view name, OptionType& opt,
                           ContextParser const& cp, OptionParser const& op)
      : opt_{opt}
      , cp_{cp}
      , op_{op}
      , name_{name} {}

  void parse(std::string_view arg, contextual_option_policy policy =
                                       contextual_option_policy::normal) const {
    try {
      auto pos = arg.find("::");

      if (pos == arg.npos) {
        opt_.set_default(op_.parse(arg));
      } else {
        auto ctx = arg.substr(0, pos);
        auto val = op_.parse(arg.substr(pos + 2));
        if constexpr (std::is_same_v<
                          std::invoke_result_t<decltype(&ContextParser::parse),
                                               ContextParser, decltype(ctx)>,
                          typename option_type::context_type>) {
          add_contextual(cp_.parse(ctx), val, policy);
        } else {
          for (auto c : cp_.parse(ctx)) {
            add_contextual(c, val, policy);
          }
        }
      }
    } catch (std::exception const& e) {
      throw std::runtime_error(
          fmt::format("failed to parse value '{}' for option '{}': {}", arg,
                      name_, e.what()));
    }
  }

  void parse_fallback(std::string_view arg) const {
    parse(arg, contextual_option_policy::fallback);
  }

  void parse(std::span<std::string const> list) const {
    for (auto const& arg : list) {
      parse(arg);
    }
  }

  void parse(std::span<std::string_view const> list) const {
    for (auto const& arg : list) {
      parse(arg);
    }
  }

  void dump(std::ostream& os) const {
    os << "[" << name_ << "]\n";
    os << "  default: ";
    if (opt_.default_) {
      os << op_.to_string(*opt_.default_) << "\n";
    } else {
      os << "(no default set)\n";
    }
    for (auto const& [ctx, val] : opt_.contextual_) {
      os << "  [" << cp_.to_string(ctx) << "]: " << op_.to_string(val) << "\n";
    }
  }

  std::string as_string() const {
    std::ostringstream oss;
    dump(oss);
    return oss.str();
  }

  std::string const& name() const { return name_; }

 private:
  void add_contextual(typename option_type::context_type const& ctx,
                      typename option_type::value_type const& val,
                      contextual_option_policy policy) const {
    if (!opt_.add_contextual(ctx, val, policy)) {
      throw std::runtime_error(fmt::format(
          "duplicate context '{}' for option '{}'", cp_.to_string(ctx), name_));
    }
  }

  OptionType& opt_;
  ContextParser const& cp_;
  OptionParser const& op_;
  std::string const name_;
};

} // namespace dwarfs

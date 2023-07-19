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

#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

#include <fmt/format.h>

namespace dwarfs {

template <typename Policy>
class contextual_option {
 public:
  using policy_type = Policy;
  using context_argument_type = typename policy_type::ContextArgumentType;
  using context_type = typename policy_type::ContextType;
  using option_type = typename policy_type::OptionType;

  contextual_option() = default;
  explicit contextual_option(option_type const& def)
      : default_{def} {}

  void set_default(option_type const& val) { default_ = val; }

  void add_contextual(context_type const& ctx, option_type const& val) {
    contextual_[ctx] = val;
  }

  std::optional<option_type>
  get_optional(context_argument_type const& arg) const {
    if constexpr (std::is_same_v<context_type, context_argument_type>) {
      return get_optional_impl(arg);
    } else {
      return get_optional_impl(policy_type::context_from_arg(arg));
    }
  }

  option_type get(context_argument_type const& arg) const {
    if constexpr (std::is_same_v<context_type, context_argument_type>) {
      return get_impl(arg);
    } else {
      return get_impl(policy_type::context_from_arg(arg));
    }
  }

  std::optional<option_type> get_optional() const { return default_; }

  option_type get() const { return default_.value(); }

  template <typename T>
  bool any_is(T&& pred) const {
    for (auto e : contextual_) {
      if (pred(e.second)) {
        return true;
      }
    }
    return default_ && pred(*default_);
  }

 private:
  std::optional<option_type> get_optional_impl(context_type const& ctx) const {
    if (auto it = contextual_.find(ctx); it != contextual_.end()) {
      return it->second;
    }
    return default_;
  }

  option_type get_impl(context_type const& ctx) const {
    if (auto it = contextual_.find(ctx); it != contextual_.end()) {
      return it->second;
    }
    return default_.value();
  }

  std::optional<option_type> default_;
  std::unordered_map<context_type, option_type> contextual_;
};

template <typename OptionType, typename ContextParser, typename OptionParser>
class contextual_option_parser {
 public:
  using option_type = OptionType;
  using policy_type = typename option_type::policy_type;

  contextual_option_parser(OptionType& opt, ContextParser const& cp,
                           OptionParser const& op)
      : opt_{opt}
      , cp_{cp}
      , op_{op} {}

  void parse(std::string_view arg) const {
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
          opt_.add_contextual(cp_.parse(ctx), val);
        } else {
          for (auto c : cp_.parse(ctx)) {
            opt_.add_contextual(c, val);
          }
        }
      }
    } catch (std::exception const& e) {
      throw std::runtime_error(
          fmt::format("failed to parse: {} ({})", arg, e.what()));
    }
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

 private:
  OptionType& opt_;
  ContextParser const& cp_;
  OptionParser const& op_;
};

} // namespace dwarfs

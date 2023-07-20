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

#include <map>
#include <stdexcept>
#include <vector>

#include <fmt/format.h>

#include <folly/gen/String.h>

#include "dwarfs/fragment_order_parser.h"

namespace dwarfs {

namespace {

const std::map<std::string_view, file_order_mode> order_choices{
    {"none", file_order_mode::NONE},
    {"path", file_order_mode::PATH},
#ifdef DWARFS_HAVE_PYTHON
    {"script", file_order_mode::SCRIPT},
#endif
    {"similarity", file_order_mode::SIMILARITY},
    {"nilsimsa", file_order_mode::NILSIMSA},
};

void parse_order_option(std::string_view ordname, std::string_view opt,
                        int& value, std::string_view name,
                        std::optional<int> min = std::nullopt,
                        std::optional<int> max = std::nullopt) {
  if (!opt.empty()) {
    if (auto val = folly::tryTo<int>(opt)) {
      auto tmp = *val;
      if (min && max && (tmp < *min || tmp > *max)) {
        throw std::range_error(
            fmt::format("{} ({}) out of range for order '{}' ({}..{})", name,
                        opt, ordname, *min, *max));
      }
      if (min && tmp < *min) {
        throw std::range_error(
            fmt::format("{} ({}) cannot be less than {} for order '{}'", name,
                        opt, *min, ordname));
      }
      if (max && tmp > *max) {
        throw std::range_error(
            fmt::format("{} ({}) cannot be greater than {} for order '{}'",
                        name, opt, *max, ordname));
      }
      value = tmp;
    } else {
      throw std::range_error(fmt::format(
          "{} ({}) is not numeric for order '{}'", name, opt, ordname));
    }
  }
}

} // namespace

std::string fragment_order_parser::choices() {
  using namespace folly::gen;
  return from(order_choices) | get<0>() | unsplit<std::string>(", ");
}

// TODO: find a common syntax for these options so we don't need
//       complex parsers like this one
file_order_options fragment_order_parser::parse(std::string_view arg) const {
  file_order_options rv;

  std::vector<std::string_view> order_opts;

  folly::split(':', arg, order_opts);

  if (auto it = order_choices.find(order_opts.front());
      it != order_choices.end()) {
    rv.mode = it->second;

    if (order_opts.size() > 1) {
      if (rv.mode != file_order_mode::NILSIMSA) {
        throw std::runtime_error(
            fmt::format("inode order mode '{}' does not support options",
                        order_opts.front()));
      }

      if (order_opts.size() > 4) {
        throw std::runtime_error(fmt::format(
            "too many options for inode order mode '{}'", order_opts.front()));
      }

      auto ordname = order_opts[0];

      parse_order_option(ordname, order_opts[1], rv.nilsimsa_limit, "limit", 0,
                         255);

      parse_order_option(ordname, order_opts[2], rv.nilsimsa_depth, "depth", 0);

      if (order_opts.size() > 3) {
        parse_order_option(ordname, order_opts[3], rv.nilsimsa_min_depth,
                           "min depth", 0);
      }
    }
  } else {
    throw std::runtime_error(fmt::format("invalid inode order mode: {}", arg));
  }

  return rv;
}

std::string
fragment_order_parser::to_string(file_order_options const& opts) const {
  switch (opts.mode) {
  case file_order_mode::NONE:
    return "none";

  case file_order_mode::PATH:
    return "path";

  case file_order_mode::SCRIPT:
    return "script";

  case file_order_mode::SIMILARITY:
    return "similarity";

  case file_order_mode::NILSIMSA:
    return fmt::format("nilsimsa (limit={}, depth={}, min_depth={})",
                       opts.nilsimsa_limit, opts.nilsimsa_depth,
                       opts.nilsimsa_min_depth);
  }
  return "<unknown>";
}

} // namespace dwarfs

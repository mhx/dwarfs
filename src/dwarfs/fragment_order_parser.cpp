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
#include "dwarfs/option_map.h"

namespace dwarfs {

namespace {

const std::map<std::string_view, file_order_mode> order_choices{
    {"none", file_order_mode::NONE},
    {"path", file_order_mode::PATH},
    {"revpath", file_order_mode::REVPATH},
    {"similarity", file_order_mode::SIMILARITY},
    {"nilsimsa", file_order_mode::NILSIMSA},
};

} // namespace

std::string fragment_order_parser::choices() {
  // TODO: C++23
  // auto tools = std::views::keys(order_choices) | std::views::join_with(", ");
  using namespace folly::gen;
  return from(order_choices) | get<0>() | unsplit<std::string>(", ");
}

// TODO: find a common syntax for these options so we don't need
//       complex parsers like this one
file_order_options fragment_order_parser::parse(std::string_view arg) const {
  file_order_options rv;

  option_map om(arg);
  auto algo = om.choice();

  if (auto it = order_choices.find(algo); it != order_choices.end()) {
    rv.mode = it->second;
  } else {
    throw std::runtime_error(fmt::format("invalid inode order mode: {}", algo));
  }

  if (om.has_options()) {
    switch (rv.mode) {
    case file_order_mode::NILSIMSA:
      rv.nilsimsa_max_children = om.get_size(
          "max-children", file_order_options::kDefaultNilsimsaMaxChildren);
      rv.nilsimsa_max_cluster_size =
          om.get_size("max-cluster-size",
                      file_order_options::kDefaultNilsimsaMaxClusterSize);

      if (rv.nilsimsa_max_children < 1) {
        throw std::runtime_error(fmt::format("invalid max-children value: {}",
                                             rv.nilsimsa_max_children));
      }

      if (rv.nilsimsa_max_cluster_size < 1) {
        throw std::runtime_error(
            fmt::format("invalid max-cluster-size value: {}",
                        rv.nilsimsa_max_cluster_size));
      }
      break;

    default:
      throw std::runtime_error(
          fmt::format("inode order mode '{}' does not support options", algo));
    }

    om.report();
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

  case file_order_mode::REVPATH:
    return "revpath";

  case file_order_mode::SIMILARITY:
    return "similarity";

  case file_order_mode::NILSIMSA:
    return fmt::format("nilsimsa:max_children={}:max_cluster_size={}",
                       opts.nilsimsa_max_children,
                       opts.nilsimsa_max_cluster_size);
  }
  return "<unknown>";
}

} // namespace dwarfs

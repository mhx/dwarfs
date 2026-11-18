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

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <range/v3/algorithm/find.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/file_access.h>
#include <dwarfs/logger.h>
#include <dwarfs/option_map.h>
#include <dwarfs/writer/fragment_order_parser.h>

namespace dwarfs::writer {

namespace {

using namespace std::string_view_literals;

constexpr std::array order_choices{
    std::pair{"none"sv, fragment_order_mode::NONE},
    std::pair{"path"sv, fragment_order_mode::PATH},
    std::pair{"revpath"sv, fragment_order_mode::REVPATH},
    std::pair{"similarity"sv, fragment_order_mode::SIMILARITY},
    std::pair{"nilsimsa"sv, fragment_order_mode::NILSIMSA},
    std::pair{"explicit"sv, fragment_order_mode::EXPLICIT},
};

} // namespace

std::string fragment_order_parser::choices() {
  return ranges::views::keys(order_choices) | ranges::views::join(", "sv) |
         ranges::to<std::string>();
}

// TODO: find a common syntax for these options so we don't need
//       complex parsers like this one
fragment_order_options
fragment_order_parser::parse(std::string_view arg) const {
  fragment_order_options rv;

  option_map om(arg);
  auto algo = om.choice();

  if (auto it = ranges::find(
          order_choices, algo,
          &std::pair<std::string_view, fragment_order_mode>::first);
      it != order_choices.end()) {
    rv.mode = it->second;
  } else {
    throw std::runtime_error(fmt::format("invalid inode order mode: {}", algo));
  }

  if (om.has_options()) {
    switch (rv.mode) {
    case fragment_order_mode::NILSIMSA: {
      LOG_PROXY(prod_logger_policy, lgr_);
      for (auto key : {"max-children"sv, "max-cluster-size"sv}) {
        if (om.get_optional<std::string>(key, std::string{})) {
          LOG_WARN << "nilsimsa option '" << key
                   << "' is deprecated and ignored";
        }
      }
      rv.nilsimsa_passes =
          om.get_size("passes", fragment_order_options::kDefaultNilsimsaPasses);
      rv.nilsimsa_window =
          om.get_size("window", fragment_order_options::kDefaultNilsimsaWindow);
      rv.nilsimsa_offset_bits = om.get_size(
          "offset", fragment_order_options::kDefaultNilsimsaOffsetBits);
      rv.nilsimsa_seed = om.get<uint64_t>("seed", uint64_t{0});
      if (rv.nilsimsa_passes < 1 || rv.nilsimsa_passes > 32) {
        throw std::runtime_error(
            fmt::format("invalid passes value: {}", rv.nilsimsa_passes));
      }
      if (rv.nilsimsa_window < 1 || rv.nilsimsa_window > 128) {
        throw std::runtime_error(
            fmt::format("invalid window value: {}", rv.nilsimsa_window));
      }
      if (rv.nilsimsa_offset_bits > 8) {
        throw std::runtime_error(
            fmt::format("invalid offset value: {}", rv.nilsimsa_offset_bits));
      }
    } break;

    case fragment_order_mode::EXPLICIT: {
      auto file = om.get<std::string>("file");
      std::error_code ec;
      auto input = fa_->open_input(file, ec);

      if (ec) {
        throw std::runtime_error(fmt::format(
            "failed to open explicit order file '{}': {}", file, ec.message()));
      }

      std::string line;
      while (std::getline(input->is(), line)) {
        auto const path = std::filesystem::path{line}.relative_path();
        rv.explicit_order[path] = rv.explicit_order.size();
      }
      rv.explicit_order_file = std::move(file);
    } break;

    default:
      throw std::runtime_error(
          fmt::format("inode order mode '{}' does not support options", algo));
    }

    om.report();
  }

  return rv;
}

std::string
fragment_order_parser::to_string(fragment_order_options const& opts) const {
  switch (opts.mode) {
  case fragment_order_mode::NONE:
    return "none";

  case fragment_order_mode::PATH:
    return "path";

  case fragment_order_mode::REVPATH:
    return "revpath";

  case fragment_order_mode::SIMILARITY:
    return "similarity";

  case fragment_order_mode::NILSIMSA:
    return fmt::format("nilsimsa:passes={}:window={}:offset={}:seed={}",
                       opts.nilsimsa_passes, opts.nilsimsa_window,
                       opts.nilsimsa_offset_bits, opts.nilsimsa_seed);

  case fragment_order_mode::EXPLICIT:
    return fmt::format("explicit:file={}", opts.explicit_order_file);
  }
  return "<unknown>";
}

} // namespace dwarfs::writer

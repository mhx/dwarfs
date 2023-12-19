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

#include <ostream>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "dwarfs/history.h"
#include "dwarfs/version.h"

namespace dwarfs {

history::history(history_config const& cfg)
    : cfg_{cfg} {}

void history::parse(std::span<uint8_t const> data) {
  history_.entries()->clear();
  parse_append(data);
}

void history::parse_append(std::span<uint8_t const> data) {
  folly::Range<const uint8_t*> range{data.data(), data.size()};
  thrift::history::history tmp;
  apache::thrift::CompactSerializer::deserialize(range, tmp);
  history_.entries()->insert(history_.entries()->end(),
                             std::make_move_iterator(tmp.entries()->begin()),
                             std::make_move_iterator(tmp.entries()->end()));
}

void history::append(std::optional<std::vector<std::string>> args) {
  auto& histent = history_.entries()->emplace_back();
  auto& version = histent.version().value();
  version.major() = PRJ_VERSION_MAJOR;
  version.minor() = PRJ_VERSION_MINOR;
  version.patch() = PRJ_VERSION_PATCH;
  version.is_release() = std::string_view(PRJ_GIT_DESC) == PRJ_GIT_ID;
  version.git_rev() = PRJ_GIT_REV;
  version.git_branch() = PRJ_GIT_BRANCH;
  version.git_desc() = PRJ_GIT_DESC;
  histent.system_id() = PRJ_SYSTEM_ID;
  histent.compiler_id() = PRJ_COMPILER_ID;
  if (args) {
    histent.arguments() = std::move(*args);
  }
  if (cfg_.with_timestamps) {
    histent.timestamp() = std::time(nullptr);
  }
}

std::vector<uint8_t> history::serialize() const {
  std::string buf;
  ::apache::thrift::CompactSerializer::serialize(history_, &buf);
  return std::vector<uint8_t>(buf.begin(), buf.end());
}

void history::dump(std::ostream& os) const {
  if (!history_.entries()->empty()) {
    size_t const iwidth{std::to_string(history_.entries()->size()).size()};
    size_t i{1};

    os << "History:\n";
    for (auto const& histent : *history_.entries()) {
      os << "  " << fmt::format("{:>{}}:", i++, iwidth);

      if (histent.timestamp().has_value()) {
        os << " "
           << fmt::format("[{:%Y-%m-%d %H:%M:%S}]",
                          fmt::localtime(histent.timestamp().value()));
      }

      auto const& version = histent.version().value();

      os << " libdwarfs " << version.git_desc().value();

      if (!version.is_release().value()) {
        os << " (" << version.git_branch().value() << ")";
      }

      os << " on " << histent.system_id().value() << ", "
         << histent.compiler_id().value() << "\n";

      if (histent.arguments().has_value() && !histent.arguments()->empty()) {
        os << fmt::format("  {:>{}}  args:", "", iwidth);
        for (auto const& arg : histent.arguments().value()) {
          os << ' ' << arg;
        }
        os << "\n";
      }
    }
  }
}

folly::dynamic history::as_dynamic() const {
  folly::dynamic dyn = folly::dynamic::array;

  for (auto const& histent : *history_.entries()) {
    folly::dynamic entry = folly::dynamic::object;

    auto const& version = histent.version().value();

    entry["libdwarfs_version"] = folly::dynamic::object
        // clang-format off
        ("major", version.major().value())
        ("minor", version.minor().value())
        ("patch", version.patch().value())
        ("is_release", version.is_release().value())
        // clang-format on
        ;

    auto& version_dyn = entry["libdwarfs_version"];

    if (version.git_rev().has_value()) {
      version_dyn["git_rev"] = version.git_rev().value();
    }

    if (version.git_branch().has_value()) {
      version_dyn["git_branch"] = version.git_branch().value();
    }

    if (version.git_desc().has_value()) {
      version_dyn["git_desc"] = version.git_desc().value();
    }

    entry["system_id"] = histent.system_id().value();
    entry["compiler_id"] = histent.compiler_id().value();

    if (histent.arguments().has_value()) {
      folly::dynamic args = folly::dynamic::array;
      for (auto const& arg : histent.arguments().value()) {
        args.push_back(arg);
      }
      entry["arguments"] = std::move(args);
    }

    if (histent.timestamp().has_value()) {
      entry["timestamp"] = folly::dynamic::object
          // clang-format off
          ("epoch", histent.timestamp().value())
          ("local", fmt::format("{:%Y-%m-%dT%H:%M:%S}",
                                fmt::localtime(histent.timestamp().value())))
          // clang-format on
          ;
    }

    dyn.push_back(std::move(entry));
  }

  return dyn;
}

} // namespace dwarfs

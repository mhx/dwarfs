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
#include <string>
#include <vector>

#include <folly/dynamic.h>

#include "dwarfs/options.h"

#include "dwarfs/gen-cpp2/history_types.h"

namespace dwarfs {

class history {
 public:
  explicit history(history_config const& cfg = {});

  void parse(std::span<uint8_t const> data);
  void parse_append(std::span<uint8_t const> data);
  thrift::history::history const& get() const { return history_; }
  void append(std::optional<std::vector<std::string>> args);
  std::vector<uint8_t> serialize() const;
  void dump(std::ostream& os) const;
  folly::dynamic as_dynamic() const;

 private:
  thrift::history::history history_;
  history_config const cfg_;
};

} // namespace dwarfs

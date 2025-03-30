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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <dwarfs/byte_buffer.h>
#include <dwarfs/history_config.h>

namespace dwarfs {

namespace thrift::history {

class history;

} // namespace thrift::history

class history {
 public:
  explicit history(history_config const& cfg = {});
  ~history();

  void parse(std::span<uint8_t const> data);
  void parse_append(std::span<uint8_t const> data);
  thrift::history::history const& get() const { return *history_; }
  void append(std::optional<std::vector<std::string>> args);
  size_t size() const;
  shared_byte_buffer serialize() const;
  void dump(std::ostream& os) const;
  nlohmann::json as_json() const;

 private:
  std::unique_ptr<thrift::history::history> history_;
  history_config const cfg_;
};

} // namespace dwarfs

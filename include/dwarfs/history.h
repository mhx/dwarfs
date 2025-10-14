/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
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

class library_dependencies;

namespace thrift::history {

class history;

} // namespace thrift::history

class history {
 public:
  explicit history(history_config const& cfg = {});
  history(history&&) noexcept;
  history& operator=(history&&) noexcept;
  ~history() noexcept;

  void parse(std::span<uint8_t const> data);
  void parse_append(std::span<uint8_t const> data);
  void
  append(std::optional<std::vector<std::string>> args,
         std::function<void(library_dependencies&)> const& extra_deps = {});
  size_t size() const;
  shared_byte_buffer serialize() const;
  void dump(std::ostream& os) const;
  nlohmann::json as_json() const;

 private:
  std::unique_ptr<thrift::history::history> history_;
  history_config cfg_;
};

} // namespace dwarfs

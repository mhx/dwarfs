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

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <dwarfs/conv.h>

namespace dwarfs {

class option_map {
 public:
  explicit option_map(std::string_view spec);

  std::string const& choice() const { return choice_; }

  bool has_options() const { return !opt_.empty(); }

  template <typename T>
  T get(std::string const& key, T const& default_value = T()) {
    auto i = opt_.find(key);

    if (i != opt_.end()) {
      std::string val = i->second;
      opt_.erase(i);
      return to<T>(val);
    }

    return default_value;
  }

  template <typename T>
  std::optional<T> get_optional(std::string const& key) {
    auto i = opt_.find(key);

    if (i != opt_.end()) {
      std::string val = i->second;
      opt_.erase(i);
      return to<T>(val);
    }

    return std::nullopt;
  }

  size_t get_size(std::string const& key, size_t default_value = 0);

  void report();

 private:
  std::unordered_map<std::string, std::string> opt_;
  std::string choice_;
};

} // namespace dwarfs

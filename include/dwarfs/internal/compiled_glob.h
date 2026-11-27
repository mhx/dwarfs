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

#include <bitset>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace dwarfs::internal {

class compiled_glob {
 public:
  explicit compiled_glob(std::string_view pattern, bool ignorecase = false,
                         bool floating = false);

  bool match(std::string_view path) const;

 private:
  enum class opcode {
    literal,
    any,
    char_set,
    star,
    globstar,
    directory,
    split,
    jump,
    accept,
  };

  struct instruction {
    opcode op;
    size_t arg{0};
  };

  static constexpr size_t npos = std::string_view::npos;

  struct char_set {
    void add_range(char32_t from, char32_t to);
    bool matches(char32_t c) const;

    std::bitset<128> ascii;
    std::vector<std::pair<char32_t, char32_t>> ranges;
    bool negate{false};
  };

  char32_t fold(char32_t c) const;
  bool match_char(instruction const& ins, char32_t c) const;
  size_t compile_char_set(std::string_view pattern, size_t pos);
  bool match_linear(std::string_view path) const;
  bool match_nfa(std::string_view path) const;

  std::vector<instruction> code_;
  std::vector<char_set> char_sets_;
  bool ignorecase_;
  size_t basename_start_{0};
  size_t linear_repeat_{npos};
};

} // namespace dwarfs::internal

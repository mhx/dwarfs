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

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <dwarfs/internal/compiled_glob.h>
#include <dwarfs/internal/unicode_case_folding.h>

namespace dwarfs::internal {

namespace {

[[noreturn]] void
invalid_pattern(std::string_view reason, std::string_view pattern) {
  throw std::runtime_error(std::string(reason) +
                           " in pattern: " + std::string(pattern));
}

// Keep invalid UTF-8 bytes distinct from valid Unicode scalar values and
// from each other.
constexpr char32_t kInvalidByteBase = 0x110000;

constexpr bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

char32_t
decode_non_ascii(std::string_view path, size_t& pos, unsigned char first) {
  unsigned extra;
  char32_t value;
  char32_t minimum;

  if (first >= 0xC2 && first <= 0xDF) {
    extra = 1;
    value = first & 0x1F;
    minimum = 0x80;
  } else if (first >= 0xE0 && first <= 0xEF) {
    extra = 2;
    value = first & 0x0F;
    minimum = 0x800;
  } else if (first >= 0xF0 && first <= 0xF4) {
    extra = 3;
    value = first & 0x07;
    minimum = 0x10000;
  } else {
    return kInvalidByteBase + first;
  }

  if (path.size() - pos < extra) {
    return kInvalidByteBase + first;
  }

  for (unsigned i = 0; i < extra; ++i) {
    auto const c = static_cast<unsigned char>(path[pos + i]);
    if (!is_continuation(c)) {
      return kInvalidByteBase + first;
    }
    value = (value << 6) | (c & 0x3F);
  }

  if (value < minimum || value > 0x10FFFF ||
      (value >= 0xD800 && value <= 0xDFFF)) {
    return kInvalidByteBase + first;
  }

  pos += extra;

  return value;
}

inline char32_t next_char(std::string_view path, size_t& pos) {
  auto const first = static_cast<unsigned char>(path[pos++]);

  if (first < 0x80) [[likely]] {
    return first;
  }

  return decode_non_ascii(path, pos, first);
}

char32_t
decode_prev_non_ascii(std::string_view path, size_t& end, unsigned char last) {
  // Locate the potential leading byte, then validate using the same decoder
  // as the forward scan. Invalid input must be consumed identically in both
  // directions.
  auto start = end;

  while (start > 0 && end - start < 3 &&
         is_continuation(static_cast<unsigned char>(path[start]))) {
    --start;
  }

  auto pos = start;
  auto const c = next_char(path.substr(0, end + 1), pos);

  if (pos == end + 1 && c < kInvalidByteBase) {
    end = start;
    return c;
  }

  return kInvalidByteBase + last;
}

inline char32_t prev_char(std::string_view path, size_t& end) {
  auto const last = static_cast<unsigned char>(path[--end]);

  if (last < 0x80) [[likely]] {
    return last;
  }

  return decode_prev_non_ascii(path, end, last);
}

} // namespace

inline char32_t compiled_glob::fold(char32_t c) const {
  return ignorecase_ ? case_fold_char(c) : c;
}

void compiled_glob::char_set::add_range(char32_t from, char32_t to) {
  // Keep ASCII characters in a bitset for fast lookup.
  for (; from <= to && from < ascii.size(); ++from) {
    if (from != U'/') {
      ascii.set(from);
    }
  }

  if (from <= to) {
    if (!ranges.empty() && from <= ranges.back().second + 1 &&
        to + 1 >= ranges.back().first) {
      auto& [lo, hi] = ranges.back();
      lo = std::min(lo, from);
      hi = std::max(hi, to);
    } else {
      ranges.emplace_back(from, to);
    }
  }
}

bool compiled_glob::char_set::matches(char32_t c) const {
  if (c < ascii.size()) [[likely]] {
    return ascii[c] != negate;
  }

  return std::ranges::any_of(ranges, [c](auto const& range) {
           return c >= range.first && c <= range.second;
         }) != negate;
}

size_t compiled_glob::compile_char_set(std::string_view pattern, size_t pos) {
  char_set chars;

  ++pos; // '['

  chars.negate = pos < pattern.size() && pattern[pos] == '!';
  if (chars.negate) {
    ++pos;
  }

  // A closing bracket is literal in the first position of a character set.
  bool first = true;

  while (pos < pattern.size()) {
    if (!first && pattern[pos] == ']') {
      code_.push_back({opcode::char_set, char_sets_.size()});
      char_sets_.push_back(std::move(chars));
      return pos + 1;
    }

    first = false;

    auto const start = pos;
    auto const from = next_char(pattern, pos);
    auto to = from;

    if (from == U'/') {
      invalid_pattern("invalid character '/' in character class", pattern);
    }

    if (pos + 1 < pattern.size() && pattern[pos] == '-' &&
        pattern[pos + 1] != ']') {
      ++pos;
      to = next_char(pattern, pos);

      if (to == U'/') {
        invalid_pattern("invalid character '/' in character class", pattern);
      }

      if (from > to) {
        invalid_pattern(std::string("invalid range '") +
                            std::string(pattern.substr(start, pos - start)) +
                            "' in character class",
                        pattern);
      }
    }

    if (ignorecase_) {
      // Fold each member of the original range.
      for (auto c = from; c <= to; ++c) {
        auto const folded = case_fold_char(c);
        chars.add_range(folded, folded);
      }
    } else {
      chars.add_range(from, to);
    }
  }

  invalid_pattern("unmatched '['", pattern);
}

compiled_glob::compiled_glob(std::string_view pattern, bool ignorecase,
                             bool floating)
    : ignorecase_{ignorecase} {
  struct brace {
    size_t split;
    size_t exits{npos};
  };
  std::vector<brace> braces;

  // Floating patterns must start at a directory boundary.
  // Unlike a leading **/, the prefix must consume at least one slash.
  if (floating) {
    code_.push_back({opcode::directory});
  }

  size_t pos = 0;

  while (pos < pattern.size()) {
    auto const c = next_char(pattern, pos);

    switch (c) {
    case '\\':
      if (pos == pattern.size()) {
        invalid_pattern("trailing backslash", pattern);
      }
      code_.push_back({opcode::literal, fold(next_char(pattern, pos))});
      break;

    case '*': {
      bool const component_start = pos == 1 || pattern[pos - 2] == '/';

      if (pos < pattern.size() && pattern[pos] == '*') {
        ++pos;

        if (component_start && pos < pattern.size() && pattern[pos] == '/') {
          ++pos;
          // **/ matches zero or more complete directory components.
          auto const end = code_.size() + 2;
          code_.push_back({opcode::split, end});
          code_.push_back({opcode::directory});
        } else {
          code_.push_back({opcode::globstar});
        }
      } else {
        if (component_start && (pos == pattern.size() || pattern[pos] == '/')) {
          // A whole-component * must match a nonempty name, as before.
          code_.push_back({opcode::any});
        }

        code_.push_back({opcode::star});
      }

      break;
    }

    case '?':
      code_.push_back({opcode::any});
      break;

    case '[':
      pos = compile_char_set(pattern, pos - 1);
      break;

    case ']':
      invalid_pattern("unmatched ']'", pattern);

    case '{':
      braces.push_back({code_.size()});
      code_.push_back({opcode::split});
      break;

    case ',':
      if (!braces.empty()) {
        auto& b = braces.back();
        // Finish the current alternative, chaining jumps until the matching
        // closing brace tells us their common destination.
        code_.push_back({opcode::jump, b.exits});
        b.exits = code_.size() - 1;
        code_[b.split].arg = code_.size();
        b.split = code_.size();
        code_.push_back({opcode::split});
      } else {
        code_.push_back({opcode::literal, ','});
      }

      break;

    case '}': {
      if (braces.empty()) {
        invalid_pattern("unmatched '}'", pattern);
      }

      auto const b = braces.back();
      braces.pop_back();
      // The final alternative doesn't need a split.
      code_[b.split] = {opcode::jump, b.split + 1};

      for (auto i = b.exits; i != npos;) {
        auto const prev = code_[i].arg;
        code_[i].arg = code_.size();
        i = prev;
      }

      break;
    }

    default:
      code_.push_back({opcode::literal, fold(c)});
      break;
    }
  }

  if (!braces.empty()) {
    invalid_pattern("unmatched '{'", pattern);
  }

  code_.push_back({opcode::accept});

  // A leading directory wildcard followed by a pattern that cannot consume
  // a slash can only match the basename.
  size_t prefix = 0;

  if (code_[0].op == opcode::directory) {
    prefix = 1;
  } else if (code_[0].op == opcode::split && code_[0].arg == 2 &&
             code_[1].op == opcode::directory) {
    prefix = 2;
  }

  if (prefix != 0) {
    basename_start_ = prefix;

    for (size_t i = prefix; i < code_.size(); ++i) {
      auto const& ins = code_[i];

      if (ins.op == opcode::directory || ins.op == opcode::globstar ||
          (ins.op == opcode::literal && ins.arg == '/') ||
          (ins.op == opcode::char_set && char_sets_[ins.arg].matches(U'/'))) {
        basename_start_ = 0;
        break;
      }
    }
  }

  // Fixed-width patterns and patterns with just one ordinary repeat can be
  // matched with two linear scans.
  auto repeat = code_.size() - 1;

  for (size_t i = basename_start_; i + 1 < code_.size(); ++i) {
    switch (code_[i].op) {
    case opcode::literal:
    case opcode::any:
    case opcode::char_set:
      break;

    case opcode::star:
    case opcode::globstar:
      if (repeat != code_.size() - 1) {
        return;
      }
      repeat = i;
      break;

    default:
      return;
    }
  }

  linear_repeat_ = repeat;
}

inline bool
compiled_glob::match_char(instruction const& ins, char32_t c) const {
  switch (ins.op) {
  case opcode::literal:
    return ins.arg == c;

  case opcode::any:
    return c != '/';

  case opcode::char_set:
    return char_sets_[ins.arg].matches(c);

  default:
    return false;
  }
}

bool compiled_glob::match_linear(std::string_view path) const {
  auto const end = code_.size() - 1;
  auto const repeat = linear_repeat_;
  auto const begin = basename_start_;
  auto const width = end - begin;

  // Every code point occupies at least one byte.
  if (path.size() < width - (repeat != end)) {
    return false;
  }

  auto right = path.size();

  if (repeat != end) {
    // Check the suffix first (e.g. the extension in *.txt).
    for (size_t i = end; i > repeat + 1;) {
      if (right == 0 || !match_char(code_[--i], fold(prev_char(path, right)))) {
        return false;
      }
    }
  }

  path = path.substr(0, right);
  size_t left = 0;

  for (size_t i = begin; i < repeat; ++i) {
    if (left == path.size() ||
        !match_char(code_[i], fold(next_char(path, left)))) {
      return false;
    }
  }

  if (repeat == end) {
    return left == path.size();
  }

  return code_[repeat].op == opcode::globstar || path.find('/', left) == npos;
}

bool compiled_glob::match_nfa(std::string_view path) const {
  using word = uint64_t;

  static constexpr size_t bits = 64;
  auto const words = (code_.size() - 1) / bits + 1;

  std::array<word, 8> local{};
  std::vector<word> heap;
  std::span<word> buffers = local;

  if (words > local.size() / 2) {
    heap.resize(2 * words);
    buffers = heap;
  }

  auto current = buffers.first(words);
  auto next = buffers.subspan(words, words);

  auto add = [](std::span<word> states, size_t i) {
    states[i / bits] |= word{1} << (i % bits);
  };

  add(current, basename_start_);

  for (size_t pos = 0;;) {
    bool const at_end = pos == path.size();
    auto const c = at_end ? char32_t{0} : fold(next_char(path, pos));
    bool consumed = false;

    auto advance = [&](size_t i) {
      add(next, i);
      consumed = true;
    };

    for (size_t w = 0; w < words; ++w) {
      while (current[w]) {
        auto const i = w * bits + std::countr_zero(current[w]);

        current[w] &= current[w] - 1;

        auto const& ins = code_[i];

        switch (ins.op) {
        case opcode::split:
          add(current, i + 1);
          add(current, ins.arg);
          break;

        case opcode::jump:
          add(current, ins.arg);
          break;

        case opcode::star:
        case opcode::globstar:
          add(current, i + 1);
          if (!at_end && (ins.op == opcode::globstar || c != '/')) {
            advance(i);
          }
          break;

        case opcode::directory:
          if (!at_end) {
            advance(i);
            if (c == '/') {
              advance(i + 1);
            }
          }
          break;

        case opcode::accept:
          if (at_end) {
            return true;
          }
          break;

        default:
          if (!at_end && match_char(ins, c)) {
            advance(i + 1);
          }
          break;
        }
      }
    }

    if (!consumed) {
      return false;
    }

    std::swap(current, next);
  }
}

bool compiled_glob::match(std::string_view path) const {
  if (basename_start_ != 0) {
    auto const slash = path.rfind('/');

    if (slash != npos) {
      path.remove_prefix(slash + 1);
    } else if (basename_start_ == 1) {
      // A floating writer rule needs at least one directory separator.
      return false;
    }
  }

  return linear_repeat_ != npos ? match_linear(path) : match_nfa(path);
}

} // namespace dwarfs::internal

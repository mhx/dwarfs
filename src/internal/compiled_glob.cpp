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

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <dwarfs/internal/compiled_glob.h>

namespace dwarfs::internal {

namespace {

[[noreturn]] void
invalid_pattern(std::string_view reason, std::string_view pattern) {
  throw std::runtime_error(std::string(reason) +
                           " in pattern: " + std::string(pattern));
}

} // namespace

unsigned char compiled_glob::fold(unsigned char c) const {
  if (ignorecase_ && c >= 'A' && c <= 'Z') {
    c += 'a' - 'A';
  }
  return c;
}

size_t compiled_glob::compile_char_set(std::string_view pattern, size_t pos) {
  std::bitset<256> chars;

  ++pos; // '['

  bool const negate = pos < pattern.size() && pattern[pos] == '!';

  if (negate) {
    ++pos;
  }

  // A closing bracket is literal in the first position of a character set.
  bool first = true;

  while (pos < pattern.size()) {
    if (!first && pattern[pos] == ']') {
      if (negate) {
        chars.flip();
      }
      code_.push_back({opcode::char_set, char_sets_.size()});
      char_sets_.push_back(chars);
      return pos + 1;
    }

    first = false;

    auto const from = static_cast<unsigned char>(pattern[pos++]);

    if (from == '/') {
      invalid_pattern("invalid character '/' in character class", pattern);
    }

    if (pos + 1 < pattern.size() && pattern[pos] == '-' &&
        pattern[pos + 1] != ']') {
      auto const to = static_cast<unsigned char>(pattern[pos + 1]);

      if (to == '/') {
        invalid_pattern("invalid character '/' in character class", pattern);
      }

      if (from > to) {
        invalid_pattern(std::string("invalid range '") + char(from) + '-' +
                            char(to) + "' in character class",
                        pattern);
      }

      for (unsigned c = from; c <= to; ++c) {
        if (c != '/') {
          chars.set(fold(static_cast<unsigned char>(c)));
        }
      }

      pos += 2;
    } else {
      chars.set(fold(from));
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
    auto const c = pattern[pos++];

    switch (c) {
    case '\\':
      if (pos == pattern.size()) {
        invalid_pattern("trailing backslash", pattern);
      }
      code_.push_back(
          {opcode::literal, fold(static_cast<unsigned char>(pattern[pos++]))});
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
      code_.push_back({opcode::literal, fold(static_cast<unsigned char>(c))});
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
          (ins.op == opcode::char_set && char_sets_[ins.arg]['/'])) {
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

bool compiled_glob::match_byte(instruction const& ins, unsigned char c) const {
  switch (ins.op) {
  case opcode::literal:
    return ins.arg == c;

  case opcode::any:
    return c != '/';

  case opcode::char_set:
    return char_sets_[ins.arg][c];

  default:
    return false;
  }
}

bool compiled_glob::match_linear(std::string_view path) const {
  auto const end = code_.size() - 1;
  auto const repeat = linear_repeat_;
  auto const begin = basename_start_;
  auto const width = end - begin;

  if (repeat == end) {
    if (path.size() != width) {
      return false;
    }
  } else {
    if (path.size() < width - 1) {
      return false;
    }

    // Check the suffix first (e.g. the extension in *.txt).
    for (size_t i = end, j = path.size(); i > repeat + 1;) {
      if (!match_byte(code_[--i],
                      fold(static_cast<unsigned char>(path[--j])))) {
        return false;
      }
    }

    if (code_[repeat].op == opcode::star &&
        path.substr(repeat - begin, path.size() - (width - 1)).find('/') !=
            npos) {
      return false;
    }
  }

  for (size_t i = begin; i < repeat; ++i) {
    if (!match_byte(code_[i],
                    fold(static_cast<unsigned char>(path[i - begin])))) {
      return false;
    }
  }
  return true;
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

  for (size_t pos = 0;; ++pos) {
    bool const at_end = pos == path.size();
    auto const c = at_end ? 0 : fold(static_cast<unsigned char>(path[pos]));
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
          if (!at_end && match_byte(ins, static_cast<unsigned char>(c))) {
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

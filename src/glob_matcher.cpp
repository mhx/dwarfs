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
#include <vector>

#include <dwarfs/glob_matcher.h>

#include <dwarfs/internal/compiled_glob.h>

namespace dwarfs {

class glob_matcher_ final : public glob_matcher::impl {
 public:
  glob_matcher_() = default;

  explicit glob_matcher_(std::span<std::string const> patterns) {
    for (auto const& p : patterns) {
      add_pattern(p);
    }
  }

  glob_matcher_(std::span<std::string const> patterns,
                glob_matcher::options const& opts) {
    for (auto const& p : patterns) {
      add_pattern(p, opts);
    }
  }

  void add_pattern(std::string_view pattern) override {
    glob_matcher::options opts;

    if (pattern.starts_with("i:")) {
      opts.ignorecase = true;
      pattern.remove_prefix(2);
    } else if (pattern.starts_with(':')) {
      pattern.remove_prefix(1);
    }

    add_pattern(pattern, opts);
  }

  void add_pattern(std::string_view pattern,
                   glob_matcher::options const& opts) override {
    m_.emplace_back(pattern, opts.ignorecase);
  }

  bool match(std::string_view sv) const override {
    return std::ranges::any_of(
        m_, [&sv](auto const& glob) { return glob.match(sv); });
  }

 private:
  std::vector<internal::compiled_glob> m_;
};

glob_matcher::glob_matcher()
    : impl_{std::make_unique<glob_matcher_>()} {}

glob_matcher::glob_matcher(std::initializer_list<std::string const> patterns)
    : impl_{std::make_unique<glob_matcher_>(patterns)} {}

glob_matcher::glob_matcher(std::span<std::string const> patterns)
    : impl_{std::make_unique<glob_matcher_>(patterns)} {}

glob_matcher::glob_matcher(std::initializer_list<std::string const> patterns,
                           options const& opts)
    : impl_{std::make_unique<glob_matcher_>(patterns, opts)} {}

glob_matcher::glob_matcher(std::span<std::string const> patterns,
                           options const& opts)
    : impl_{std::make_unique<glob_matcher_>(patterns, opts)} {}

glob_matcher::~glob_matcher() = default;

} // namespace dwarfs

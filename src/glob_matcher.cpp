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

#include <algorithm>
#include <regex>
#include <vector>

#include <dwarfs/glob_matcher.h>

#include <dwarfs/internal/glob_to_regex.h>

namespace dwarfs {

namespace {

constexpr std::regex_constants::syntax_option_type
regex_flags(glob_matcher::options const& opts) {
  auto flags =
      std::regex_constants::ECMAScript | std::regex_constants::optimize;
  if (opts.ignorecase) {
    flags |= std::regex_constants::icase;
  }
  return flags;
}

std::regex
glob_to_regex(std::string_view pattern, glob_matcher::options const& opts) {
  return std::regex("(?:^" + internal::glob_to_regex_string(pattern) + "$)",
                    regex_flags(opts));
}

} // namespace

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
    } else if (pattern.starts_with(":")) {
      pattern.remove_prefix(1);
    }

    add_pattern(pattern, opts);
  }

  void add_pattern(std::string_view pattern,
                   glob_matcher::options const& opts) override {
    m_.push_back(glob_to_regex(pattern, opts));
  }

  bool match(std::string_view sv) const override {
    return std::ranges::any_of(m_, [&sv](auto const& re) {
      return std::regex_match(sv.begin(), sv.end(), re);
    });
  }

 private:
  std::vector<std::regex> m_;
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

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

#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace dwarfs {

class glob_matcher {
 public:
  struct options {
    bool ignorecase{false};
  };

  glob_matcher();
  explicit glob_matcher(std::initializer_list<std::string const> patterns);
  explicit glob_matcher(std::span<std::string const> patterns);
  glob_matcher(std::initializer_list<std::string const> patterns,
               options const& opts);
  glob_matcher(std::span<std::string const> patterns, options const& opts);
  ~glob_matcher();

  void add_pattern(std::string_view pattern) { impl_->add_pattern(pattern); }

  void add_pattern(std::string_view pattern, options const& opts) {
    impl_->add_pattern(pattern, opts);
  }

  bool match(std::string_view sv) const { return impl_->match(sv); }
  bool match(char c) const { return impl_->match(std::string_view(&c, 1)); }

  bool operator()(std::string_view sv) const { return match(sv); }
  bool operator()(char c) const { return match(c); }

  class impl {
   public:
    virtual ~impl() = default;
    virtual void add_pattern(std::string_view pattern) = 0;
    virtual void add_pattern(std::string_view pattern, options const& opts) = 0;
    virtual bool match(std::string_view sv) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

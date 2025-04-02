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

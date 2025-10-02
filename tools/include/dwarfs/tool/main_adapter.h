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

#include <span>
#include <string>
#include <string_view>

#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool {

struct iolayer;

class main_adapter {
 public:
  using main_fn_type = int (*)(int, sys_char**, iolayer const&);

  explicit main_adapter(main_fn_type main_fn);

  int operator()(int argc, sys_char** argv) const;
  int operator()(std::span<std::string const> args, iolayer const& iol) const;
  int operator()(std::span<std::string_view const> args,
                 iolayer const& iol) const;

  int safe(int argc, sys_char** argv) const;
  int safe(std::span<std::string const> args, iolayer const& iol) const;

 private:
  main_fn_type main_fn_{nullptr};
};

} // namespace dwarfs::tool

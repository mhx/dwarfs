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

#include <vector>

#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/safe_main.h>

namespace dwarfs::tool {

namespace {

template <typename T>
int call_sys_main_iolayer(std::span<T> args, iolayer const& iol,
                          main_adapter::main_fn_type main_fn) {
  std::vector<sys_string> argv;
  std::vector<sys_char*> argv_ptrs;
  argv.reserve(args.size());
  argv_ptrs.reserve(args.size());
  for (auto const& arg : args) {
    argv.emplace_back(string_to_sys_string(std::string(arg)));
    argv_ptrs.emplace_back(argv.back().data());
  }
  return main_fn(argv_ptrs.size(), argv_ptrs.data(), iol);
}

} // namespace

int main_adapter::operator()(int argc, sys_char** argv) const {
  return main_fn_(argc, argv, iolayer::system_default());
}

int main_adapter::operator()(std::span<std::string const> args,
                             iolayer const& iol) const {
  return call_sys_main_iolayer(args, iol, main_fn_);
}

int main_adapter::operator()(std::span<std::string_view const> args,
                             iolayer const& iol) const {
  return call_sys_main_iolayer(args, iol, main_fn_);
}

int main_adapter::safe(int argc, sys_char** argv) const {
  return safe_main([&] { return (*this)(argc, argv); });
}

int main_adapter::safe(std::span<std::string const> args,
                       iolayer const& iol) const {
  return safe_main([&] { return (*this)(args, iol); });
}

int main_adapter::safe(std::span<std::string_view const> args,
                       iolayer const& iol) const {
  return safe_main([&] { return (*this)(args, iol); });
}

} // namespace dwarfs::tool

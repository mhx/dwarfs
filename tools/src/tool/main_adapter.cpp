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

#include <iostream>
#include <vector>

#include <dwarfs/util.h>

#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/safe_main.h>

#ifdef DWARFS_USE_FOLLY_MEMCPY

extern "C" {

extern void* __real_memcpy(void* dest, void const* src, size_t size);
extern void* __folly_memcpy(void* dest, void const* src, size_t size);
}

namespace {

static void* (*the_memcpy)(void*, void const*, size_t) = &__real_memcpy;

void select_memcpy() {
  bool const debug = dwarfs::getenv_is_enabled("DWARFS_DEBUG_MEMCPY");
  if (__builtin_cpu_supports("avx2")) {
    the_memcpy = &__folly_memcpy;
    if (debug) {
      std::cerr << "Using AVX2 memcpy\n";
    }
  } else if (debug) {
    std::cerr << "Using default memcpy\n";
  }
}

} // namespace

extern "C" void*
__wrap_memcpy(void* __restrict dest, void const* __restrict src, size_t size) {
  return the_memcpy(dest, src, size);
}

#endif // DWARFS_USE_FOLLY_MEMCPY

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

main_adapter::main_adapter(main_fn_type main_fn)
    : main_fn_(main_fn) {
#ifdef DWARFS_USE_FOLLY_MEMCPY
  select_memcpy();
#endif
}

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

} // namespace dwarfs::tool

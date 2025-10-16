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

#include <cerrno>
#include <iostream>
#include <system_error>

#include <folly/portability/Stdlib.h>

#include <dwarfs/detail/scoped_env.h>

namespace dwarfs::detail {

namespace {

void setenv_impl(std::string const& name, std::string const& value) {
  if (::setenv(name.c_str(), value.c_str(), 1) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            "setenv failed for " + name);
  }
}

void unsetenv_impl(std::string const& name) {
  if (::unsetenv(name.c_str()) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            "unsetenv failed for " + name);
  }
}

} // namespace

scoped_env::scoped_env() = default;

scoped_env::scoped_env(std::string const& name, std::string const& value) {
  set(name, value);
}

scoped_env::~scoped_env() {
  try {
    restore();
  } catch (std::exception const& e) {
    std::cerr << "error in scoped_env destructor: " << e.what() << "\n";
  }
}

void scoped_env::set(std::string const& name, std::string const& value) {
  ensure_saved(name);
  setenv_impl(name, value);
}

void scoped_env::unset(std::string const& name) {
  ensure_saved(name);
  unsetenv_impl(name);
}

void scoped_env::restore() {
  for (auto const& [name, value] : original_) {
    if (value) {
      setenv_impl(name, *value);
    } else {
      unsetenv_impl(name);
    }
  }

  original_.clear();
}

void scoped_env::ensure_saved(std::string const& name) {
  if (!original_.contains(name)) {
    if (auto const* orig = std::getenv(name.c_str())) {
      original_[name] = std::string(orig);
    } else {
      original_[name] = std::nullopt;
    }
  }
}

} // namespace dwarfs::detail

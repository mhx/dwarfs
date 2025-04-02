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

#include <set>
#include <string>

namespace dwarfs {

enum class version_format {
  maj_min_patch_dec_100, // 1.2.3 <-> 10203
  boost,                 // 1.2.3 <-> 100203
};

class library_dependencies {
 public:
  static std::string common_as_string();

  void add_library(std::string const& name_version_string);
  void add_library(std::string const& library_name,
                   std::string const& version_string);
  void add_library(std::string const& library_name, uint64_t version,
                   version_format fmt);
  void add_library(std::string const& library_name, unsigned major,
                   unsigned minor, unsigned patch);

  void add_common_libraries();

  std::string as_string() const;
  std::set<std::string> const& as_set() const { return deps_; }

 private:
  std::set<std::string> deps_;
};

} // namespace dwarfs

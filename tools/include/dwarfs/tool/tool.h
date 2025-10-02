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

#include <functional>
#include <string>
#include <string_view>

#include <boost/program_options.hpp>

#include <dwarfs/config.h>

#ifdef DWARFS_BUILTIN_MANPAGE
#include <dwarfs/tool/manpage.h>
#endif

namespace dwarfs {

struct logger_options;
class library_dependencies;

namespace tool {

struct iolayer;

using extra_deps_fn = std::function<void(library_dependencies&)>;

std::string tool_header(std::string_view tool_name, std::string_view extra_info,
                        extra_deps_fn const& extra_deps);

std::string tool_header_nodeps(std::string_view tool_name);

inline std::string
tool_header(std::string_view tool_name, extra_deps_fn const& extra_deps) {
  return tool_header(tool_name, {}, extra_deps);
}

void add_common_options(boost::program_options::options_description& opts,
                        logger_options& logopts);

#ifdef DWARFS_BUILTIN_MANPAGE
void show_manpage(manpage::document doc, iolayer const& iol);
#endif

} // namespace tool

} // namespace dwarfs

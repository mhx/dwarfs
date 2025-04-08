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

#include <fmt/format.h>

#include <dwarfs/config.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/logger.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/version.h>

#ifdef DWARFS_BUILTIN_MANPAGE
#include <dwarfs/terminal.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/pager.h>
#include <dwarfs/tool/render_manpage.h>
#endif

#ifdef DWARFS_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#ifdef DWARFS_USE_MIMALLOC
#include <mimalloc.h>
#endif

namespace po = boost::program_options;

namespace boost {

void validate(boost::any& v, std::vector<std::string> const&,
              std::optional<bool>*, int) {
  po::validators::check_first_occurrence(v);
  v = std::make_optional(true);
}

} // namespace boost

namespace dwarfs::tool {

namespace {

#ifdef DWARFS_USE_JEMALLOC
std::string get_jemalloc_version() {
#ifdef __APPLE__
  char const* j = JEMALLOC_VERSION;
#else
  char const* j = nullptr;
  size_t s = sizeof(j);
  // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
  ::mallctl("version", &j, &s, nullptr, 0);
  assert(j);
#endif
  std::string rv{j};
  if (auto pos = rv.find('-'); pos != std::string::npos) {
    rv.erase(pos, std::string::npos);
  }
  return rv;
}
#endif

#ifdef DWARFS_USE_MIMALLOC
std::string get_mimalloc_version() {
  auto v = mi_version();
  auto major = v / 100;
  auto minor = (v % 100) / 10;
  auto patch = v % 10;
  return fmt::format("{}.{}.{}", major, minor, patch);
}
#endif

std::string
tool_header_impl(std::string_view tool_name, std::string_view extra_info = {}) {
  std::string date;

  if (DWARFS_GIT_DATE) {
    date = fmt::format(" [{}]", DWARFS_GIT_DATE);
  }

  return fmt::format(
      // clang-format off
    R"(     ___                  ___ ___)""\n"
    R"(    |   \__ __ ____ _ _ _| __/ __|         Deduplicating Warp-speed)""\n"
    R"(    | |) \ V  V / _` | '_| _|\__ \      Advanced Read-only File System)""\n"
    R"(    |___/ \_/\_/\__,_|_| |_| |___/         by Marcus Holland-Moritz)""\n\n"
      // clang-format on
      "{} ({}{}{})\nbuilt for {}\n\n",
      tool_name, DWARFS_GIT_ID, date, extra_info, DWARFS_BUILD_ID);
}

} // namespace

std::string tool_header(std::string_view tool_name, std::string_view extra_info,
                        extra_deps_fn const& extra_deps) {
  library_dependencies deps;
  deps.add_common_libraries();

#ifdef DWARFS_USE_JEMALLOC
  deps.add_library("libjemalloc", get_jemalloc_version());
#endif

#ifdef DWARFS_USE_MIMALLOC
  deps.add_library("libmimalloc", get_mimalloc_version());
#endif

  if (extra_deps) {
    extra_deps(deps);
  }

  return tool_header_impl(tool_name, extra_info) + deps.as_string() + "\n\n";
}

std::string tool_header_nodeps(std::string_view tool_name) {
  return tool_header_impl(tool_name);
}

void add_common_options(po::options_description& opts,
                        logger_options& logopts) {
  auto log_level_desc = "log level (" + logger::all_level_names() + ")";

  // clang-format off
  opts.add_options()
    ("log-level",
        po::value<logger::level_type>(&logopts.threshold)
            ->default_value(logger::INFO),
        log_level_desc.c_str())
    ("log-with-context",
        po::value<std::optional<bool>>(&logopts.with_context)->zero_tokens(),
        "enable context logging regardless of level")
#ifdef DWARFS_BUILTIN_MANPAGE
    ("man",
        "show manual page and exit")
#endif
    ("help,h",
        "output help message and exit")
    ;
  // clang-format on
}

#ifdef DWARFS_BUILTIN_MANPAGE
void show_manpage(manpage::document doc, iolayer const& iol) {
  bool is_tty = iol.term->is_tty(iol.out);

  auto content =
      render_manpage(doc, iol.term->width(), is_tty && iol.term->is_fancy());

  if (is_tty) {
    if (auto pager = find_pager_program(*iol.os)) {
      show_in_pager(*pager, content);
      return;
    }
  }

  iol.out << content;
}
#endif

} // namespace dwarfs::tool

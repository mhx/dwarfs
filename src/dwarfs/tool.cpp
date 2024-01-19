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

#include <iostream>

#include <fmt/format.h>

#include "dwarfs/logger.h"
#include "dwarfs/tool.h"
#include "dwarfs/version.h"

#ifdef DWARFS_BUILTIN_MANPAGE
#include "dwarfs/iolayer.h"
#include "dwarfs/pager.h"
#include "dwarfs/render_manpage.h"
#include "dwarfs/terminal.h"
#endif

namespace po = boost::program_options;

namespace boost {

void validate(boost::any& v, const std::vector<std::string>&,
              std::optional<bool>*, int) {
  po::validators::check_first_occurrence(v);
  v = std::make_optional(true);
}

} // namespace boost

namespace dwarfs {

std::string
tool_header(std::string_view tool_name, std::string_view extra_info) {
  std::string date;
  if (PRJ_GIT_DATE) {
    date = fmt::format(" [{}]", PRJ_GIT_DATE);
  }
  return fmt::format(
      // clang-format off
    R"(     ___                  ___ ___)""\n"
    R"(    |   \__ __ ____ _ _ _| __/ __|         Deduplicating Warp-speed)""\n"
    R"(    | |) \ V  V / _` | '_| _|\__ \      Advanced Read-only File System)""\n"
    R"(    |___/ \_/\_/\__,_|_| |_| |___/         by Marcus Holland-Moritz)""\n\n"
      // clang-format on
      "{} ({}{}{})\nbuilt for {}\n\n",
      tool_name, PRJ_GIT_ID, date, extra_info, PRJ_BUILD_ID);
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

} // namespace dwarfs

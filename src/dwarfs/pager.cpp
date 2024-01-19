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

#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/process.hpp>

#include <folly/portability/Unistd.h>

#include "dwarfs/os_access.h"
#include "dwarfs/pager.h"

namespace dwarfs {

namespace {

namespace bp = boost::process;

std::vector<pager_program> const pagers{
    {"less", {"-R"}},
};

} // namespace

std::optional<pager_program> find_pager_program(os_access const& os) {
  if (auto pager_env = os.getenv("PAGER")) {
    std::string_view sv{pager_env.value()};

    if (sv == "cat") {
      return std::nullopt;
    }

    if (sv.starts_with('"') && sv.ends_with('"')) {
      sv.remove_prefix(1);
      sv.remove_suffix(1);
    }

    std::filesystem::path p{std::string(sv)};

    if (os.access(p, X_OK) == 0) {
      return pager_program{p, {}};
    }

    if (auto exe = os.find_executable(p); !exe.empty()) {
      return pager_program{exe, {}};
    }
  }

  for (auto const& p : pagers) {
    if (auto exe = os.find_executable(p.name); !exe.empty()) {
      return pager_program{exe, p.args};
    }
  }

  return std::nullopt;
}

void show_in_pager(pager_program const& pager, std::string text) {
  boost::asio::io_service ios;
  bp::child proc(pager.name.wstring(), bp::args(pager.args),
                 bp::std_in =
                     boost::asio::const_buffer(text.data(), text.size()),
                 bp::std_out > stdout, ios);
  ios.run();
  proc.wait();
}

} // namespace dwarfs

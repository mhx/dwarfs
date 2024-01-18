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

#include "dwarfs/pager.h"

namespace dwarfs {

namespace {

namespace bp = boost::process;

struct pager_def {
  std::string name;
  std::vector<std::string> args;
};

std::vector<pager_def> const pagers{
    {"less", {"-R"}},
};

auto find_executable(std::string name) { return bp::search_path(name); }

std::pair<boost::filesystem::path, std::vector<std::string>> find_pager() {
  if (auto pager_env = std::getenv("PAGER")) {
    std::string_view sv(pager_env);
    if (sv.starts_with('"') && sv.ends_with('"')) {
      sv.remove_prefix(1);
      sv.remove_suffix(1);
    }
    if (sv == "cat") {
      return {};
    }
    boost::filesystem::path p{std::string(sv)};
    if (boost::filesystem::exists(p)) {
      return {p.string(), {}};
    }
    if (auto exe = find_executable(pager_env); !exe.empty()) {
      return {exe, {}};
    }
  }

  for (auto const& p : pagers) {
    if (auto exe = find_executable(std::string(p.name)); !exe.empty()) {
      return {exe, p.args};
    }
  }

  return {};
}

} // namespace

bool show_in_pager(std::string text) {
  auto [pager_exe, pager_args] = find_pager();

  if (pager_exe.empty()) {
    return false;
  }

  boost::asio::io_service ios;
  bp::child proc(pager_exe, bp::args(pager_args),
                 bp::std_in =
                     boost::asio::const_buffer(text.data(), text.size()),
                 bp::std_out > stdout, ios);
  ios.run();
  proc.wait();

  return true;
}

} // namespace dwarfs

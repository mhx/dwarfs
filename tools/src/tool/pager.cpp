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

#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/io_context.hpp>
#if __has_include(<boost/process/v1/args.hpp>)
#define BOOST_PROCESS_VERSION 1
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process.hpp>
#endif

#include <dwarfs/os_access.h>
#include <dwarfs/tool/pager.h>

namespace dwarfs::tool {

namespace {

namespace bp = boost::process;

std::span<pager_program const> get_pagers() {
  static std::vector<pager_program> const pagers{
      {"less", {"-R"}},
  };

  return pagers;
}

} // namespace

#ifdef _WIN32
#define X_OK 0
#endif

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

  for (auto const& p : get_pagers()) {
    if (auto exe = os.find_executable(p.name); !exe.empty()) {
      return pager_program{exe, p.args};
    }
  }

  return std::nullopt;
}

void show_in_pager(pager_program const& pager, std::string_view text) {
  boost::asio::io_context ios;
  // NOLINTBEGIN(clang-analyzer-unix.BlockInCriticalSection)
  bp::child proc(pager.name.wstring(), bp::args(pager.args),
                 bp::std_in =
                     boost::asio::const_buffer(text.data(), text.size()),
                 bp::std_out > stdout, ios);
  // NOLINTEND(clang-analyzer-unix.BlockInCriticalSection)
  ios.run();
  proc.wait();
}

} // namespace dwarfs::tool

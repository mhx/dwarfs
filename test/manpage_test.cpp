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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <array>
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/config.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/pager.h>
#include <dwarfs/tool/render_manpage.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::tool;
using namespace std::string_literals;

namespace {

struct tool_defs {
  manpage::document doc;
  main_adapter::main_fn_type main;
  std::string_view help_option;
  bool is_fuse;
};

std::map<std::string, tool_defs> const tools = {
#ifdef DWARFS_WITH_TOOLS
    {"mkdwarfs", {manpage::get_mkdwarfs_manpage(), mkdwarfs_main, "-H", false}},
    {"dwarfsck", {manpage::get_dwarfsck_manpage(), dwarfsck_main, "-h", false}},
    {"dwarfsextract",
     {manpage::get_dwarfsextract_manpage(), dwarfsextract_main, "-h", false}},
#endif
#ifdef DWARFS_WITH_FUSE_DRIVER
    {"dwarfs", {manpage::get_dwarfs_manpage(), dwarfs_main, "-h", true}},
#endif
};

auto const render_tests = ranges::views::keys(tools) | ranges::to<std::vector>;

std::array const coverage_tests{
#ifdef DWARFS_WITH_TOOLS
    "mkdwarfs"s,
    "dwarfsck"s,
    "dwarfsextract"s,
#endif
#ifdef DWARFS_WITH_FUSE_DRIVER
#ifndef DWARFS_TEST_RUNNING_ON_ASAN
    // FUSE driver is leaky, so we don't run this test under ASAN
    "dwarfs"s,
#endif
#endif
};

} // namespace

class manpage_render_test
    : public ::testing::TestWithParam<std::tuple<std::string, bool>> {};

TEST_P(manpage_render_test, basic) {
  auto [name, color] = GetParam();
  auto doc = tools.at(name).doc;
  for (size_t width = 20; width <= 200; width += 1) {
    auto out = render_manpage(doc, width, color);
    EXPECT_GT(out.size(), 1000);
    EXPECT_THAT(out, ::testing::HasSubstr(name));
    EXPECT_THAT(out, ::testing::HasSubstr("SYNOPSIS"));
    EXPECT_THAT(out, ::testing::HasSubstr("DESCRIPTION"));
    EXPECT_THAT(out, ::testing::HasSubstr("AUTHOR"));
    EXPECT_THAT(out, ::testing::HasSubstr("COPYRIGHT"));
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, manpage_render_test,
                         ::testing::Combine(::testing::ValuesIn(render_tests),
                                            ::testing::Bool()));

namespace {

std::regex const boost_po_option{R"(\n\s+(-(\w)\s+\[\s+)?--(\w[\w-]*\w))"};
std::regex const manpage_option{R"(\n\s+(-(\w),\s+)?--(\w[\w-]*\w))"};
std::regex const fuse_option{R"(\n\s+-o\s+([\w()]+))"};

std::map<std::string, std::string>
parse_options(std::string const& text, std::regex const& re, bool is_fuse) {
  std::map<std::string, std::string> options;
  auto opts_begin = std::sregex_iterator(text.begin(), text.end(), re);
  auto opts_end = std::sregex_iterator();

  for (auto it = opts_begin; it != opts_end; ++it) {
    auto match = *it;
    if (is_fuse) {
      auto opt = match[1].str();
      if (!options.emplace(opt, std::string{}).second) {
        throw std::runtime_error("duplicate option definition for " + opt);
      }
    } else {
      auto short_opt = match[2].str();
      auto long_opt = match[3].str();
      if (auto it = options.find(long_opt); it != options.end()) {
        if (!it->second.empty()) {
          if (short_opt.empty()) {
            continue;
          } else {
            throw std::runtime_error("duplicate option definition for " +
                                     long_opt);
          }
        }
      }
      if (long_opt.starts_with("experimental-")) {
        continue;
      }
      options[long_opt] = short_opt;
    }
  }

  return options;
}

} // namespace

class manpage_coverage_test : public ::testing::TestWithParam<std::string> {};

TEST_P(manpage_coverage_test, options) {
  auto tool_name = GetParam();
  auto const& tool = tools.at(tool_name);
  auto man = render_manpage(tool.doc, 80, false);
  test::test_iolayer iol;
  std::array<std::string_view, 2> const args{tool_name, tool.help_option};
  auto rv [[maybe_unused]] = main_adapter{tool.main}(args, iol.get());

#ifndef _WIN32
  // WinFSP exits with a non-zero code when displaying usage :-/
  ASSERT_EQ(0, rv) << tool_name << " " << tool.help_option << " failed";
#endif

  auto help_opts = parse_options(
      iol.out(), tool.is_fuse ? fuse_option : boost_po_option, tool.is_fuse);
  auto man_opts = parse_options(
      man, tool.is_fuse ? fuse_option : manpage_option, tool.is_fuse);

  if (tool.is_fuse) {
    man_opts.erase("allow_root");
    man_opts.erase("allow_other");
#ifdef _WIN32
    man_opts.erase("uid");
    man_opts.erase("gid");
#endif
#ifndef DWARFS_PERFMON_ENABLED
    man_opts.erase("perfmon");
    man_opts.erase("perfmon_trace");
#endif
  } else {
    EXPECT_TRUE(help_opts.contains("help"))
        << tool_name << " missing help option";
  }

  for (auto const& [opt, short_opt] : help_opts) {
    auto it = man_opts.find(opt);
    if (it == man_opts.end()) {
      FAIL() << "option " << opt << " not documented for " << tool_name;
    } else {
      EXPECT_EQ(short_opt, it->second)
          << "short option mismatch for " << opt << " for " << tool_name;
    }
  }

  if (tool_name == "dwarfsextract") {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    man_opts.erase("format");
    man_opts.erase("format-filters");
    man_opts.erase("format-options");
#endif
#ifndef DWARFS_PERFMON_ENABLED
    man_opts.erase("perfmon");
    man_opts.erase("perfmon-trace");
#endif
    man_opts.erase("pattern");
  }

  for (auto const& [opt, short_opt] : man_opts) {
    auto it = help_opts.find(opt);
    if (it == help_opts.end()) {
      FAIL() << "option " << opt << " is obsolete for " << tool_name;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, manpage_coverage_test,
                         ::testing::ValuesIn(coverage_tests));

TEST(pager_test, find_pager_program) {
  auto resolver = [](std::filesystem::path const& name) {
    std::map<std::string, std::filesystem::path> const programs = {
        {"less", "/whatever/bin/less"},
        {"more", "/somewhere/bin/more"},
        {"cat", "/bin/cat"},
    };
    for (auto const& [n, p] : programs) {
      if (name == n || name == p) {
        return p;
      }
    }
    return std::filesystem::path{};
  };

  {
    test::os_access_mock os;
    os.set_executable_resolver(
        [](std::filesystem::path const&) { return std::filesystem::path{}; });

    {
      auto pager = find_pager_program(os);
      ASSERT_FALSE(pager);
    }

    os.set_executable_resolver(resolver);

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/whatever/bin/less", pager->name);
      EXPECT_EQ(std::vector<std::string>({"-R"}), pager->args);
    }

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/whatever/bin/less", pager->name);
      EXPECT_EQ(std::vector<std::string>({"-R"}), pager->args);
    }

    os.set_access_fail("more");
    os.set_access_fail("less");

    os.setenv("PAGER", "more");

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/somewhere/bin/more", pager->name);
      EXPECT_TRUE(pager->args.empty());
    }

    os.setenv("PAGER", "less");

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/whatever/bin/less", pager->name);
      EXPECT_THAT(pager->args, ::testing::ElementsAre("-R"));
    }

    os.setenv("PAGER", "cat");

    {
      auto pager = find_pager_program(os);
      ASSERT_FALSE(pager);
    }

    os.setenv("PAGER", "/bla/foo");

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/bla/foo", pager->name);
      EXPECT_TRUE(pager->args.empty());
    }

    os.setenv("PAGER", R"("/bla/foo")");

    {
      auto pager = find_pager_program(os);
      ASSERT_TRUE(pager);
      EXPECT_EQ("/bla/foo", pager->name);
      EXPECT_TRUE(pager->args.empty());
    }
  }
}

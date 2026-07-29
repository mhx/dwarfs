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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/stat.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/detail/scoped_env.h>
#include <dwarfs/file_util.h>
#include <dwarfs/os_access.h>
#include <dwarfs/tool/pager.h>

#ifdef _WIN32
#include <dwarfs/tool/internal/pager_command_line.h>
#include <dwarfs/tool/sys_char.h>
#endif

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using namespace dwarfs::tool;

using testing::_;
using testing::ElementsAre;
using testing::NiceMock;
using testing::Pair;

namespace fs = std::filesystem;

namespace {

class pager_os_mock : public test::os_access_mock {
 public:
  MOCK_METHOD(std::optional<std::string>, getenv, (std::string_view),
              (const, override));
};

using env_map = std::map<std::string, std::string, std::less<>>;

auto make_os(env_map env = {}) {
  auto os = std::make_shared<NiceMock<pager_os_mock>>();

  ON_CALL(*os, getenv(_))
      .WillByDefault([e = std::move(env)](
                         std::string_view name) -> std::optional<std::string> {
        if (auto it = e.find(name); it != e.end()) {
          return it->second;
        }
        return std::nullopt;
      });

  return os;
}

void expect_pager(std::optional<pager_program> const& p,
                  std::string const& cmd [[maybe_unused]],
                  std::vector<std::string> const& argv [[maybe_unused]]) {
  ASSERT_TRUE(p);
#ifdef _WIN32
  EXPECT_EQ(fs::path{argv.front()}, p->name);
  EXPECT_EQ(std::vector<std::string>(std::next(argv.begin()), argv.end()),
            p->args);
#else
  EXPECT_EQ(fs::path{"/bin/sh"}, p->name);
  EXPECT_EQ((std::vector<std::string>{"-c", cmd}), p->args);
#endif
}

} // namespace

TEST(find_pager_program, no_env_uses_default_pager) {
  auto os = make_os();
  expect_pager(find_pager_program(*os), "less", {"less"});
}

TEST(find_pager_program, dwarfs_pager_wins) {
  auto os = make_os({{"DWARFS_PAGER", "p1"}, {"PAGER", "p2"}});
  expect_pager(find_pager_program(*os), "p1", {"p1"});
}

TEST(find_pager_program, pager_is_the_last_resort) {
  auto os = make_os({{"PAGER", "p2"}});
  expect_pager(find_pager_program(*os), "p2", {"p2"});
}

TEST(find_pager_program, empty_higher_priority_var_disables_paging) {
  auto os = make_os({{"DWARFS_PAGER", ""}, {"PAGER", "less"}});
  EXPECT_FALSE(find_pager_program(*os));
}

class disabling_pager_test : public testing::TestWithParam<std::string> {};

TEST_P(disabling_pager_test, disables_paging) {
  auto os = make_os({{"PAGER", GetParam()}});
  EXPECT_FALSE(find_pager_program(*os));
}

INSTANTIATE_TEST_SUITE_P(pager, disabling_pager_test,
                         testing::Values("", " ", "\t", "\n", "cat"));

class non_disabling_pager_test : public testing::TestWithParam<std::string> {};

TEST_P(non_disabling_pager_test, does_not_disable_paging) {
  auto os = make_os({{"PAGER", GetParam()}});
  EXPECT_TRUE(find_pager_program(*os));
}

INSTANTIATE_TEST_SUITE_P(pager, non_disabling_pager_test,
                         testing::Values("cat -v", " cat ", "/bin/cat",
                                         "\"cat\"", "CAT"));

#ifndef _WIN32

class posix_pager_passthrough_test
    : public testing::TestWithParam<std::string> {};

TEST_P(posix_pager_passthrough_test, is_passed_to_the_shell_verbatim) {
  auto os = make_os({{"PAGER", GetParam()}});

  auto p = find_pager_program(*os);

  ASSERT_TRUE(p);
  EXPECT_EQ(fs::path{"/bin/sh"}, p->name);
  EXPECT_THAT(p->args, ElementsAre("-c", GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    pager, posix_pager_passthrough_test,
    testing::Values("less -X -F", "less  -X   -F", "\"/opt/my pager\" -x",
                    "/opt/my\\ pager", "col -bx | less -R",
                    "sh -c 'awk \"{print}\" | bat -p -lman'",
                    "$MY_PAGER --opt=\"a b\""));

#else

struct split_case {
  std::string env;
  std::vector<std::string> argv;
};

class windows_pager_split_test : public testing::TestWithParam<split_case> {};

TEST_P(windows_pager_split_test, splits_using_platform_rules) {
  auto const& c = GetParam();
  auto os = make_os({{"PAGER", c.env}});

  auto p = find_pager_program(*os);

  ASSERT_TRUE(p);
  EXPECT_EQ(fs::path{c.argv.front()}, p->name);
  EXPECT_EQ(std::vector<std::string>(std::next(c.argv.begin()), c.argv.end()),
            p->args);
}

INSTANTIATE_TEST_SUITE_P(
    pager, windows_pager_split_test,
    testing::Values(split_case{"less -X -F", {"less", "-X", "-F"}},
                    split_case{"less  -X   -F", {"less", "-X", "-F"}},
                    split_case{"\"C:\\Program Files\\less\\less.exe\" -X",
                               {"C:\\Program Files\\less\\less.exe", "-X"}},
                    split_case{"pager --sep=\"a b\"", {"pager", "--sep=a b"}}));

TEST(pager_command_line, quoting_round_trips) {
  std::vector<std::string> const args{
      "C:\\Program Files\\less\\less.exe",
      "plain",
      "with space",
      "with\"quote",
      "trailing\\",
      "a\\\\b",
      "",
      "--opt=va lue",
  };

  std::wstring cmd;

  for (auto const& a : args) {
    if (!cmd.empty()) {
      cmd += L' ';
    }
    internal::append_quoted(cmd, string_to_sys_string(a));
  }

  auto parsed = internal::split_command_line(sys_string_to_string(cmd));

  ASSERT_TRUE(parsed);
  EXPECT_EQ(args, *parsed);
}

#endif

class show_in_pager_test : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_.emplace("dwarfs");
    out_ = dir_->path() / "out.bin";
  }

  void TearDown() override { dir_.reset(); }

  std::string read_out() const {
    std::ifstream ifs{out_, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{ifs},
                       std::istreambuf_iterator<char>{}};
  }

  std::vector<std::string> read_records() const {
    std::vector<std::string> records;
    auto blob = read_out();

    for (size_t pos = 0; pos < blob.size();) {
      auto end = blob.find('\0', pos);
      records.emplace_back(blob, pos, end - pos);
      pos = end + 1;
    }

    return records;
  }

  pager_program helper(std::vector<std::string> args) const {
#ifdef DWARFS_CROSSCOMPILING_EMULATOR
    args.insert(args.begin(), helper_path().string());
    return pager_program{DWARFS_CROSSCOMPILING_EMULATOR, std::move(args),
                         "helper"};
#else
    return pager_program{helper_path(), std::move(args), "helper"};
#endif
  }

  static fs::path helper_path() {
#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif
    auto path =
        fs::path(TOOLS_BIN_DIR).make_preferred() / "pager_test_helper" EXE_EXT;

    if (!fs::exists(path)) {
      throw std::runtime_error("pager_test_helper not found at " +
                               path.string());
    }

    return path;
  }

  std::optional<temporary_directory> dir_;
  fs::path out_;
};

TEST_F(show_in_pager_test, empty_input) {
  std::error_code ec;
  show_in_pager(helper({"cat", out_.string()}), "", ec);
  EXPECT_FALSE(ec) << ec.message();
  EXPECT_EQ("", read_out());
}

TEST_F(show_in_pager_test, small_input) {
  std::string const text{"hello\npager\n"};
  std::error_code ec;
  show_in_pager(helper({"cat", out_.string()}), text, ec);
  EXPECT_FALSE(ec) << ec.message();
  EXPECT_EQ(text, read_out());
}

TEST_F(show_in_pager_test, binary_input_with_nuls) {
  std::string text;

  for (int i = 0; i < 256; ++i) {
    text.push_back(static_cast<char>(i));
  }

  std::error_code ec;
  show_in_pager(helper({"cat", out_.string()}), text, ec);
  EXPECT_FALSE(ec) << ec.message();
  EXPECT_EQ(text, read_out());
}

TEST_F(show_in_pager_test, large_input_does_not_deadlock) {
  constexpr auto huge_size = 8_MiB;

  std::string text;
  text.reserve(huge_size);

  for (size_t i = 0; text.size() < huge_size; ++i) {
    text += "line " + std::to_string(i) + " of a rather long manual page\n";
  }

  std::error_code ec;
  show_in_pager(helper({"cat", out_.string()}), text, ec);

  EXPECT_FALSE(ec) << ec.message();
  auto got = read_out();
  ASSERT_EQ(text.size(), got.size());
  EXPECT_TRUE(text == got);
}

TEST_F(show_in_pager_test, arguments_reach_the_child_unmangled) {
  std::vector<std::string> const tricky{
      "plain",  "with space", "with\"quote", "trailing\\",
      "a\\\\b", "",           "x\\\"y",      "--opt=va lue",
  };

  std::vector<std::string> args{"argv", out_.string()};
  args.insert(args.end(), tricky.begin(), tricky.end());

  std::error_code ec;
  show_in_pager(helper(args), "", ec);

  EXPECT_FALSE(ec) << ec.message();
  EXPECT_EQ(tricky, read_records());
}

TEST_F(show_in_pager_test, pager_quitting_early_is_not_an_error) {
  std::string const text(4_MiB, 'x');

  std::error_code ec;
  show_in_pager(helper({"head", out_.string(), "128"}), text, ec);

  EXPECT_FALSE(ec) << ec.message();
  EXPECT_EQ(128, read_out().size());
}

TEST_F(show_in_pager_test, ordinary_nonzero_exit_is_not_an_error) {
  std::error_code ec;
  show_in_pager(helper({"fail", out_.string(), "3"}), "some text", ec);
  EXPECT_FALSE(ec) << ec.message();
}

TEST_F(show_in_pager_test, nonexistent_pager_is_reported) {
  std::error_code ec;
  show_in_pager(pager_program{dir_->path() / "definitely-not-here", {}, "nope"},
                "some text", ec);
  EXPECT_TRUE(ec) << ec.message();
}

#ifndef _WIN32

TEST_F(show_in_pager_test, non_executable_pager_is_reported) {
  auto path = dir_->path() / "not-executable";
  {
    std::ofstream ofs{path};
  }
  ::chmod(path.c_str(), 0644);

  std::error_code ec;
  show_in_pager(pager_program{path, {}, path.string()}, "some text", ec);

  EXPECT_TRUE(ec) << ec.message();
}

TEST_F(show_in_pager_test, pager_exiting_127_is_misread_as_not_found) {
  std::error_code ec;
  show_in_pager(helper({"fail", out_.string(), "127"}), "some text", ec);
  EXPECT_EQ(std::make_error_code(std::errc::no_such_file_or_directory), ec);
}

TEST_F(show_in_pager_test, sigpipe_disposition_is_restored) {
  for (auto disp : {SIG_DFL, SIG_IGN}) {
    auto prev = ::signal(SIGPIPE, disp);
    std::error_code ec;
    show_in_pager(helper({"head", out_.string(), "16"}),
                  std::string(4_MiB, 'y'), ec);
    auto now = ::signal(SIGPIPE, disp);
    EXPECT_EQ(disp, now);
    ::signal(SIGPIPE, prev);
  }
}

#endif

TEST_F(show_in_pager_test, sets_less_when_unset) {
  detail::scoped_env env;
  env.unset("LESS");

  std::error_code ec;
  show_in_pager(helper({"env", out_.string(), "LESS"}), "", ec);

  EXPECT_FALSE(ec) << ec.message();
  EXPECT_THAT(read_records(), ElementsAre("LESS=FRX"));

  // ... and the parent's environment is left as it was.
  EXPECT_EQ(nullptr, std::getenv("LESS"));
}

TEST_F(show_in_pager_test, does_not_override_user_less) {
  detail::scoped_env env{"LESS", "iMj3"};

  std::error_code ec;
  show_in_pager(helper({"env", out_.string(), "LESS"}), "", ec);

  EXPECT_FALSE(ec) << ec.message();
  EXPECT_THAT(read_records(), ElementsAre("LESS=iMj3"));
  EXPECT_STREQ("iMj3", std::getenv("LESS"));
}

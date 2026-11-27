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

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/command_line.h>

namespace cli = dwarfs::cli;
using namespace std::string_literals;
using namespace std::string_view_literals;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::testing::ThrowsMessage;

namespace {

void parse(cli::options& opts, std::initializer_list<std::string_view> args) {
  opts.parse(std::span(args.begin(), args.size()));
}

} // namespace

TEST(command_line, scalar_options) {
  cli::options opts;
  std::optional<std::string> input;
  std::string output;
  size_t workers;
  size_t disk;
  bool help{false};
  std::optional<bool> context;

  opts.add("input,i", input, "input file");
  opts.add("output,o", output, "output file");
  opts.add("num-workers,n", workers, "worker count", 4);
  opts.add("num-disk-writers", disk, "writer count", 0);
  opts.flag("help,h", help, "help");
  opts.flag("log-with-context", context, "context");

  parse(opts, {"--inp=file", "-oout", "-hn12", "--log-with-context"});

  EXPECT_THAT(input, Optional("file"s));
  EXPECT_EQ(output, "out");
  EXPECT_EQ(workers, 12);
  EXPECT_EQ(disk, 0);
  EXPECT_TRUE(help);
  EXPECT_THAT(context, Optional(true));
  EXPECT_TRUE(opts.contains("input"));
  EXPECT_FALSE(opts.contains("num-disk-writers"));
}

TEST(command_line, explicit_and_implicit_values) {
  for (auto const& [args, expected] :
       std::vector<std::pair<std::vector<std::string_view>,
                             std::optional<std::string>>>{
           {{}, std::nullopt},
           {{"--input"}, "all"},
           {{"--input="}, ""},
           {{"--input", ""}, ""},
           {{"--input", "file"}, "file"},
           {{"--input=--help"}, "--help"},
           {{"-i--help"}, "--help"},
           {{"--input", "--help"}, "all"},
           {{"--input", "-"}, "-"},
       }) {
    SCOPED_TRACE(fmt::format("args: {}, expected: {}", fmt::join(args, ","),
                             expected ? *expected : "nullopt"));

    cli::options opts;
    std::optional<std::string> input;
    bool help{false};

    opts.add("input,i", input, "input file").implicit_value("all");
    opts.flag("help,h", help, "help");
    opts.parse(args);

    EXPECT_EQ(input, expected);
    EXPECT_EQ(help, args.size() == 2 && args.back() == "--help");
  }
}

TEST(command_line, required_values) {
  for (auto const& args : {std::vector<std::string_view>{"--input"},
                           {"--input", "--help"},
                           {"--input", "-h"},
                           {"--input", "--unknown"},
                           {"--input", "--"}}) {
    SCOPED_TRACE(fmt::format("args: {}", fmt::join(args, ",")));

    cli::options opts;
    std::string input;
    bool help{false};

    opts.add("input,i", input, "input file");
    opts.flag("help,h", help, "help");

    EXPECT_THROW(opts.parse(args), cli::error);
  }

  cli::options opts;
  std::string input;

  opts.add("input,i", input, "input file");

  parse(opts, {"--input=--help"});

  EXPECT_EQ(input, "--help");
}

TEST(command_line, repeated_and_positional) {
  cli::options opts;
  std::vector<std::string> patterns;
  std::vector<std::string> compression;
  bool help{false};

  opts.add_repeated("pattern", patterns, "patterns");
  opts.positional("pattern");
  opts.add_multi("compression,C", compression, "compression");
  opts.flag("help,h", help, "help");

  parse(opts, {"first", "--pattern=second", "-C", "a,b", "lz4", "-Cnone",
               "--help", "third", "--", "--pattern", "-h", "-"});

  EXPECT_THAT(patterns,
              ElementsAre("first", "second", "third", "--pattern", "-h", "-"));
  EXPECT_THAT(compression, ElementsAre("a,b", "lz4", "none"));
  EXPECT_TRUE(help);
}

TEST(command_line, numbers) {
  for (auto const arg : {"-1"sv, "+1"sv, ""sv, "1x"sv, " 1"sv, "1 "sv,
                         "18446744073709551616"sv}) {
    SCOPED_TRACE(fmt::format("arg: {}", arg));

    cli::options opts;
    uint64_t value{};

    opts.add("number,n", value, "number");

    EXPECT_THROW(parse(opts, {"--number", arg}), cli::error);
  }

  cli::options opts;
  int signed_value{};
  uint64_t unsigned_value{};

  opts.add("signed,s", signed_value, "signed number");
  opts.add("unsigned,u", unsigned_value, "unsigned number");

  parse(opts, {"-s", "-42", "--unsigned=18446744073709551615"});

  EXPECT_EQ(signed_value, -42);
  EXPECT_EQ(unsigned_value, std::numeric_limits<uint64_t>::max());
}

TEST(command_line, errors) {
  for (auto const& args :
       std::vector<std::vector<std::string_view>>{{"--num"},
                                                  {"--unknown"},
                                                  {"-z"},
                                                  {"-hh"},
                                                  {"--help=false"},
                                                  {"--help="},
                                                  {"--num-workers", "1", "-n2"},
                                                  {"positional"}}) {
    SCOPED_TRACE(fmt::format("args: {}", fmt::join(args, ",")));

    cli::options opts;
    size_t workers{}, writers{};
    bool help{false};

    opts.add("num-workers,n", workers, "workers");
    opts.add("num-writers", writers, "writers");
    opts.flag("help,h", help, "help");

    EXPECT_THROW(opts.parse(args), cli::error);
  }
}

TEST(command_line, help_width_and_description_ownership) {
  cli::options opts("Basic options");
  size_t count;
  bool verbose{false};
  std::string description = "description from a runtime string that wraps at "
                            "the requested width";

  opts.add("count,c", count, description, 4).value_name("N");
  description.clear();
  opts.group("Advanced options");
  opts.flag("verbose,v", verbose, "enable verbose output");

  for (size_t width : {40, 80, 100}) {
    SCOPED_TRACE(fmt::format("width: {}", width));

    std::ostringstream out;

    opts.help(out, width);

    auto const text = out.str();

    EXPECT_THAT(text, HasSubstr("Basic options:"));
    EXPECT_THAT(text, HasSubstr("Advanced options:"));
    EXPECT_THAT(text, HasSubstr("--count"));
    EXPECT_THAT(text, HasSubstr("(=4)"));
    EXPECT_THAT(text, HasSubstr("description from"));

    std::istringstream lines(text);

    for (std::string line; std::getline(lines, line);) {
      EXPECT_LE(line.size(), width);
    }
  }

  std::ostringstream implicit, explicit_width;

  opts.help(implicit);
  opts.help(explicit_width, 80);

  EXPECT_EQ(implicit.str(), explicit_width.str());
}

TEST(command_line, native_arguments) {
  using namespace dwarfs;
  cli::options opts;
  sys_string input;
  std::vector<std::string> patterns;

  opts.add("input,i", input, "input file");
  opts.add_repeated("pattern", patterns, "patterns");
  opts.positional("pattern");

  auto const path = string_to_sys_string("/dir/\xc3\xa4.txt");
  auto const pattern = string_to_sys_string("**/\xc3\xa4.*");
  auto const executable = string_to_sys_string("dwarfsextract");
  auto const flag = string_to_sys_string("-i");
  sys_char const* argv[]{executable.c_str(), flag.c_str(), path.c_str(),
                         pattern.c_str()};

  opts.parse(4, argv);

  EXPECT_EQ(input, path);
  EXPECT_THAT(patterns, ElementsAre("**/\xc3\xa4.*"));
}

TEST(command_line, scalar_positional) {
  for (auto const& args :
       std::vector<std::vector<std::string_view>>{{"input.dwarfs"},
                                                  {"--", "input.dwarfs"},
                                                  {"--input=input.dwarfs"}}) {
    SCOPED_TRACE(fmt::format("args: {}", fmt::join(args, ",")));

    cli::options opts;
    std::optional<std::string> input;

    opts.add("input,i", input, "input file");
    opts.positional("input");

    opts.parse(args);

    EXPECT_THAT(input, Optional("input.dwarfs"s));
  }

  for (auto const& args : std::vector<std::vector<std::string_view>>{
           {"first", "second"}, {"first", "--input=second"}}) {
    SCOPED_TRACE(fmt::format("args: {}", fmt::join(args, ",")));

    cli::options opts;
    std::string input;

    opts.add("input,i", input, "input file");
    opts.positional("input");

    EXPECT_THROW(opts.parse(args), cli::error);
  }
}

TEST(command_line, filter_arguments) {
  cli::options opts;
  std::vector<std::string> filters;
  std::string input;

  opts.add_multi("filter,F", filters, "filter rules");
  opts.add("input,i", input, "input file");

  parse(opts, {"-F", "- /var/", "-i *.TXT", "+ /home/", "--filter=- /usr/",
               "-i", "/"});

  EXPECT_THAT(filters,
              ElementsAre("- /var/", "-i *.TXT", "+ /home/", "- /usr/"));
  EXPECT_EQ(input, "/");
}

TEST(command_line, grouped_and_hidden_help) {
  cli::options opts("Basic options");
  std::string input, advanced;
  std::vector<std::string> sources;

  opts.add("input,i", input, "input file");
  opts.group("Advanced options");
  opts.add("advanced", advanced, "advanced setting");
  opts.add_repeated("source-path", sources, "source paths").hidden();
  opts.positional("source-path");

  std::ostringstream basic, full;

  opts.help(basic, "Basic options", 60);
  opts.help(full);

  EXPECT_THAT(basic.str(), HasSubstr("--input"));
  EXPECT_THAT(basic.str(), ::testing::Not(HasSubstr("Advanced options:")));
  EXPECT_THAT(basic.str(), ::testing::Not(HasSubstr("--advanced")));
  EXPECT_THAT(full.str(), HasSubstr("Advanced options:"));
  EXPECT_THAT(full.str(), HasSubstr("--advanced"));
  EXPECT_THAT(full.str(), ::testing::Not(HasSubstr("--source-path")));

  parse(opts, {"source", "--source-path=other"});

  EXPECT_THAT(sources, ElementsAre("source", "other"));
}

TEST(command_line, invalid_option_definitions) {
  cli::options opts;
  std::string value;
  bool flag{false};
  std::vector<std::string> values;

  for (auto name : {"", "-input", "in=put", "in put"}) {
    SCOPED_TRACE(name);
    EXPECT_THROW(opts.add(name, value, ""), std::logic_error);
  }

  for (auto name : {"input,", "input,ab", "input,-", "input,="}) {
    SCOPED_TRACE(name);
    EXPECT_THROW(opts.add(name, value, ""), std::logic_error);
  }

  opts.add("input,i", value, "");

  EXPECT_THROW(opts.add("input", value, ""), std::logic_error);
  EXPECT_THROW(opts.add("other,i", value, ""), std::logic_error);
  EXPECT_THROW(opts.flag("flag,f", flag, "").implicit_value("true"),
               std::logic_error);
  EXPECT_THROW(opts.add_multi("values", values, "").implicit_value("x"),
               std::logic_error);
  EXPECT_THROW(opts.positional("unknown"), std::logic_error);
  EXPECT_THROW(opts.positional("flag"), std::logic_error);

  opts.positional("input");
  parse(opts, {"file"});

  EXPECT_EQ(value, "file");
  EXPECT_THROW(opts.add("late", value, ""), std::logic_error);
  EXPECT_THROW(opts.positional("input"), std::logic_error);
  EXPECT_THROW(parse(opts, {}), std::logic_error);
}

TEST(command_line, ambiguous_and_empty_names) {
  {
    cli::options opts;
    bool first{false}, second{false}, third{false};

    opts.flag("number-c", third, "");
    opts.flag("number-a", first, "");
    opts.flag("number-b", second, "");

    EXPECT_THAT([&] { parse(opts, {"--num"}); },
                ThrowsMessage<cli::error>(
                    "option '--num' is ambiguous and matches '--number-a', "
                    "'--number-b', and '--number-c'"));
  }

  {
    cli::options opts;
    std::string value;

    opts.add("input", value, "");

    EXPECT_THAT([&] { parse(opts, {"--=value"}); },
                ThrowsMessage<cli::error>("unrecognised option '--=value'"));
  }
}

TEST(command_line, invalid_typed_values) {
  {
    cli::options opts;
    std::optional<unsigned> value{42};
    opts.add("number", value, "");

    EXPECT_THAT([&] { parse(opts, {"--number=-1"}); },
                ThrowsMessage<cli::error>(HasSubstr("is invalid")));
    EXPECT_THAT(value, Optional(42u));
    EXPECT_FALSE(opts.contains("number"));
  }

  {
    cli::options opts;
    std::vector<int> values;

    opts.add_multi("numbers,n", values, "");

    EXPECT_THAT([&] { parse(opts, {"-n", "bad"}); },
                ThrowsMessage<cli::error>(HasSubstr("is invalid")));
    EXPECT_THAT(values, ::testing::IsEmpty());
  }

  {
    cli::options opts;
    double value{};

    opts.add("number", value, "");
    parse(opts, {"--number", "-.5"});

    EXPECT_DOUBLE_EQ(value, -0.5);
  }
}

TEST(command_line, help_empty_group_and_description) {
  cli::options opts("");
  std::string value;
  bool flag{false};

  opts.flag("flag", flag, "");
  opts.add("value", value, "first line\nsecond line \t")
      .value_name("TEXT")
      .implicit_value("implicit");

  std::ostringstream out;
  opts.help(out, 80);

  EXPECT_THAT(out.str(), ::testing::AllOf(HasSubstr("  --flag\n"),
                                          HasSubstr("[=TEXT(=implicit)]"),
                                          HasSubstr("first line\n"),
                                          HasSubstr("second line\n")));
  EXPECT_THAT(out.str(), ::testing::Not(HasSubstr("\t")));
}

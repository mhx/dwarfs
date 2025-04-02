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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/glob_matcher.h>

using dwarfs::glob_matcher;

TEST(glob_matcher_test, simple_patterns) {
  std::vector<std::string> patterns = {"*.cpp", "*.h"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("main.cpp"));
  EXPECT_TRUE(matcher("utils.h"));
  EXPECT_FALSE(matcher("README.md"));
}

TEST(glob_matcher_test, brace_expansion) {
  std::vector<std::string> patterns = {"{README,CONTRIBUTING,LICENSE}.md"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("README.md"));
  EXPECT_TRUE(matcher("CONTRIBUTING.md"));
  EXPECT_TRUE(matcher("LICENSE.md"));
  EXPECT_FALSE(matcher("INSTALL.md"));
}

TEST(glob_matcher_test, nested_brace_expansion) {
  std::vector<std::string> patterns = {"file{1,{2,3}}.txt"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("file1.txt"));
  EXPECT_TRUE(matcher("file2.txt"));
  EXPECT_TRUE(matcher("file3.txt"));
  EXPECT_FALSE(matcher("file4.txt"));
}

TEST(glob_matcher_test, single_character_wildcard) {
  std::vector<std::string> patterns = {"data?.csv"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("data1.csv"));
  EXPECT_TRUE(matcher("dataA.csv"));
  EXPECT_FALSE(matcher("data10.csv"));
  EXPECT_FALSE(matcher("data.csv"));
}

TEST(glob_matcher_test, character_class) {
  std::vector<std::string> patterns = {"log[0-9].txt"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("log0.txt"));
  EXPECT_TRUE(matcher("log5.txt"));
  EXPECT_FALSE(matcher("log10.txt"));
  EXPECT_FALSE(matcher("logA.txt"));
}

TEST(glob_matcher_test, negated_character_class) {
  std::vector<std::string> patterns = {"log[!0-9].txt"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("logA.txt"));
  EXPECT_TRUE(matcher("log_.txt"));
  EXPECT_FALSE(matcher("log0.txt"));
  EXPECT_FALSE(matcher("log5.txt"));
}

TEST(glob_matcher_test, globstar) {
  std::vector<std::string> patterns = {"src/**/main.cpp"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("src/main.cpp"));
  EXPECT_TRUE(matcher("src/utils/main.cpp"));
  EXPECT_TRUE(matcher("src/utils/helpers/main.cpp"));
  EXPECT_FALSE(matcher("main.cpp"));
  EXPECT_FALSE(matcher("src/main.c"));
}

TEST(glob_matcher_test, globstar_at_start) {
  std::vector<std::string> patterns = {"**/test.cpp"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("test.cpp"));
  EXPECT_TRUE(matcher("src/test.cpp"));
  EXPECT_TRUE(matcher("src/utils/test.cpp"));
  EXPECT_FALSE(matcher("test.c"));
}

TEST(glob_matcher_test, globstar_at_end) {
  std::vector<std::string> patterns = {"src/**"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("src/"));
  EXPECT_TRUE(matcher("src/main.cpp"));
  EXPECT_TRUE(matcher("src/utils/helper.hpp"));
  EXPECT_FALSE(matcher("include/main.hpp"));
}

TEST(glob_matcher_test, complex_patterns) {
  std::vector<std::string> patterns = {"build/{debug,release}/**/*.o",
                                       "logs/**/*.log", "**/*.{png,jpg,jpeg}"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("build/debug/main.o"));
  EXPECT_TRUE(matcher("build/release/utils/helper.o"));
  EXPECT_FALSE(matcher("build/profile/main.o"));

  EXPECT_TRUE(matcher("logs/app.log"));
  EXPECT_TRUE(matcher("logs/2021/01/01/system.log"));
  EXPECT_FALSE(matcher("logs/app.txt"));

  EXPECT_TRUE(matcher("image.png"));
  EXPECT_TRUE(matcher("assets/images/photo.jpg"));
  EXPECT_TRUE(matcher("screenshots/test.jpeg"));
  EXPECT_FALSE(matcher("document.pdf"));
}

TEST(glob_matcher_test, edge_cases) {
  // Character class edge cases
  {
    glob_matcher matcher{"[][!]"};
    for (char c : {'[', ']', '!'}) {
      EXPECT_TRUE(matcher(c));
    }
    for (char c : {'a', 'b', 'c'}) {
      EXPECT_FALSE(matcher(c));
    }
  }

  {
    glob_matcher matcher{"[]-]"};
    for (char c : {']', '-'}) {
      EXPECT_TRUE(matcher(c));
    }
    for (char c : {'[', '/', 'a'}) {
      EXPECT_FALSE(matcher(c));
    }
  }

  {
    glob_matcher matcher{"[,----0]"};
    for (char c : {',', '-', '.', '0'}) {
      EXPECT_TRUE(matcher(c));
    }
    for (char c : {'[', '/', 'a'}) {
      EXPECT_FALSE(matcher(c));
    }
  }

  // Invalid / in character class
  EXPECT_THAT(
      [] { glob_matcher{"foo[a/b]"}; },
      testing::ThrowsMessage<std::runtime_error>(
          "invalid character '/' in character class in pattern: foo[a/b]"));

  // Unmatched brace
  EXPECT_THAT([] { glob_matcher{"file{1,2.txt"}; },
              testing::ThrowsMessage<std::runtime_error>(
                  "unmatched '{' in pattern: file{1,2.txt"));
  EXPECT_THAT([] { glob_matcher{"file{1,2.txt}3}"}; },
              testing::ThrowsMessage<std::runtime_error>(
                  "unmatched '}' in pattern: file{1,2.txt}3}"));

  // Unmatched bracket
  EXPECT_THAT([] { glob_matcher{"file[1-2.txt"}; },
              testing::ThrowsMessage<std::runtime_error>(
                  "unmatched '[' in pattern: file[1-2.txt"));
  EXPECT_THAT([] { glob_matcher{"file[1-2]].txt"}; },
              testing::ThrowsMessage<std::runtime_error>(
                  "unmatched ']' in pattern: file[1-2]].txt"));

  // Trailing backslash
  EXPECT_THAT([] { glob_matcher{"file.txt\\"}; },
              testing::ThrowsMessage<std::runtime_error>(
                  "trailing backslash in pattern: file.txt\\"));

  // Patterns that should match files in the root directory only
  std::vector<std::string> root_patterns = {"/*.txt"};
  glob_matcher matcher(root_patterns);

  EXPECT_TRUE(matcher("/file.txt"));
  EXPECT_FALSE(matcher("/dir/file.txt"));
  EXPECT_FALSE(matcher("file.txt"));
}

TEST(glob_matcher_test, escaped_characters) {
  std::vector<std::string> patterns = {"data\\*.csv"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("data*.csv"));
  EXPECT_FALSE(matcher("data123.csv"));
}

TEST(glob_matcher_test, literal_dots) {
  std::vector<std::string> patterns = {".*rc"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher(".bashrc"));
  EXPECT_TRUE(matcher(".vimrc"));
  EXPECT_FALSE(matcher("myrc"));
}

TEST(glob_matcher_test, multiple_patterns) {
  std::vector<std::string> patterns = {"*.cpp", "src/**/test{1,2}.cpp",
                                       "include/*.{h,hpp}", "docs/README.md"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("main.cpp"));
  EXPECT_TRUE(matcher("src/test1.cpp"));
  EXPECT_TRUE(matcher("src/utils/test2.cpp"));
  EXPECT_TRUE(matcher("include/main.h"));
  EXPECT_TRUE(matcher("docs/README.md"));
  EXPECT_FALSE(matcher("include/utils/helper.hpp"));
  EXPECT_FALSE(matcher("main.c"));
  EXPECT_FALSE(matcher("docs/CONTRIBUTING.md"));
}

TEST(glob_matcher_test, hidden_files) {
  std::vector<std::string> patterns = {".*"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher(".gitignore"));
  EXPECT_TRUE(matcher(".env"));
  EXPECT_FALSE(matcher("README.md"));
}

TEST(glob_matcher_test, directory_patterns) {
  std::vector<std::string> patterns = {"*/", "src/*/", "docs/**/"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("bin/"));
  EXPECT_TRUE(matcher("src/utils/"));
  EXPECT_TRUE(matcher("docs/"));
  EXPECT_TRUE(matcher("docs/guides/"));
  EXPECT_FALSE(matcher("README.md"));
  EXPECT_FALSE(matcher("src/main.cpp"));
}

TEST(glob_matcher_test, escaped_braces) {
  std::vector<std::string> patterns = {"src/\\{test\\}.cpp",
                                       "data/\\{2020,2021\\}/report.txt",
                                       "docs/\\{README\\}.md"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("src/{test}.cpp"));
  EXPECT_TRUE(matcher("data/{2020,2021}/report.txt"));
  EXPECT_TRUE(matcher("docs/{README}.md"));
  EXPECT_FALSE(matcher("src/test.cpp"));
  EXPECT_FALSE(matcher("data/2020/report.txt"));
}

TEST(glob_matcher_test, mixed_escaped_and_unescaped_braces) {
  std::vector<std::string> patterns = {"src/{test,prod}/\\{config\\}.json"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("src/test/{config}.json"));
  EXPECT_TRUE(matcher("src/prod/{config}.json"));
  EXPECT_FALSE(matcher("src/test/config.json"));
  EXPECT_FALSE(matcher("src/{test}/config.json"));
}

TEST(glob_matcher_test, escaped_commas_in_braces) {
  std::vector<std::string> patterns = {"file{one\\,two,three}.txt"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("fileone,two.txt"));
  EXPECT_TRUE(matcher("filethree.txt"));
  EXPECT_FALSE(matcher("fileonetwo.txt"));
}

TEST(glob_matcher_test, escaped_characters_in_braces) {
  std::vector<std::string> patterns = {"dir/{sub\\{dir\\},other}"};
  glob_matcher matcher(patterns);

  EXPECT_TRUE(matcher("dir/sub{dir}"));
  EXPECT_TRUE(matcher("dir/other"));
  EXPECT_FALSE(matcher("dir/subdir"));
}

TEST(glob_matcher_test, python_fnmatch) {
  EXPECT_TRUE(glob_matcher{"abc"}("abc"));
  EXPECT_TRUE(glob_matcher{"?*?"}("abc"));
  EXPECT_TRUE(glob_matcher{"???*"}("abc"));
  EXPECT_TRUE(glob_matcher{"*???"}("abc"));
  EXPECT_TRUE(glob_matcher{"???"}("abc"));
  EXPECT_TRUE(glob_matcher{"*"}("abc"));
  EXPECT_TRUE(glob_matcher{"ab[cd]"}("abc"));
  EXPECT_TRUE(glob_matcher{"ab[!de]"}("abc"));
  EXPECT_FALSE(glob_matcher{"ab[de]"}("abc"));
  EXPECT_FALSE(glob_matcher{"??"}("a"));
  EXPECT_FALSE(glob_matcher{"b"}("a"));
  EXPECT_TRUE(glob_matcher{"[\\]"}("\\"));
  EXPECT_TRUE(glob_matcher{"[!\\]"}("a"));
  EXPECT_FALSE(glob_matcher{"[!\\]"}("\\"));
  EXPECT_TRUE(glob_matcher{"foo*"}("foo\nbar"));
  EXPECT_TRUE(glob_matcher{"foo*"}("foo\nbar\n"));
  EXPECT_FALSE(glob_matcher{"foo*"}("\nfoo"));
  EXPECT_TRUE(glob_matcher{"*"}("\n"));
}

TEST(glob_matcher_test, python_case) {
  EXPECT_TRUE(glob_matcher{"abc"}("abc"));
  EXPECT_TRUE(glob_matcher{":abc"}("abc"));
  EXPECT_FALSE(glob_matcher{"AbC"}("abc"));
  EXPECT_TRUE(glob_matcher({"AbC"}, {.ignorecase = true})("abc"));
  EXPECT_TRUE(glob_matcher{"i:AbC"}("abc"));
  EXPECT_FALSE(glob_matcher{"abc"}("AbC"));
  EXPECT_TRUE(glob_matcher({"abc"}, {.ignorecase = true})("AbC"));
  EXPECT_TRUE(glob_matcher{"i:abc"}("AbC"));
  EXPECT_TRUE(glob_matcher{"AbC"}("AbC"));
  EXPECT_TRUE(glob_matcher{":AbC"}("AbC"));
}

TEST(glob_matcher_test, python_char_set) {
  static constexpr std::string_view testcases =
      R"(abcdefghijklmnopqrstuvwxyz0123456789!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";
  static constexpr std::string_view uppercase = R"(ABCDEFGHIJKLMNOPQRSTUVWXYZ)";
  using namespace std::literals;

  for (char c : testcases) {
    glob_matcher positive{"[az]"};
    glob_matcher negative{"[!az]"};
    if (c == 'a' || c == 'z') {
      EXPECT_TRUE(positive(c));
      EXPECT_FALSE(negative(c));
    } else {
      EXPECT_FALSE(positive(c));
      EXPECT_TRUE(negative(c));
    }
  }

  for (char c : testcases) {
    EXPECT_EQ("az"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[az]"}(c));
    EXPECT_EQ("az"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[AZ]"}(c));
    EXPECT_EQ("az"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!az]"}(c));
    EXPECT_EQ("az"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!AZ]"}(c));
  }

  for (char c : uppercase) {
    EXPECT_EQ("AZ"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[az]"}(c));
    EXPECT_EQ("AZ"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[AZ]"}(c));
    EXPECT_EQ("AZ"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!az]"}(c));
    EXPECT_EQ("AZ"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!AZ]"}(c));
  }

  for (char c : testcases) {
    glob_matcher matcher{"[aa]"};
    if (c == 'a') {
      EXPECT_TRUE(matcher(c));
    } else {
      EXPECT_FALSE(matcher(c));
    }
  }

  for (char c : testcases) {
    EXPECT_EQ(c == '^' || c == 'a' || c == 'z', glob_matcher{"[^az]"}(c));
    EXPECT_EQ(c == '[' || c == 'a' || c == 'z', glob_matcher{"[[az]"}(c));
    EXPECT_EQ(c != ']', glob_matcher{"[!]]"}(c));
  }
}

TEST(glob_matcher_test, python_range) {
  static constexpr std::string_view testcases =
      R"(abcdefghijklmnopqrstuvwxyz0123456789!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";
  static constexpr std::string_view uppercase = R"(ABCDEFGHIJKLMNOPQRSTUVWXYZ)";
  using namespace std::literals;

  for (char c : testcases) {
    EXPECT_EQ("bcd"sv.find(c) != std::string_view::npos,
              glob_matcher{"[b-d]"}(c));
    EXPECT_EQ("bcd"sv.find(c) == std::string_view::npos,
              glob_matcher{"[!b-d]"}(c));
    EXPECT_EQ("bcdxyz"sv.find(c) != std::string_view::npos,
              glob_matcher{"[b-dx-z]"}(c));
    EXPECT_EQ("bcdxyz"sv.find(c) == std::string_view::npos,
              glob_matcher{"[!b-dx-z]"}(c));
  }

  for (char c : testcases) {
    EXPECT_EQ("bcd"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[B-D]"}(c));
    EXPECT_EQ("bcd"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!B-D]"}(c));
  }

  for (char c : uppercase) {
    EXPECT_EQ("BCD"sv.find(c) != std::string_view::npos,
              glob_matcher{"i:[b-d]"}(c));
    EXPECT_EQ("BCD"sv.find(c) == std::string_view::npos,
              glob_matcher{"i:[!b-d]"}(c));
  }

  for (char c : testcases) {
    EXPECT_EQ(c == 'b', glob_matcher{"[b-b]"}(c));
  }

  for (char c : testcases) {
    EXPECT_EQ(c != '-' && c != '#', glob_matcher{"[!-#]"}(c));
    EXPECT_EQ(c != '-' && c != '.', glob_matcher{"[!--.]"}(c));
    EXPECT_EQ(c == '^' || c == '_' || c == '`', glob_matcher{"[^-`]"}(c));
    EXPECT_EQ(c == '[' || c == '\\' || c == ']' || c == '^',
              glob_matcher{"[[-^]"}(c))
        << c;
    EXPECT_EQ(c == '\\' || c == ']' || c == '^', glob_matcher{R"([\-^])"}(c));
    EXPECT_EQ(c == '-' || c == 'b', glob_matcher{"[-b]"}(c));
    EXPECT_EQ(c != '-' && c != 'b', glob_matcher{"[!-b]"}(c));
    EXPECT_EQ(c == '-' || c == 'b', glob_matcher{"[-b]"}(c));
    EXPECT_EQ(c != '-' && c != 'b', glob_matcher{"[!-b]"}(c));
    EXPECT_EQ(c == '-', glob_matcher{"[-]"}(c));
    EXPECT_EQ(c != '-', glob_matcher{"[!-]"}(c));
  }

  EXPECT_THAT([] { glob_matcher{"[d-b]"}('a'); },
              testing::ThrowsMessage<std::runtime_error>(
                  "invalid range 'd-b' in character class in pattern: [d-b]"));
}

TEST(glob_matcher_test, multi_pattern) {
  glob_matcher matcher;
  matcher.add_pattern("*.cpp");
  matcher.add_pattern("*.txt", {.ignorecase = true});

  EXPECT_TRUE(matcher("main.cpp"));
  EXPECT_TRUE(matcher("README.txt"));
  EXPECT_TRUE(matcher("CHANGELOG.TXT"));
  EXPECT_FALSE(matcher("main.c"));
  EXPECT_FALSE(matcher("UTILS.CPP"));
}

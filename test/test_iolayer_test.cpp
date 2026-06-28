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

#include <atomic>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/compiler.h>
#include <dwarfs/file_access_generic.h>
#include <dwarfs/file_util.h>
#include <dwarfs/terminal_ansi.h>

#include "test_helpers.h"

namespace fs = std::filesystem;

using namespace dwarfs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Property;
using ::testing::Throws;

namespace {

// Read everything from the current get position to EOF. Reads straight from the
// streambuf, so it is unaffected by eofbit/failbit already set on the iostream.
std::string read_all(std::iostream& ios) {
  std::ostringstream oss;
  oss << ios.rdbuf();
  return oss.str();
}

struct test_fa_traits {
  std::shared_ptr<test::test_file_access> tfa =
      std::make_shared<test::test_file_access>();

  std::shared_ptr<file_access const> fa() const { return tfa; }

  fs::path path(std::string const& name) const {
    return fs::path{"/test"} / name;
  }

  void seed(std::string const& name, std::string content) const {
    tfa->set_file(path(name), std::move(content));
  }

  std::optional<std::string> stored(std::string const& name) const {
    return tfa->get_file(path(name));
  }
};

struct real_fa_traits {
  temporary_directory td_{"dwarfs"};
  std::shared_ptr<file_access const> fa_;

  real_fa_traits() { fa_ = create_file_access_generic(); }

  std::shared_ptr<file_access const> fa() const { return fa_; }

  fs::path path(std::string const& name) const { return td_.path() / name; }

  void seed(std::string const& name, std::string content) const {
    std::ofstream os(path(name), std::ios::binary | std::ios::trunc);
    os << content;
  }

  std::optional<std::string> stored(std::string const& name) const {
    std::ifstream is(path(name), std::ios::binary);
    if (!is) {
      return std::nullopt;
    }
    std::ostringstream ss;
    ss << is.rdbuf();
    return ss.str();
  }
};

template <typename Traits>
class open_iostream_test : public ::testing::Test {
 protected:
  Traits t_;

  std::shared_ptr<file_access const> fa() { return t_.fa(); }
  fs::path path(std::string const& name) { return t_.path(name); }
  void seed(std::string const& name, std::string content) {
    t_.seed(name, std::move(content));
  }
  std::optional<std::string> stored(std::string const& name) {
    return t_.stored(name);
  }
};

} // namespace

TEST(test_iolayer, file_access) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;

  tfa->set_file("/test/file1", "Hello World!\n");
  tfa->set_file("/test/error", "something");
  tfa->set_open_error("/test/error", std::make_error_code(std::errc::io_error));
  tfa->set_close_error(
      "/test/file1", std::make_error_code(std::errc::device_or_resource_busy));
  tfa->set_close_error("/test/file3",
                       std::make_error_code(std::errc::bad_address));
  tfa->set_open_error("/test/file4",
                      std::make_error_code(std::errc::bad_file_descriptor));

  EXPECT_THAT(
      [&] { fa->open_input_binary("/test/does_not_exist"); },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));

  EXPECT_THAT(
      [&] { fa->open_output_binary(fs::path{}); },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));

  EXPECT_THAT([&] { fa->open_input("/test/error"); },
              Throws<std::system_error>(
                  Property(&std::system_error::code,
                           Eq(std::make_error_code(std::errc::io_error)))));

  EXPECT_THAT([&] { fa->open_output("/test/file4"); },
              Throws<std::system_error>(Property(
                  &std::system_error::code,
                  Eq(std::make_error_code(std::errc::bad_file_descriptor)))));

  {
    auto in = fa->open_input_binary("/test/file1");
    std::vector<std::string> lines;
    while (in->is().good()) {
      std::string line;
      std::getline(in->is(), line);
      lines.push_back(line);
    }
    std::error_code ec;
    in->close(ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec, std::make_error_code(std::errc::device_or_resource_busy));
    EXPECT_THAT(lines, ElementsAre("Hello World!", ""));
  }

  {
    auto out = fa->open_output("/test/file2");
    out->os() << "Line 1\nLine 2\n";
    out->close(); // close again, should be fine
  }

  auto f2 = tfa->get_file("/test/file2");
  ASSERT_TRUE(f2.has_value());
  EXPECT_EQ(*f2, "Line 1\nLine 2\n");

  {
    auto out = fa->open_output("/test/file3");
    EXPECT_THAT([&] { out->close(); },
                Throws<std::system_error>(Property(
                    &std::system_error::code,
                    Eq(std::make_error_code(std::errc::bad_address)))));
  }
}

TEST(test_iolayer, test_terminal) {
  test::test_terminal term;

  EXPECT_EQ("<bg-normal>", term.bgcolor(termcolor::NORMAL));
  EXPECT_EQ("<bg-dim-white>", term.bgcolor(termcolor::DIM_WHITE));
}

TEST(test_iolayer, use_real_terminal) {
  {
    test::test_iolayer io;
    auto const& iol = io.get();
    auto const& term = *iol.term;
    EXPECT_EQ(typeid(term), typeid(test::test_terminal));
  }

  {
    test::test_iolayer io;
    io.use_real_terminal(true);
    auto const& iol = io.get();
    auto const& term = *iol.term;
    EXPECT_EQ(typeid(term), typeid(terminal_ansi));
  }
}

// Expected behavior follows C++23 [filebuf.members]:
//   out / out|trunc      -> "w"    creates, truncates
//   out|app / app        -> "a"    creates, writes always at end
//   in                   -> "r"    must exist
//   in|out               -> "r+"   must exist, no truncation
//   in|out|trunc         -> "w+"   creates, truncates, read+write
//   in|out|app / in|app  -> "a+"   creates, no truncation, writes at end
//   |noreplace           -> exclusive create (fails if file exists)
//   |ate                 -> initial position at end, but still seekable

using fa_impl_types = ::testing::Types<test_fa_traits, real_fa_traits>;
TYPED_TEST_SUITE(open_iostream_test, fa_impl_types);

// --- in (r): read existing, must exist -------------------------------------

TYPED_TEST(open_iostream_test, in_reads_existing_content) {
  this->seed("f", "Hello World!\n");
  auto s = this->fa()->open(this->path("f"), std::ios::in);
  ASSERT_TRUE(s);
  EXPECT_EQ(read_all(s->ios()), "Hello World!\n");
}

TYPED_TEST(open_iostream_test, in_missing_file_throws) {
  EXPECT_THAT(
      [&] { this->fa()->open(this->path("nope"), std::ios::in); },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));
}

TYPED_TEST(open_iostream_test, in_missing_file_ec) {
  std::error_code ec;
  auto s = this->fa()->open(this->path("nope"), std::ios::in, ec);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(ec, std::make_error_code(std::errc::no_such_file_or_directory));
}

// --- in|out (r+): must exist, no truncation, in-place overwrite -------------

TYPED_TEST(open_iostream_test, in_out_requires_existing_file) {
  EXPECT_THAT(
      [&] {
        this->fa()->open(this->path("nope"), std::ios::in | std::ios::out);
      },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));
}

TYPED_TEST(open_iostream_test, in_out_overwrites_in_place_without_truncating) {
  this->seed("f", "0123456789");
  auto s = this->fa()->open(this->path("f"), std::ios::in | std::ios::out);
  ASSERT_TRUE(s);
  s->ios().seekp(5);
  s->ios() << "ABC";
  s->close();
  EXPECT_EQ(this->stored("f"), "01234ABC89");
}

// --- out (w): create / truncate --------------------------------------------

TYPED_TEST(open_iostream_test, out_truncates_existing) {
  this->seed("f", "0123456789");
  auto s = this->fa()->open(this->path("f"), std::ios::out);
  ASSERT_TRUE(s);
  s->ios() << "AB";
  s->close();
  EXPECT_EQ(this->stored("f"), "AB");
}

TYPED_TEST(open_iostream_test, out_creates_missing) {
  auto s = this->fa()->open(this->path("new"), std::ios::out);
  ASSERT_TRUE(s);
  s->ios() << "data";
  s->close();
  EXPECT_EQ(this->stored("new"), "data");
}

// --- in|out|trunc (w+): create / truncate, read+write ----------------------

TYPED_TEST(open_iostream_test, in_out_trunc_truncates_and_reads_back) {
  this->seed("f", "old-content");
  auto s = this->fa()->open(this->path("f"),
                            std::ios::in | std::ios::out | std::ios::trunc);
  ASSERT_TRUE(s);
  s->ios() << "new";
  s->ios().seekg(0); // seek is the required output->input transition
  EXPECT_EQ(read_all(s->ios()), "new");
  s->close();
  EXPECT_EQ(this->stored("f"), "new");
}

// --- app (a) / in|out|app (a+): writes always at end -----------------------

TYPED_TEST(open_iostream_test, app_writes_at_end_ignoring_seek) {
  this->seed("f", "AAA");
  auto s = this->fa()->open(this->path("f"), std::ios::out | std::ios::app);
  ASSERT_TRUE(s);
  s->ios().seekp(0); // ignored: append always writes at end
  s->ios() << "BBB";
  s->close();
  EXPECT_EQ(this->stored("f"), "AAABBB");
}

TYPED_TEST(open_iostream_test, app_creates_missing) {
  auto s = this->fa()->open(this->path("new"), std::ios::out | std::ios::app);
  ASSERT_TRUE(s);
  s->ios() << "X";
  s->close();
  EXPECT_EQ(this->stored("new"), "X");
}

TYPED_TEST(open_iostream_test, in_out_app_preserves_and_reads_existing) {
  // a+ must not truncate and must expose existing content for reading.
  this->seed("f", "AAA");
  auto s = this->fa()->open(this->path("f"),
                            std::ios::in | std::ios::out | std::ios::app);
  ASSERT_TRUE(s);
  s->ios().seekg(0);
  EXPECT_EQ(read_all(s->ios()), "AAA");
  s->close();
  EXPECT_EQ(this->stored("f"), "AAA"); // unchanged: no truncation
}

// --- ate: start at end, but seekable (distinct from app) -------------------

TYPED_TEST(open_iostream_test, ate_starts_at_end) {
  this->seed("f", "AAA");
  auto s = this->fa()->open(this->path("f"),
                            std::ios::in | std::ios::out | std::ios::ate);
  ASSERT_TRUE(s);
  s->ios() << "BBB"; // initial position is end-of-file
  s->close();
  EXPECT_EQ(this->stored("f"), "AAABBB");
}

TYPED_TEST(open_iostream_test, ate_honours_seek_unlike_app) {
  this->seed("f", "AAABBB");
  auto s = this->fa()->open(this->path("f"),
                            std::ios::in | std::ios::out | std::ios::ate);
  ASSERT_TRUE(s);
  s->ios().seekp(0); // honoured (would be ignored under app)
  s->ios() << "X";
  s->close();
  EXPECT_EQ(this->stored("f"), "XAABBB");
}

// --- noreplace (C++23): exclusive create -----------------------------------

#if DWARFS_HAVE_IOS_NOREPLACE
TYPED_TEST(open_iostream_test, noreplace_creates_when_absent) {
  auto s =
      this->fa()->open(this->path("new"), std::ios::out | std::ios::noreplace);
  ASSERT_TRUE(s);
  s->ios() << "data";
  s->close();
  EXPECT_EQ(this->stored("new"), "data");
}

TYPED_TEST(open_iostream_test, noreplace_fails_when_present_throws) {
  this->seed("f", "exists");
  EXPECT_THAT(
      [&] {
        this->fa()->open(this->path("f"), std::ios::out | std::ios::noreplace);
      },
      Throws<std::system_error>(
          Property(&std::system_error::code,
                   Eq(std::make_error_code(std::errc::file_exists)))));
  EXPECT_EQ(this->stored("f"), "exists");
}

TYPED_TEST(open_iostream_test, noreplace_fails_when_present_ec) {
  this->seed("f", "exists");
  std::error_code ec;
  auto s = this->fa()->open(this->path("f"),
                            std::ios::out | std::ios::noreplace, ec);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(ec, std::make_error_code(std::errc::file_exists));
  EXPECT_EQ(this->stored("f"), "exists");
}
#else
TEST(test_iolayer, noreplace_unsupported_by_stdlib) {
  GTEST_SKIP() << "std::ios::noreplace unavailable";
}
#endif

TYPED_TEST(open_iostream_test, empty_path_throws) {
  EXPECT_THAT(
      [&] { this->fa()->open(fs::path{}, std::ios::in | std::ios::out); },
      Throws<std::system_error>(Property(
          &std::system_error::code,
          Eq(std::make_error_code(std::errc::no_such_file_or_directory)))));
}

TYPED_TEST(open_iostream_test, empty_path_ec) {
  std::error_code ec;
  auto s = this->fa()->open(fs::path{}, std::ios::in | std::ios::out, ec);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(ec, std::make_error_code(std::errc::no_such_file_or_directory));
}

// --- ec overload success path ----------------------------------------------

TYPED_TEST(open_iostream_test, ec_overload_clears_ec_on_success) {
  this->seed("f", "hello");
  std::error_code ec = std::make_error_code(std::errc::io_error);
  auto s = this->fa()->open(this->path("f"), std::ios::in, ec);
  ASSERT_TRUE(s);
  EXPECT_FALSE(ec);
  EXPECT_EQ(read_all(s->ios()), "hello");
}

TEST(test_iolayer, open_iostream_writes_visible_only_after_close) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;

  auto s = fa->open("/test/f", std::ios::out);
  ASSERT_TRUE(s);
  s->ios() << "data";
  EXPECT_FALSE(tfa->get_file("/test/f").has_value());
  s->close();
  ASSERT_TRUE(tfa->get_file("/test/f").has_value());
  EXPECT_EQ(*tfa->get_file("/test/f"), "data");
}

TEST(test_iolayer, open_iostream_close_error_throws) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;
  tfa->set_close_error("/test/f", std::make_error_code(std::errc::io_error));

  auto s = fa->open("/test/f", std::ios::out);
  ASSERT_TRUE(s);
  s->ios() << "data";
  EXPECT_THAT([&] { s->close(); },
              Throws<std::system_error>(
                  Property(&std::system_error::code,
                           Eq(std::make_error_code(std::errc::io_error)))));
}

TEST(test_iolayer, open_iostream_close_error_ec) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;
  tfa->set_close_error("/test/f", std::make_error_code(std::errc::io_error));

  auto s = fa->open("/test/f", std::ios::out);
  ASSERT_TRUE(s);
  s->ios() << "data";
  std::error_code ec;
  s->close(ec);
  EXPECT_TRUE(ec);
  EXPECT_EQ(ec, std::make_error_code(std::errc::io_error));
}

TEST(test_iolayer, open_iostream_close_failure_leaves_store_unchanged) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;
  tfa->set_file("/test/f", "original");
  tfa->set_close_error("/test/f", std::make_error_code(std::errc::io_error));

  auto s = fa->open("/test/f", std::ios::in | std::ios::out);
  ASSERT_TRUE(s);
  s->ios().seekp(0);
  s->ios() << "OVERWRITE";
  std::error_code ec;
  s->close(ec);
  ASSERT_TRUE(ec);
  EXPECT_EQ(tfa->get_file("/test/f"), "original");
}

TEST(test_iolayer, open_iostream_open_error_injection) {
  auto tfa = std::make_shared<test::test_file_access>();
  std::shared_ptr<file_access> fa = tfa;
  tfa->set_file("/test/f", "x");
  tfa->set_open_error("/test/f",
                      std::make_error_code(std::errc::permission_denied));

  EXPECT_THAT([&] { fa->open("/test/f", std::ios::in); },
              Throws<std::system_error>(Property(
                  &std::system_error::code,
                  Eq(std::make_error_code(std::errc::permission_denied)))));

  std::error_code ec;
  auto s = fa->open("/test/f", std::ios::in, ec);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(ec, std::make_error_code(std::errc::permission_denied));
}

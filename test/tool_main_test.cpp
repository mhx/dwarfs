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

#include <array>
#include <filesystem>
#include <iostream>
#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include <folly/json.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";
auto test_data_image = test_dir / "data.dwarfs";

enum class input_mode {
  from_file,
  from_stdin,
};

constexpr std::array<input_mode, 2> const input_modes = {
    input_mode::from_file, input_mode::from_stdin};

std::ostream& operator<<(std::ostream& os, input_mode m) {
  switch (m) {
  case input_mode::from_file:
    os << "from_file";
    break;
  case input_mode::from_stdin:
    os << "from_stdin";
    break;
  }
  return os;
}

struct locale_setup_helper {
  locale_setup_helper() { setup_default_locale(); }
};

void setup_locale() { static locale_setup_helper helper; }

class tool_main_test : public testing::Test {
 public:
  void SetUp() override {
    setup_locale();
    iol = std::make_unique<test::test_iolayer>();
  }

  void TearDown() override { iol.reset(); }

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::unique_ptr<test::test_iolayer> iol;
};

class mkdwarfs_tester {
 public:
  mkdwarfs_tester(std::shared_ptr<test::os_access_mock> pos)
      : fa{std::make_shared<test::test_file_access>()}
      , os{std::move(pos)}
      , iol{os, fa} {
    setup_locale();
  }

  mkdwarfs_tester()
      : mkdwarfs_tester(test::os_access_mock::create_test_instance()) {}

  static mkdwarfs_tester create_empty() {
    return mkdwarfs_tester(std::make_shared<test::os_access_mock>());
  }

  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "mkdwarfs");
    return mkdwarfs_main(args, iol.get());
  }

  int run(std::initializer_list<std::string> args) {
    return run(std::vector<std::string>(args));
  }

  int run(std::string args) { return run(test::parse_args(args)); }

  filesystem_v2 fs_from_data(std::string data) {
    auto mm = std::make_shared<test::mmap_mock>(std::move(data));
    return filesystem_v2(lgr, mm);
  }

  filesystem_v2 fs_from_file(std::string path) {
    auto fsimage = fa->get_file(path);
    if (!fsimage) {
      throw std::runtime_error("file not found: " + path);
    }
    return fs_from_data(std::move(fsimage.value()));
  }

  std::string out() const { return iol.out(); }
  std::string err() const { return iol.err(); }

  std::shared_ptr<test::test_file_access> fa;
  std::shared_ptr<test::os_access_mock> os;
  test::test_iolayer iol;
  test::test_logger lgr;
};

std::optional<filesystem_v2>
build_with_args(std::vector<std::string> opt_args = {}) {
  std::string const image_file = "test.dwarfs";
  mkdwarfs_tester t;
  std::vector<std::string> args = {"-i", "/", "-o", image_file};
  args.insert(args.end(), opt_args.begin(), opt_args.end());
  if (t.run(args) != 0) {
    return std::nullopt;
  }
  return t.fs_from_file(image_file);
}

std::set<uint64_t> get_all_fs_times(filesystem_v2 const& fs) {
  std::set<uint64_t> times;
  fs.walk([&](auto const& e) {
    file_stat st;
    fs.getattr(e.inode(), &st);
    times.insert(st.atime);
    times.insert(st.ctime);
    times.insert(st.mtime);
  });
  return times;
}

} // namespace

class mkdwarfs_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "mkdwarfs");
    return mkdwarfs_main(args, iol->get());
  }
};

class dwarfsck_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsck");
    return dwarfsck_main(args, iol->get());
  }
};

class dwarfsextract_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsextract");
    return dwarfsextract_main(args, iol->get());
  }
};

TEST_F(mkdwarfs_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsck_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsextract_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(mkdwarfs_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsck_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsextract_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(mkdwarfs_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--long-help"));
  // check that the detailed help is not shown
  EXPECT_THAT(out(), ::testing::Not(::testing::HasSubstr("Advanced options:")));
  EXPECT_THAT(out(),
              ::testing::Not(::testing::HasSubstr("Compression algorithms:")));
}

TEST_F(mkdwarfs_main_test, cmdline_long_help_arg) {
  auto exit_code = run({"--long-help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Advanced options:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression level defaults:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression algorithms:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Categories:"));
}

TEST_F(dwarfsck_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
}

TEST_F(dwarfsextract_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
}

#ifdef DWARFS_PERFMON_ENABLED
TEST_F(dwarfsextract_main_test, perfmon) {
  // TODO: passing in test_data_image this way only only works because
  //       dwarfsextract_main does not currently use the os_access abstraction
  auto exit_code = run({"-i", test_data_image.string(), "-f", "mtree",
                        "--perfmon", "filesystem_v2,inode_reader_v2"});
  EXPECT_EQ(exit_code, 0);
  auto outs = out();
  auto errs = err();
  EXPECT_GT(outs.size(), 100);
  EXPECT_FALSE(errs.empty());
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readv_future]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.getattr]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.open]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readlink]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.statvfs]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[inode_reader_v2.readv_future]"));
#ifndef _WIN32
  // googletest on Windows does not support fancy regexes
  EXPECT_THAT(errs, ::testing::ContainsRegex(
                        R"(\[filesystem_v2\.getattr\])"
                        R"(\s+samples:\s+[0-9]+)"
                        R"(\s+overall:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+avg latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p50 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p90 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p99 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"));
#endif
}
#endif

class mkdwarfs_input_list_test : public testing::TestWithParam<input_mode> {};

TEST_P(mkdwarfs_input_list_test, basic) {
  auto mode = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "somelink\nfoo.pl\nsomedir/ipsum.py\n";

  mkdwarfs_tester t;
  std::string input_file;

  if (mode == input_mode::from_file) {
    input_file = "input_list.txt";
    t.fa->set_file(input_file, input_list);
  } else {
    input_file = "-";
    t.iol.set_in(input_list);
  }

  EXPECT_EQ(0, t.run({"--input-list", input_file, "-o", image_file}));

  auto fs = t.fs_from_file(image_file);

  auto link = fs.find("/somelink");
  auto foo = fs.find("/foo.pl");
  auto ipsum = fs.find("/somedir/ipsum.py");

  EXPECT_TRUE(link);
  EXPECT_TRUE(foo);
  EXPECT_TRUE(ipsum);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(link->is_symlink());
  EXPECT_TRUE(foo->is_regular_file());
  EXPECT_TRUE(ipsum->is_regular_file());

  std::set<fs::path> const expected = {"", "somelink", "foo.pl", "somedir",
                                       fs::path("somedir") / "ipsum.py"};
  std::set<fs::path> actual;
  fs.walk([&](auto const& e) { actual.insert(e.fs_path()); });

  EXPECT_EQ(expected, actual);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_input_list_test,
                         ::testing::ValuesIn(input_modes));

class categorizer_test : public testing::TestWithParam<std::string> {};

TEST_P(categorizer_test, end_to_end) {
  auto level = GetParam();
  std::string const image_file = "test.dwarfs";

  auto t = mkdwarfs_tester::create_empty();

  t.os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  t.os->add_local_files(audio_data_dir);
  t.os->add_file("random", 4096, true);

  EXPECT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize",
                      "--log-level=" + level}));

  auto fs = t.fs_from_file(image_file);

  auto iv16 = fs.find("/test8.aiff");
  auto iv32 = fs.find("/test8.caf");

  EXPECT_TRUE(iv16);
  EXPECT_TRUE(iv32);

  {
    std::vector<std::string> dumps;

    for (int detail = 0; detail <= 6; ++detail) {
      std::ostringstream os;
      fs.dump(os, detail);
      auto d = os.str();
      if (!dumps.empty()) {
        EXPECT_GT(d.size(), dumps.back().size()) << detail;
      }
      dumps.emplace_back(std::move(d));
    }

    EXPECT_GT(dumps.back().size(), 10'000);
  }

  {
    std::vector<std::string> infos;

    for (int detail = 0; detail <= 4; ++detail) {
      auto info = fs.info_as_dynamic(detail);
      auto i = folly::toJson(info);
      if (!infos.empty()) {
        EXPECT_GT(i.size(), infos.back().size()) << detail;
      }
      infos.emplace_back(std::move(i));
    }

    EXPECT_GT(infos.back().size(), 1'000);
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, categorizer_test,
                         ::testing::Values("error", "warn", "info", "verbose",
                                           "debug", "trace"));

TEST(mkdwarfs_test, chmod_norm) {
  std::string const image_file = "test.dwarfs";

  std::set<std::string> real, norm;

  {
    mkdwarfs_tester t;
    EXPECT_EQ(0, t.run({"-i", "/", "-o", image_file}));
    auto fs = t.fs_from_file(image_file);
    fs.walk([&](auto const& e) { real.insert(e.inode().perm_string()); });
  }

  {
    mkdwarfs_tester t;
    EXPECT_EQ(0, t.run({"-i", "/", "-o", image_file, "--chmod=norm"}));
    auto fs = t.fs_from_file(image_file);
    fs.walk([&](auto const& e) { norm.insert(e.inode().perm_string()); });
  }

  EXPECT_NE(real, norm);

  std::set<std::string> expected_norm = {"r--r--r--", "r-xr-xr-x"};

  EXPECT_EQ(expected_norm, norm);
}

TEST(mkdwarfs_test, dump_inodes) {
  std::string const image_file = "test.dwarfs";
  std::string const inode_file = "inode.dump";

  mkdwarfs_tester t;
  t.os->setenv("DWARFS_DUMP_INODES", inode_file);

  EXPECT_EQ(0, t.run({"-i", "/", "-o", image_file}));

  auto dump = t.fa->get_file(inode_file);

  ASSERT_TRUE(dump);
  EXPECT_GT(dump->size(), 100) << dump.value();
}

TEST(mkdwarfs_test, set_time_now) {
  auto t0 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  auto regfs = build_with_args();
  ASSERT_TRUE(regfs);
  auto reg = get_all_fs_times(*regfs);

  auto optfs = build_with_args({"--set-time=now"});
  ASSERT_TRUE(optfs);
  auto opt = get_all_fs_times(*optfs);

  auto t1 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  ASSERT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_GE(*opt.begin(), t0);
  EXPECT_LE(*opt.begin(), t1);
}

TEST(mkdwarfs_test, set_time_epoch) {
  auto regfs = build_with_args();
  ASSERT_TRUE(regfs);
  auto reg = get_all_fs_times(*regfs);

  auto optfs = build_with_args({"--set-time=100000001"});
  ASSERT_TRUE(optfs);
  auto opt = get_all_fs_times(*optfs);

  EXPECT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 100000001);
}

TEST(mkdwarfs_test, set_time_epoch_string) {
  using namespace std::chrono_literals;
  using std::chrono::sys_days;

  auto optfs = build_with_args({"--set-time", "2020-01-01 01:02"});
  ASSERT_TRUE(optfs);
  auto opt = get_all_fs_times(*optfs);

  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(),
            std::chrono::duration_cast<std::chrono::seconds>(
                (sys_days{2020y / 1 / 1} + 1h + 2min).time_since_epoch())
                .count());
}

TEST(mkdwarfs_test, set_time_error) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--set-time=InVaLiD"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot parse time point"));
}

TEST(mkdwarfs_test, unrecognized_arguments) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("unrecognized argument"));
}

TEST(mkdwarfs_test, invalid_compression_level) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-l", "10"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid compression level"));
}

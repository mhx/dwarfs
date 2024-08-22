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

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <random>
#include <regex>
#include <set>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <nlohmann/json.hpp>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/config.h>
#include <dwarfs/file_util.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/iovec_read_buf.h>
#include <dwarfs/string.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/util.h>
#include <dwarfs_tool_main.h>

#include "filter_test_data.h"
#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

using tool::iolayer;

namespace {

// TODO: this is a workaround for older Clang versions
struct fs_path_hash {
  auto operator()(const std::filesystem::path& p) const noexcept {
    return std::filesystem::hash_value(p);
  }
};

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";
auto fits_data_dir = test_dir / "fits";

constexpr std::array<std::string_view, 6> const log_level_strings{
    "error", "warn", "info", "verbose", "debug", "trace"};

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

class tester_common {
 public:
  using main_ptr_t = tool::main_adapter::main_fn_type;

  tester_common(main_ptr_t mp, std::string toolname,
                std::shared_ptr<test::os_access_mock> pos)
      : fa{std::make_shared<test::test_file_access>()}
      , os{std::move(pos)}
      , iol{std::make_unique<test::test_iolayer>(os, fa)}
      , main_{mp}
      , toolname_{std::move(toolname)} {
    setup_locale();
  }

  int run(std::vector<std::string> args) {
    args.insert(args.begin(), toolname_);
    return tool::main_adapter(main_)(args, iol->get());
  }

  int run(std::initializer_list<std::string> args) {
    return run(std::vector<std::string>(args));
  }

  int run(std::string args) { return run(test::parse_args(args)); }

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::shared_ptr<test::test_file_access> fa;
  std::shared_ptr<test::os_access_mock> os;
  std::unique_ptr<test::test_iolayer> iol;

 private:
  main_ptr_t main_;
  std::string toolname_;
};

struct random_file_tree_options {
  double avg_size{4096.0};
  int dimension{20};
  int max_name_len{50};
  bool with_errors{false};
  bool with_invalid_utf8{false};
};

auto constexpr default_fs_opts = reader::filesystem_options{
    .block_cache = {.max_bytes = 256 * 1024,
                    .sequential_access_detector_threshold = 4}};

class mkdwarfs_tester : public tester_common {
 public:
  mkdwarfs_tester(std::shared_ptr<test::os_access_mock> pos)
      : tester_common(tool::mkdwarfs_main, "mkdwarfs", std::move(pos)) {}

  mkdwarfs_tester()
      : mkdwarfs_tester(test::os_access_mock::create_test_instance()) {}

  static mkdwarfs_tester create_empty() {
    return mkdwarfs_tester(std::make_shared<test::os_access_mock>());
  }

  void add_stream_logger(std::ostream& os,
                         logger::level_type level = logger::VERBOSE) {
    lgr =
        std::make_unique<stream_logger>(std::make_shared<test::test_terminal>(),
                                        os, logger_options{.threshold = level});
  }

  void add_root_dir() { os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0}); }

  void add_special_files() {
    static constexpr file_stat::off_type const size = 10;
    std::string data(size, 'x');
    os->add("suid", {1001, 0104755, 1, 0, 0, size, 0, 3333, 2222, 1111}, data);
    os->add("sgid", {1002, 0102755, 1, 0, 0, size, 0, 0, 0, 0}, data);
    os->add("sticky", {1003, 0101755, 1, 0, 0, size, 0, 0, 0, 0}, data);
    os->add("block", {1004, 060666, 1, 0, 0, 0, 77, 0, 0, 0}, std::string{});
    os->add("sock", {1005, 0140666, 1, 0, 0, 0, 0, 0, 0, 0}, std::string{});
  }

  std::vector<std::pair<fs::path, std::string>> add_random_file_tree(
      random_file_tree_options const& opt = random_file_tree_options{}) {
    size_t max_size{128 * static_cast<size_t>(opt.avg_size)};
    std::mt19937_64 rng{42};
    std::exponential_distribution<> size_dist{1 / opt.avg_size};
    std::uniform_int_distribution<> path_comp_size_dist{0, opt.max_name_len};
    std::uniform_int_distribution<> invalid_dist{0, 1};
    std::vector<std::pair<fs::path, std::string>> paths;

    auto random_path_component = [&] {
      auto size = path_comp_size_dist(rng);
      if (opt.with_invalid_utf8 && invalid_dist(rng) == 0) {
        return test::create_random_string(size, 96, 255, rng);
      }
      return test::create_random_string(size, 'A', 'Z', rng);
    };

    for (int x = 0; x < opt.dimension; ++x) {
      fs::path d1{random_path_component() + std::to_string(x)};
      os->add_dir(d1);

      for (int y = 0; y < opt.dimension; ++y) {
        fs::path d2{d1 / (random_path_component() + std::to_string(y))};
        os->add_dir(d2);

        for (int z = 0; z < opt.dimension; ++z) {
          fs::path f{d2 / (random_path_component() + std::to_string(z))};
          auto size = std::min(max_size, static_cast<size_t>(size_dist(rng)));
          std::string data;

          if (size < 1024 * 1024 && rng() % 2 == 0) {
            data = test::create_random_string(size, rng);
          } else {
            data = test::loremipsum(size);
          }

          os->add_file(f, data);
          paths.emplace_back(f, data);

          if (opt.with_errors) {
            auto failpath = fs::path{"/"} / f;
            switch (rng() % 8) {
            case 0:
              os->set_access_fail(failpath);
              [[fallthrough]];
            case 1:
            case 2:
              os->set_map_file_error(
                  failpath,
                  std::make_exception_ptr(std::runtime_error("map_file_error")),
                  rng() % 4);
              break;

            default:
              break;
            }
          }
        }
      }
    }

    return paths;
  }

  void add_test_file_tree() {
    for (auto const& [stat, name] : test::test_dirtree()) {
      auto path = name.substr(name.size() == 5 ? 5 : 6);

      switch (stat.type()) {
      case posix_file_type::regular:
        os->add(path, stat,
                [size = stat.size] { return test::loremipsum(size); });
        break;
      case posix_file_type::symlink:
        os->add(path, stat, test::loremipsum(stat.size));
        break;
      default:
        os->add(path, stat);
        break;
      }
    }
  }

  reader::filesystem_v2
  fs_from_data(std::string data,
               reader::filesystem_options const& opt = default_fs_opts) {
    if (!lgr) {
      lgr = std::make_unique<test::test_logger>();
    }
    auto mm = std::make_shared<test::mmap_mock>(std::move(data));
    return reader::filesystem_v2(*lgr, *os, mm, opt);
  }

  reader::filesystem_v2
  fs_from_file(std::string path,
               reader::filesystem_options const& opt = default_fs_opts) {
    auto fsimage = fa->get_file(path);
    if (!fsimage) {
      throw std::runtime_error("file not found: " + path);
    }
    return fs_from_data(std::move(fsimage.value()), opt);
  }

  reader::filesystem_v2
  fs_from_stdout(reader::filesystem_options const& opt = default_fs_opts) {
    return fs_from_data(out(), opt);
  }

  std::unique_ptr<logger> lgr;
};

std::string
build_test_image(std::vector<std::string> extra_args = {},
                 std::map<std::string, std::string> extra_files = {}) {
  mkdwarfs_tester t;
  for (auto const& [name, contents] : extra_files) {
    t.fa->set_file(name, contents);
  }
  std::vector<std::string> args = {"-i", "/", "-o", "-"};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  if (t.run(args) != 0) {
    throw std::runtime_error("failed to build test image:\n" + t.err());
  }
  return t.out();
}

class dwarfsck_tester : public tester_common {
 public:
  dwarfsck_tester(std::shared_ptr<test::os_access_mock> pos)
      : tester_common(tool::dwarfsck_main, "dwarfsck", std::move(pos)) {}

  dwarfsck_tester()
      : dwarfsck_tester(std::make_shared<test::os_access_mock>()) {}

  static dwarfsck_tester create_with_image(std::string image) {
    auto os = std::make_shared<test::os_access_mock>();
    os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
    os->add_file("image.dwarfs", std::move(image));
    return dwarfsck_tester(std::move(os));
  }

  static dwarfsck_tester create_with_image() {
    return create_with_image(build_test_image());
  }
};

class dwarfsextract_tester : public tester_common {
 public:
  dwarfsextract_tester(std::shared_ptr<test::os_access_mock> pos)
      : tester_common(tool::dwarfsextract_main, "dwarfsextract",
                      std::move(pos)) {}

  dwarfsextract_tester()
      : dwarfsextract_tester(std::make_shared<test::os_access_mock>()) {}

  static dwarfsextract_tester create_with_image(std::string image) {
    auto os = std::make_shared<test::os_access_mock>();
    os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
    os->add_file("image.dwarfs", std::move(image));
    return dwarfsextract_tester(std::move(os));
  }

  static dwarfsextract_tester create_with_image() {
    return create_with_image(build_test_image());
  }
};

std::tuple<std::optional<reader::filesystem_v2>, mkdwarfs_tester>
build_with_args(std::vector<std::string> opt_args = {}) {
  std::string const image_file = "test.dwarfs";
  mkdwarfs_tester t;
  std::vector<std::string> args = {"-i", "/", "-o", image_file};
  args.insert(args.end(), opt_args.begin(), opt_args.end());
  if (t.run(args) != 0) {
    return {std::nullopt, std::move(t)};
  }
  return {t.fs_from_file(image_file), std::move(t)};
}

std::set<uint64_t> get_all_fs_times(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> times;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    times.insert(st.atime());
    times.insert(st.ctime());
    times.insert(st.mtime());
  });
  return times;
}

std::set<uint64_t> get_all_fs_uids(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> uids;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    uids.insert(st.uid());
  });
  return uids;
}

std::set<uint64_t> get_all_fs_gids(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> gids;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    gids.insert(st.gid());
  });
  return gids;
}

} // namespace

class mkdwarfs_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "mkdwarfs");
    return tool::main_adapter(tool::mkdwarfs_main)(args, iol->get());
  }
};

class dwarfsck_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsck");
    return tool::main_adapter(tool::dwarfsck_main)(args, iol->get());
  }
};

class dwarfsextract_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsextract");
    return tool::main_adapter(tool::dwarfsextract_main)(args, iol->get());
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

#if DWARFS_PERFMON_ENABLED
TEST(dwarfsextract_test, perfmon) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "mtree", "--perfmon",
                      "filesystem_v2,inode_reader_v2"}))
      << t.err();
  auto outs = t.out();
  auto errs = t.err();
  EXPECT_GT(outs.size(), 100);
  EXPECT_FALSE(errs.empty());
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readv_future_ec]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.getattr]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.open]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readlink_ec]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.statvfs]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[inode_reader_v2.readv_future]"));
  static std::regex const perfmon_re{R"(\[filesystem_v2\.getattr\])"
                                     R"(\s+samples:\s+\d+)"
                                     R"(\s+overall:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+avg latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p50 latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p90 latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p99 latency:\s+\d+(\.\d+)?[num]?s)"};
  EXPECT_TRUE(std::regex_search(errs, perfmon_re)) << errs;
}

TEST(dwarfsextract_test, perfmon_trace) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "gnutar", "--perfmon",
                      "filesystem_v2,inode_reader_v2,block_cache",
                      "--perfmon-trace", "trace.json"}))
      << t.err();

  EXPECT_GT(t.out().size(), 1'000'000);

  auto trace_file = t.fa->get_file("trace.json");
  ASSERT_TRUE(trace_file);
  EXPECT_GT(trace_file->size(), 10'000);

  auto trace = nlohmann::json::parse(*trace_file);
  EXPECT_TRUE(trace.is_array());

  std::set<std::string> const expected = {"filesystem_v2", "inode_reader_v2",
                                          "block_cache"};
  std::set<std::string> actual;

  for (auto const& obj : trace) {
    EXPECT_TRUE(obj.is_object());
    EXPECT_TRUE(obj["cat"].is_string());
    actual.insert(obj["cat"].get<std::string>());
  }

  EXPECT_EQ(expected, actual);
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
    t.iol->set_in(input_list);
  }

  ASSERT_EQ(0, t.run({"--input-list", input_file, "-o", image_file}));

  std::ostringstream oss;
  t.add_stream_logger(oss, logger::DEBUG);

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

TEST(mkdwarfs_test, input_list_large) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  auto paths = t.add_random_file_tree({.avg_size = 32.0, .dimension = 32});

  {
    std::ostringstream os;
    for (auto const& p : paths) {
      os << p.first.string() << '\n';
    }
    t.iol->set_in(os.str());
  }

  ASSERT_EQ(0, t.run({"-l3", "--input-list", "-", "-o", "-"})) << t.err();

  auto fs = t.fs_from_stdout();

  std::set<fs::path> expected;
  std::transform(paths.begin(), paths.end(),
                 std::inserter(expected, expected.end()),
                 [](auto const& p) { return p.first; });
  std::set<fs::path> actual;

  fs.walk([&](auto const& e) {
    if (e.inode().is_regular_file()) {
      actual.insert(e.fs_path());
    }
  });

  EXPECT_EQ(expected, actual);
}

class logging_test : public testing::TestWithParam<std::string_view> {};

TEST_P(logging_test, end_to_end) {
  auto level = GetParam();
  std::string const image_file = "test.dwarfs";

  auto t = mkdwarfs_tester::create_empty();

  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize",
                      fmt::format("--log-level={}", level)}));

  auto fs = t.fs_from_file(image_file);

  auto iv16 = fs.find("/test8.aiff");
  auto iv32 = fs.find("/test8.caf");

  EXPECT_TRUE(iv16);
  EXPECT_TRUE(iv32);

  {
    std::vector<std::string> dumps;

    for (int detail = 0; detail <= 6; ++detail) {
      std::ostringstream os;
      fs.dump(os, {.features = reader::fsinfo_features::for_level(detail)});
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
      auto info = fs.info_as_json(
          {.features = reader::fsinfo_features::for_level(detail)});
      auto i = info.dump();
      if (!infos.empty()) {
        EXPECT_GT(i.size(), infos.back().size()) << detail;
      }
      infos.emplace_back(std::move(i));
    }

    EXPECT_GT(infos.back().size(), 1'000);
  }
}

INSTANTIATE_TEST_SUITE_P(mkdwarfs, logging_test,
                         ::testing::ValuesIn(log_level_strings));

class term_logging_test
    : public testing::TestWithParam<std::tuple<std::string_view, bool>> {};

TEST_P(term_logging_test, end_to_end) {
  auto const [level, fancy] = GetParam();
  std::map<std::string_view, std::pair<std::string_view, char>> const match{
      {"error", {"<bold-red>", 'E'}},
      {"warn", {"<bold-yellow>", 'W'}},
      {"info", {"", 'I'}},
      {"verbose", {"<dim-cyan>", 'V'}},
      {"debug", {"<dim-yellow>", 'D'}},
      {"trace", {"<gray>", 'T'}},
  };
  auto const cutoff =
      std::find(log_level_strings.begin(), log_level_strings.end(), level);
  ASSERT_FALSE(cutoff == log_level_strings.end());

  {
    mkdwarfs_tester t;
    t.iol->set_terminal_is_tty(fancy);
    t.iol->set_terminal_fancy(fancy);
    t.os->set_access_fail("/somedir/ipsum.py"); // trigger an error
    EXPECT_EQ(2, t.run("-l1 -i / -o - --categorize --num-workers=8 -S 22 "
                       "-L 16M --progress=none --log-level=" +
                       std::string(level)))
        << t.err();

    auto err = t.err();
    auto it = log_level_strings.begin();

    auto make_contains_regex = [fancy = fancy](auto m) {
      auto const& [color, prefix] = m->second;
      auto beg = fancy ? color : std::string_view{};
      auto end = fancy && !color.empty() ? "<normal>" : "";
      return fmt::format("{}{}\\s\\d\\d:\\d\\d:\\d\\d.*{}\\r?\\n", beg, prefix,
                         end);
    };

    while (it != cutoff + 1) {
      auto m = match.find(*it);
      EXPECT_FALSE(m == match.end());
      auto re = make_contains_regex(m);
      EXPECT_TRUE(std::regex_search(err, std::regex(re))) << re << ", " << err;
      ++it;
    }

    while (it != log_level_strings.end()) {
      auto m = match.find(*it);
      EXPECT_FALSE(m == match.end());
      auto re = make_contains_regex(m);
      EXPECT_FALSE(std::regex_search(err, std::regex(re))) << re << ", " << err;
      ++it;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    mkdwarfs, term_logging_test,
    ::testing::Combine(::testing::ValuesIn(log_level_strings),
                       ::testing::Bool()));

TEST(mkdwarfs_test, no_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o -")) << t.err();
  EXPECT_THAT(t.err(), ::testing::Not(::testing::HasSubstr("[scanner.cpp:")));
}

TEST(mkdwarfs_test, default_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o - --log-level=verbose")) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("[scanner.cpp:"));
}

TEST(mkdwarfs_test, explicit_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o - --log-with-context")) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("[scanner.cpp:"));
}

TEST(mkdwarfs_test, metadata_inode_info) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);

  ASSERT_EQ(0, t.run("-l3 -i / -o - --categorize"));

  auto fs = t.fs_from_stdout();

  {
    auto iv = fs.find("/test8.aiff");
    ASSERT_TRUE(iv);

    auto info = fs.get_inode_info(*iv);
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    for (auto chunk : info["chunks"]) {
      ASSERT_TRUE(chunk.count("category") > 0);
      categories.insert(chunk["category"].get<std::string>());
    }

    std::set<std::string> expected{
        "pcmaudio/metadata",
        "pcmaudio/waveform",
    };

    EXPECT_EQ(expected, categories);
  }

  {
    auto iv = fs.find("/test.fits");
    ASSERT_TRUE(iv);

    auto info = fs.get_inode_info(*iv);
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    for (auto chunk : info["chunks"]) {
      ASSERT_TRUE(chunk.count("category") > 0);
      categories.insert(chunk["category"].get<std::string>());
    }

    std::set<std::string> expected{
        "fits/image",
        "fits/metadata",
    };

    EXPECT_EQ(expected, categories);
  }
}

TEST(mkdwarfs_test, metadata_path) {
  fs::path const f1{"test.txt"};
  fs::path const f2{U"猫.txt"};
  fs::path const f3{u8"⚽️.bin"};
  fs::path const f4{L"Карибського"};
  fs::path const d1{u8"我爱你"};
  fs::path const f5{d1 / u8"☀️ Sun"};

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file(f1, 2, true);
  t.os->add_file(f2, 4, true);
  t.os->add_file(f3, 8, true);
  t.os->add_file(f4, 16, true);
  t.os->add_dir(d1);
  t.os->add_file(f5, 32, true);
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  std::map<size_t, reader::dir_entry_view> entries;
  fs.walk([&](auto e) {
    auto stat = fs.getattr(e.inode());
    if (stat.is_regular_file()) {
      entries.emplace(stat.size(), e);
    }
  });

  ASSERT_EQ(entries.size(), 5);

  auto e1 = entries.at(2);
  auto e2 = entries.at(4);
  auto e3 = entries.at(8);
  auto e4 = entries.at(16);
  auto e5 = entries.at(32);

  auto de = fs.find(d1.string().c_str());

  ASSERT_TRUE(de);
  EXPECT_EQ(de->mode_string(), "---drwxr-xr-x");
  EXPECT_EQ(e1.inode().mode_string(), "----rw-r--r--");

  EXPECT_EQ(e1.fs_path(), f1);
  EXPECT_EQ(e2.fs_path(), f2);
  EXPECT_EQ(e3.fs_path(), f3);
  EXPECT_EQ(e4.fs_path(), f4);
  EXPECT_EQ(e5.fs_path(), f5);

  EXPECT_EQ(e1.wpath(), L"test.txt");
  EXPECT_EQ(e2.wpath(), L"猫.txt");
  EXPECT_EQ(e3.wpath(), L"⚽️.bin");
  EXPECT_EQ(e4.wpath(), L"Карибського");
#ifdef _WIN32
  EXPECT_EQ(e5.wpath(), L"我爱你\\☀️ Sun");
#else
  EXPECT_EQ(e5.wpath(), L"我爱你/☀️ Sun");
#endif

  EXPECT_EQ(e1.path(), "test.txt");
  EXPECT_EQ(e2.path(), "猫.txt");
  EXPECT_EQ(e3.path(), "⚽️.bin");
  EXPECT_EQ(e4.path(), "Карибського");
#ifdef _WIN32
  EXPECT_EQ(e5.path(), "我爱你\\☀️ Sun");
#else
  EXPECT_EQ(e5.path(), "我爱你/☀️ Sun");
#endif

  EXPECT_EQ(e1.unix_path(), "test.txt");
  EXPECT_EQ(e2.unix_path(), "猫.txt");
  EXPECT_EQ(e3.unix_path(), "⚽️.bin");
  EXPECT_EQ(e4.unix_path(), "Карибського");
  EXPECT_EQ(e5.unix_path(), "我爱你/☀️ Sun");
}

TEST(mkdwarfs_test, metadata_modes) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --with-specials --with-devices"));
  auto fs = t.fs_from_stdout();

  auto d1 = fs.find("/");
  auto d2 = fs.find("/foo.pl");
  auto d3 = fs.find("/somelink");
  auto d4 = fs.find("/somedir");
  auto d5 = fs.find("/somedir/pipe");
  auto d6 = fs.find("/somedir/null");
  auto d7 = fs.find("/suid");
  auto d8 = fs.find("/sgid");
  auto d9 = fs.find("/sticky");
  auto d10 = fs.find("/block");
  auto d11 = fs.find("/sock");

  ASSERT_TRUE(d1);
  ASSERT_TRUE(d2);
  ASSERT_TRUE(d3);
  ASSERT_TRUE(d4);
  ASSERT_TRUE(d5);
  ASSERT_TRUE(d6);
  ASSERT_TRUE(d7);
  ASSERT_TRUE(d8);
  ASSERT_TRUE(d9);
  ASSERT_TRUE(d10);
  ASSERT_TRUE(d11);

  EXPECT_EQ(d1->mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d2->mode_string(), "----rw-------");
  EXPECT_EQ(d3->mode_string(), "---lrwxrwxrwx");
  EXPECT_EQ(d4->mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d5->mode_string(), "---prw-r--r--");
  EXPECT_EQ(d6->mode_string(), "---crw-rw-rw-");
  EXPECT_EQ(d7->mode_string(), "U---rwxr-xr-x");
  EXPECT_EQ(d8->mode_string(), "-G--rwxr-xr-x");
  EXPECT_EQ(d9->mode_string(), "--S-rwxr-xr-x");
  EXPECT_EQ(d10->mode_string(), "---brw-rw-rw-");
  EXPECT_EQ(d11->mode_string(), "---srw-rw-rw-");
}

TEST(mkdwarfs_test, metadata_specials) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --with-specials --with-devices"));
  auto fs = t.fs_from_stdout();

  std::ostringstream oss;
  fs.dump(oss, {.features = reader::fsinfo_features::all()});
  auto dump = oss.str();

  auto meta = fs.metadata_as_json();
  std::set<std::string> types;
  for (auto const& ino : meta["root"]["inodes"]) {
    types.insert(ino["type"].get<std::string>());
    if (auto di = ino.find("inodes"); di != ino.end()) {
      for (auto const& ino2 : *di) {
        types.insert(ino2["type"].get<std::string>());
      }
    }
  }
  std::set<std::string> expected_types = {
      "file", "link", "directory", "chardev", "blockdev", "socket", "fifo"};
  EXPECT_EQ(expected_types, types);

  EXPECT_THAT(dump, ::testing::HasSubstr("char device"));
  EXPECT_THAT(dump, ::testing::HasSubstr("block device"));
  EXPECT_THAT(dump, ::testing::HasSubstr("socket"));
  EXPECT_THAT(dump, ::testing::HasSubstr("named pipe"));

  auto iv = fs.find("/block");
  ASSERT_TRUE(iv);

  std::error_code ec;
  auto stat = fs.getattr(*iv, ec);
  EXPECT_FALSE(ec);

  EXPECT_TRUE(stat.is_device());
  EXPECT_EQ(77, stat.rdev());
}

TEST(mkdwarfs_test, metadata_time_resolution) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --time-resolution=min --keep-all-times"));
  auto fs = t.fs_from_stdout();

  std::ostringstream oss;
  fs.dump(oss, {.features = reader::fsinfo_features::all()});
  auto dump = oss.str();

  EXPECT_THAT(dump, ::testing::HasSubstr("time resolution: 60 seconds"));

  auto dyn = fs.info_as_json({.features = reader::fsinfo_features::all()});
  EXPECT_EQ(60, dyn["time_resolution"].get<int>());

  auto iv = fs.find("/suid");
  ASSERT_TRUE(iv);

  std::error_code ec;
  auto stat = fs.getattr(*iv, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(3300, stat.atime());
  EXPECT_EQ(2220, stat.mtime());
  EXPECT_EQ(1080, stat.ctime());
}

TEST(mkdwarfs_test, metadata_readdir) {
  mkdwarfs_tester t;
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  auto iv = fs.find("/somedir");
  ASSERT_TRUE(iv);

  auto dir = fs.opendir(*iv);
  ASSERT_TRUE(dir);

  {
    auto r = fs.readdir(dir.value(), 0);
    ASSERT_TRUE(r);

    auto [ino, name] = r.value();
    EXPECT_EQ(".", name);
    EXPECT_EQ(ino.inode_num(), iv->inode_num());
  }

  {
    auto r = fs.readdir(dir.value(), 1);
    ASSERT_TRUE(r);

    auto [ino, name] = r.value();
    EXPECT_EQ("..", name);

    auto parent = fs.find("/");
    ASSERT_TRUE(parent);
    EXPECT_EQ(ino.inode_num(), parent->inode_num());
  }

  {
    auto r = fs.readdir(dir.value(), 100);
    EXPECT_FALSE(r);
  }
}

TEST(mkdwarfs_test, metadata_directory_iterator) {
  mkdwarfs_tester t;
  t.os->add_dir("emptydir");
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  std::map<std::string, std::vector<std::string>> testdirs{
      {"",
       {"bar.pl", "baz.pl", "empty", "emptydir", "foo.pl", "ipsum.txt",
        "somedir", "somelink", "test.pl"}},
      {"somedir", {"bad", "empty", "ipsum.py"}},
      {"emptydir", {}},
  };

  for (auto const& [path, expected_names] : testdirs) {
    auto iv = fs.find(path.c_str());
    ASSERT_TRUE(iv) << path;

    auto dir = fs.opendir(*iv);
    ASSERT_TRUE(dir) << path;

    std::vector<std::string> actual_names;
    std::vector<std::string> actual_paths;
    for (auto const& dev : *dir) {
      actual_names.push_back(dev.name());
      actual_paths.push_back(dev.unix_path());
    }

    std::vector<std::string> expected_paths;
    for (auto const& name : expected_names) {
      expected_paths.push_back(path.empty() ? name : path + "/" + name);
    }

    EXPECT_EQ(expected_names, actual_names) << path;
    EXPECT_EQ(expected_paths, actual_paths) << path;
  }
}

TEST(mkdwarfs_test, metadata_access) {
#ifdef _WIN32
#define F_OK 0
#define W_OK 2
#define R_OK 4
  static constexpr int const x_ok = 1;
#else
  static constexpr int const x_ok = X_OK;
#endif

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add("access", {1001, 040742, 1, 222, 3333});
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));

  {
    auto fs = t.fs_from_stdout();

    auto iv = fs.find("/access");
    ASSERT_TRUE(iv);

    EXPECT_TRUE(fs.access(*iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(*iv, R_OK, 1, 1));
    EXPECT_TRUE(fs.access(*iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(*iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(*iv, R_OK, 1, 3333));
    EXPECT_TRUE(fs.access(*iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(*iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(*iv, R_OK, 222, 7));
    EXPECT_TRUE(fs.access(*iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(*iv, x_ok, 222, 7));
  }

  {
    auto fs = t.fs_from_stdout({.metadata = {.readonly = true}});

    auto iv = fs.find("/access");
    ASSERT_TRUE(iv);

    EXPECT_TRUE(fs.access(*iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(*iv, R_OK, 1, 1));
    EXPECT_FALSE(fs.access(*iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(*iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(*iv, R_OK, 1, 3333));
    EXPECT_FALSE(fs.access(*iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(*iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(*iv, R_OK, 222, 7));
    EXPECT_FALSE(fs.access(*iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(*iv, x_ok, 222, 7));
  }
}

TEST(mkdwarfs_test, chmod_norm) {
  std::string const image_file = "test.dwarfs";

  std::set<std::string> real, norm;

  {
    mkdwarfs_tester t;
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file}));
    auto fs = t.fs_from_file(image_file);
    fs.walk([&](auto const& e) { real.insert(e.inode().perm_string()); });
  }

  {
    mkdwarfs_tester t;
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--chmod=norm"}));
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

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);
  t.os->add_file("large", 32 * 1024 * 1024);
  t.add_random_file_tree({.avg_size = 1024.0, .dimension = 8});
  t.os->setenv("DWARFS_DUMP_INODES", inode_file);

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize", "-W8"}));

  auto dump = t.fa->get_file(inode_file);

  ASSERT_TRUE(dump);
  EXPECT_GT(dump->size(), 1000) << dump.value();
}

TEST(mkdwarfs_test, set_time_now) {
  auto t0 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_times(*regfs);

  auto [optfs, optt] = build_with_args({"--set-time=now"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_times(*optfs);

  auto t1 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  ASSERT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_GE(*opt.begin(), t0);
  EXPECT_LE(*opt.begin(), t1);
}

TEST(mkdwarfs_test, set_time_epoch) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_times(*regfs);

  auto [optfs, optt] = build_with_args({"--set-time=100000001"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_times(*optfs);

  EXPECT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 100000001);
}

TEST(mkdwarfs_test, set_time_epoch_string) {
  using namespace std::chrono_literals;
  using std::chrono::sys_days;

  auto [optfs, optt] = build_with_args({"--set-time", "2020-01-01 01:02"});
  ASSERT_TRUE(optfs) << optt.err();
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

TEST(mkdwarfs_test, set_owner) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_uids(*regfs);

  auto [optfs, optt] = build_with_args({"--set-owner=333"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_uids(*optfs);

  ASSERT_EQ(reg.size(), 2);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 333);
}

TEST(mkdwarfs_test, set_group) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_gids(*regfs);

  auto [optfs, optt] = build_with_args({"--set-group=444"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_gids(*optfs);

  ASSERT_EQ(reg.size(), 2);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 444);
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

TEST(mkdwarfs_test, block_size_too_small) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-S", "1"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("block size must be between"));
}

TEST(mkdwarfs_test, block_size_too_large) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-S", "100"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("block size must be between"));
}

TEST(mkdwarfs_test, cannot_combine_input_list_and_filter) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"--input-list", "-", "-o", "-", "-F", "+ *"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("cannot combine --input-list and --filter"));
}

TEST(mkdwarfs_test, cannot_open_input_list_file) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"--input-list", "missing.list", "-o", "-"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open input list file"));
}

class mkdwarfs_recompress_test
    : public testing::TestWithParam<std::string_view> {};

TEST_P(mkdwarfs_recompress_test, recompress) {
  std::string const compression{GetParam()};
  std::string compression_type = compression;
  std::string const image_file = "test.dwarfs";
  std::string image;
  reader::fsinfo_options const history_opts{
      .features = {reader::fsinfo_feature::history}};

  if (auto pos = compression_type.find(':'); pos != std::string::npos) {
    compression_type.erase(pos);
  }
  boost::algorithm::to_upper(compression_type);
  if (compression_type == "NULL") {
    compression_type = "NONE";
  }

  {
    mkdwarfs_tester t;
    t.os->add_local_files(audio_data_dir);
    t.os->add_local_files(fits_data_dir);
    t.os->add_file("random", 4096, true);
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize", "-C",
                        compression}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    EXPECT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);
    auto history = fs.info_as_json(history_opts)["history"];
    EXPECT_EQ(1, history.size());
  }

  auto tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress", "-l0"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    auto history = fs.info_as_json(history_opts)["history"];
    EXPECT_EQ(2, history.size());
  }

  {
    auto t = tester(image);
    EXPECT_NE(0, t.run({"-i", image_file, "-o", "-", "--recompress=foo"}));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid recompress mode"));
  }

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=metadata"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
  }

  {
    auto t = tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::null"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
  }

#ifdef DWARFS_HAVE_FLAC
  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::flac:level=4"}))
        << t.err();
    EXPECT_THAT(t.err(),
                ::testing::HasSubstr(fmt::format(
                    "cannot compress {} compressed block with compressor 'flac "
                    "[level=4]' because the following metadata requirements "
                    "are not met: missing requirement 'bits_per_sample'",
                    compression_type)));
  }
#endif

#ifdef DWARFS_HAVE_RICEPP
  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress=block",
                        "--recompress-categories=!pcmaudio/waveform", "-C",
                        "pcmaudio/metadata::ricepp"}))
        << t.err();
    EXPECT_THAT(
        t.err(),
        ::testing::HasSubstr(fmt::format(
            "cannot compress {} compressed block with compressor 'ricepp "
            "[block_size=128]' because the following metadata requirements are "
            "not met: missing requirement 'bytes_per_sample'",
            compression_type)));
  }
#endif

  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress",
                        "--recompress-categories=pcmaudio/metadata,SoMeThInG"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr(
                             "no category 'SoMeThInG' in input filesystem"));
  }

  {
    auto t = tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--recompress", "-C",
                        "SoMeThInG::null"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("unknown category: 'SoMeThInG'"));
  }

  {
    auto t = tester(image);
    EXPECT_EQ(0, t.run({"-i", image_file, "-o", "-", "--recompress=none",
                        "--log-level=verbose", "--no-history"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    EXPECT_TRUE(fs.find("/random"));
    EXPECT_EQ(0, fs.get_history().size());
    EXPECT_EQ(1, fs.info_as_json(history_opts).count("history"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("removing HISTORY"));
  }

  {
    auto corrupt_image = image;
    corrupt_image[64] ^= 0x01; // flip a bit right after the header
    auto t = tester(corrupt_image);
    EXPECT_NE(0, t.run({"-i", image_file, "-o", "-", "--recompress"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("input filesystem is corrupt"));
  }
}

namespace {

constexpr std::array<std::string_view, 2> const source_fs_compression = {
    "zstd:level=5",
    "null",
};

} // namespace

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_recompress_test,
                         ::testing::ValuesIn(source_fs_compression));

class mkdwarfs_build_options_test
    : public testing::TestWithParam<std::string_view> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(mkdwarfs_build_options_test, basic) {
  auto opts = GetParam();
  auto options = test::parse_args(opts);
  std::string const image_file = "test.dwarfs";
  std::vector<std::string> args = {"-i",       "/",  "-o",
                                   image_file, "-C", "zstd:level=9"};
  args.insert(args.end(), options.begin(), options.end());

  auto t = mkdwarfs_tester::create_empty();

  t.add_root_dir();
  t.add_random_file_tree();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);

  ASSERT_EQ(0, t.run(args));

  auto fs = t.fs_from_file(image_file);

  fs.dump(std::cout, {.features = reader::fsinfo_features::for_level(3)});
}

namespace {

constexpr std::array<std::string_view, 8> const build_options = {
    "--categorize --order=none --file-hash=none",
    "--categorize=pcmaudio --order=path",
    "--categorize --order=revpath --file-hash=sha512",
    "--categorize=pcmaudio,incompressible --order=similarity",
    "--categorize --order=nilsimsa --time-resolution=30",
    "--categorize --order=nilsimsa:max-children=1k --time-resolution=hour",
    "--categorize --order=nilsimsa:max-cluster-size=16:max-children=16 "
    "--max-similarity-size=1M",
    "--categorize -B4 -S18",
};

} // namespace

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_build_options_test,
                         ::testing::ValuesIn(build_options));

TEST(mkdwarfs_test, order_invalid) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--order=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid inode order mode"));
}

TEST(mkdwarfs_test, order_nilsimsa_invalid_option) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--order=nilsimsa:grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "invalid option(s) for choice nilsimsa: grmpf"));
}

TEST(mkdwarfs_test, order_nilsimsa_invalid_value) {
  mkdwarfs_tester t;
  EXPECT_NE(0,
            t.run({"-i", "/", "-o", "-", "--order=nilsimsa:max-children=0"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid max-children value: 0"));
}

TEST(mkdwarfs_test, order_nilsimsa_cannot_parse_value) {
  mkdwarfs_tester t;
  EXPECT_NE(
      0, t.run({"-i", "/", "-o", "-", "--order=nilsimsa:max-cluster-size=-1"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot parse size value"));
}

TEST(mkdwarfs_test, order_nilsimsa_duplicate_option) {
  mkdwarfs_tester t;
  EXPECT_NE(0,
            t.run({"-i", "/", "-o", "-",
                   "--order=nilsimsa:max-cluster-size=1:max-cluster-size=10"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "duplicate option max-cluster-size for choice nilsimsa"));
}

TEST(mkdwarfs_test, unknown_file_hash) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--file-hash=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("unknown file hash function"));
}

TEST(mkdwarfs_test, invalid_filter_debug_mode) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--debug-filter=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid filter debug mode"));
}

TEST(mkdwarfs_test, invalid_progress_mode) {
  mkdwarfs_tester t;
  t.iol->set_terminal_is_tty(true);
  t.iol->set_terminal_fancy(true);
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--progress=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid progress mode"));
}

TEST(mkdwarfs_test, invalid_filter_rule) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", "grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("could not parse filter rule"));
}

TEST(mkdwarfs_test, time_resolution_zero) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--time-resolution=0"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("'--time-resolution' must be nonzero"));
}

TEST(mkdwarfs_test, time_resolution_invalid) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--time-resolution=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("'--time-resolution' is invalid"));
}

namespace {

constexpr std::array<std::string_view, 6> const debug_filter_mode_names = {
    "included", "excluded", "included-files", "excluded-files", "files", "all",
};

const std::map<std::string_view, writer::debug_filter_mode> debug_filter_modes{
    {"included", writer::debug_filter_mode::INCLUDED},
    {"included-files", writer::debug_filter_mode::INCLUDED_FILES},
    {"excluded", writer::debug_filter_mode::EXCLUDED},
    {"excluded-files", writer::debug_filter_mode::EXCLUDED_FILES},
    {"files", writer::debug_filter_mode::FILES},
    {"all", writer::debug_filter_mode::ALL},
};

} // namespace

class filter_test : public testing::TestWithParam<
                        std::tuple<test::filter_test_data, std::string_view>> {
};

TEST_P(filter_test, debug_filter) {
  auto [data, mode] = GetParam();
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.fa->set_file("filter.txt", data.filter());
  ASSERT_EQ(0, t.run({"-i", "/", "-F", ". filter.txt",
                      "--debug-filter=" + std::string(mode)}))
      << t.err();
  auto expected = data.get_expected_filter_output(debug_filter_modes.at(mode));
  EXPECT_EQ(expected, t.out());
}

INSTANTIATE_TEST_SUITE_P(
    mkdwarfs_test, filter_test,
    ::testing::Combine(::testing::ValuesIn(dwarfs::test::get_filter_tests()),
                       ::testing::ValuesIn(debug_filter_mode_names)));

TEST(mkdwarfs_test, filter_recursion) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.fa->set_file("filt1.txt", ". filt2.txt\n");
  t.fa->set_file("filt2.txt", ". filt3.txt\n");
  t.fa->set_file("filt3.txt", "# here we recurse\n. filt1.txt\n");
  EXPECT_EQ(1, t.run({"-i", "/", "-o", "-", "-F", ". filt1.txt"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "recursion detected while opening file: filt1.txt"));
}

TEST(mkdwarfs_test, filter_root_dir) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-F", "- /var/", "-F", "- /usr/"}))
      << t.err();
  auto fs = t.fs_from_stdout();
  EXPECT_TRUE(fs.find("/"));
  EXPECT_FALSE(fs.find("/var"));
  EXPECT_FALSE(fs.find("/usr"));
  EXPECT_TRUE(fs.find("/dev"));
  EXPECT_TRUE(fs.find("/etc"));
}

namespace {

constexpr std::array<std::string_view, 9> const pack_mode_names = {
    "chunk_table", "directories",    "shared_files", "names", "names_index",
    "symlinks",    "symlinks_index", "force",        "plain",
};

}

TEST(mkdwarfs_test, pack_modes_random) {
  DWARFS_SLOW_TEST();

  std::mt19937_64 rng{42};
  std::uniform_int_distribution<> dist{1, pack_mode_names.size()};

  for (int i = 0; i < 50; ++i) {
    std::vector<std::string_view> modes(pack_mode_names.begin(),
                                        pack_mode_names.end());
    std::shuffle(modes.begin(), modes.end(), rng);
    modes.resize(dist(rng));
    auto mode_arg = fmt::format("{}", fmt::join(modes, ","));
    auto t = mkdwarfs_tester::create_empty();
    t.add_test_file_tree();
    t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
    ASSERT_EQ(
        0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=" + mode_arg}))
        << t.err();
    auto fs = t.fs_from_stdout();
    auto info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    std::set<std::string> ms(modes.begin(), modes.end());
    std::set<std::string> fsopt;
    for (auto const& opt : info["options"]) {
      fsopt.insert(opt.get<std::string>());
    }
    auto ctx = mode_arg + "\n" +
               fs.dump({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_EQ(ms.count("chunk_table"), fsopt.count("packed_chunk_table"))
        << ctx;
    EXPECT_EQ(ms.count("directories"), fsopt.count("packed_directories"))
        << ctx;
    EXPECT_EQ(ms.count("shared_files"),
              fsopt.count("packed_shared_files_table"))
        << ctx;
    if (ms.count("plain")) {
      EXPECT_EQ(0, fsopt.count("packed_names")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_names_index")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_symlinks")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_symlinks_index")) << ctx;
    }
  }
}

TEST(mkdwarfs_test, pack_mode_none) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=none"}))
      << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
  std::set<std::string> fsopt;
  for (auto const& opt : info["options"]) {
    fsopt.insert(opt.get<std::string>());
  }
  fsopt.erase("mtime_only");
  EXPECT_TRUE(fsopt.empty()) << info["options"].dump();
}

TEST(mkdwarfs_test, pack_mode_all) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=all"}))
      << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
  std::set<std::string> expected = {"packed_chunk_table",
                                    "packed_directories",
                                    "packed_names",
                                    "packed_names_index",
                                    "packed_shared_files_table",
                                    "packed_symlinks_index"};
  std::set<std::string> fsopt;
  for (auto const& opt : info["options"]) {
    fsopt.insert(opt.get<std::string>());
  }
  fsopt.erase("mtime_only");
  EXPECT_EQ(expected, fsopt) << info["options"].dump();
}

TEST(mkdwarfs_test, pack_mode_invalid) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--pack-metadata=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("'--pack-metadata' is invalid"));
}

TEST(mkdwarfs_test, filesystem_header) {
  auto const header = test::loremipsum(333);

  mkdwarfs_tester t;
  t.fa->set_file("header.txt", header);
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "--header=header.txt"})) << t.err();

  auto image = t.out();

  auto fs = t.fs_from_data(
      image, {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});
  auto hdr = fs.header();
  ASSERT_TRUE(hdr);
  std::string actual(reinterpret_cast<char const*>(hdr->data()), hdr->size());
  EXPECT_EQ(header, actual);

  auto os = std::make_shared<test::os_access_mock>();
  os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  os->add_file("image.dwarfs", image);

  {
    dwarfsck_tester t2(os);
    EXPECT_EQ(0, t2.run({"image.dwarfs", "--print-header"})) << t2.err();
    EXPECT_EQ(header, t2.out());
  }

  {
    mkdwarfs_tester t2(os);
    ASSERT_EQ(0, t2.run({"-i", "image.dwarfs", "-o", "-", "--recompress=none",
                         "--remove-header"}))
        << t2.err();

    auto fs2 = t2.fs_from_stdout();
    EXPECT_FALSE(fs2.header());
  }
}

TEST(mkdwarfs_test, filesystem_header_error) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--header=header.txt"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open header file"));
}

TEST(mkdwarfs_test, output_file_exists) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  EXPECT_NE(0, t.run({"-i", "/", "-o", "exists.dwarfs"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("output file already exists"));
}

TEST(mkdwarfs_test, output_file_force) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "exists.dwarfs", "-l1", "--force"}))
      << t.err();
  auto fs = t.fs_from_file("exists.dwarfs");
  EXPECT_TRUE(fs.find("/foo.pl"));
}

TEST(mkdwarfs_test, output_file_fail_open) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  t.fa->set_open_error(
      "exists.dwarfs",
      std::make_error_code(std::errc::device_or_resource_busy));
  EXPECT_NE(0, t.run({"-i", "/", "-o", "exists.dwarfs", "--force"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open output file"));
}

TEST(mkdwarfs_test, output_file_fail_close) {
  mkdwarfs_tester t;
  t.fa->set_close_error("test.dwarfs",
                        std::make_error_code(std::errc::no_space_on_device));
  EXPECT_NE(0, t.run({"-i", "/", "-o", "test.dwarfs"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("failed to close output file"));
}

#ifdef DWARFS_HAVE_RICEPP
TEST(mkdwarfs_test, compression_cannot_be_used_without_category) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-C", "ricepp"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("cannot be used without a category"));
}

TEST(mkdwarfs_test, compression_cannot_be_used_for_category) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--categorize", "-C",
                      "incompressible::ricepp"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "cannot be used for category 'incompressible': "
                           "metadata requirements not met"));
}
#endif

class mkdwarfs_progress_test : public testing::TestWithParam<char const*> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(mkdwarfs_progress_test, basic) {
  std::string mode{GetParam()};
  std::string const image_file = "test.dwarfs";

  std::vector<std::string> args{
      "-i", "/", "-o", image_file, "--file-hash=sha512", "--progress", mode};

  auto t = mkdwarfs_tester::create_empty();

  t.iol->set_terminal_is_tty(true);
  t.iol->set_terminal_fancy(true);

  t.add_root_dir();
  t.add_random_file_tree({
      .avg_size = 20.0 * 1024 * 1024,
      .dimension = 2,
#ifndef _WIN32
      // Windows can't deal with non-UTF-8 filenames
      .with_invalid_utf8 = true,
#endif
  });
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);

  ASSERT_EQ(0, t.run(args));
  EXPECT_TRUE(t.out().empty()) << t.out();
}

namespace {

constexpr std::array const progress_modes{
    "none",
    "simple",
    "ascii",
    "unicode",
};

} // namespace

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_progress_test,
                         ::testing::ValuesIn(progress_modes));

TEST(dwarfsextract_test, mtree) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "mtree"})) << t.err();
  auto out = t.out();
  EXPECT_TRUE(out.starts_with("#mtree")) << out;
  EXPECT_THAT(out, ::testing::HasSubstr("type=dir"));
  EXPECT_THAT(out, ::testing::HasSubstr("type=file"));
}

TEST(dwarfsextract_test, stdout_progress_error) {
  auto t = dwarfsextract_tester::create_with_image();
  EXPECT_NE(0,
            t.run({"-i", "image.dwarfs", "-f", "mtree", "--stdout-progress"}))
      << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "cannot use --stdout-progress with --output=-"));
}

TEST(dwarfsck_test, check_exclusive) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--no-check", "--check-integrity"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "--no-check and --check-integrity are mutually exclusive"));
}

TEST(dwarfsck_test, print_header_and_json) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header", "--json"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::ContainsRegex(
                  "--print-header is mutually exclusive with.*--json"));
}

TEST(dwarfsck_test, print_header) {
  std::string const header = "interesting stuff in the header\n";
  auto image =
      build_test_image({"--header", "header.txt"}, {{"header.txt", header}});

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--print-header"})) << t.err();
    EXPECT_EQ(header, t.out());
  }

  {
    auto t = dwarfsck_tester::create_with_image(image);
    t.iol->out_stream().setstate(std::ios_base::failbit);
    EXPECT_EQ(1, t.run({"image.dwarfs", "--print-header"})) << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr("error writing header"));
  }
}

TEST(dwarfsck_test, check_fail) {
  static constexpr size_t section_header_size{64};
  auto image = build_test_image();

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs"})) << t.err();
  }

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--check-integrity"})) << t.err();
  }

  std::vector<std::pair<std::string, size_t>> section_offsets;

  {
    auto t = dwarfsck_tester::create_with_image(image);
    EXPECT_EQ(0, t.run({"image.dwarfs", "--no-check", "-j", "-d3"})) << t.err();

    auto info = nlohmann::json::parse(t.out());
    ASSERT_TRUE(info.count("sections") > 0) << info;

    size_t offset = 0;

    for (auto const& section : info["sections"]) {
      auto type = section["type"].get<std::string>();
      auto size = section["compressed_size"].get<int>();
      section_offsets.emplace_back(type, offset);
      offset += section_header_size + size;
    }

    EXPECT_EQ(image.size(), offset);
  }

  size_t index = 0;

  for (auto const& [type, offset] : section_offsets) {
    bool const is_metadata_section =
        type == "METADATA_V2" || type == "METADATA_V2_SCHEMA";
    bool const is_block = type == "BLOCK";
    auto corrupt_image = image;
    // flip a bit right after the header
    corrupt_image[offset + section_header_size] ^= 0x01;

    // std::cout << "corrupting section: " << type << " @ " << offset << "\n";

    {
      test::test_logger lgr;
      test::os_access_mock os;
      auto make_fs = [&] {
        return reader::filesystem_v2{
            lgr, os, std::make_shared<test::mmap_mock>(corrupt_image)};
      };
      if (is_metadata_section) {
        EXPECT_THAT([&] { make_fs(); },
                    ::testing::ThrowsMessage<dwarfs::runtime_error>(
                        ::testing::HasSubstr(fmt::format(
                            "checksum error in section: {}", type))));
      } else {
        auto fs = make_fs();
        auto& log = lgr.get_log();
        if (is_block) {
          EXPECT_EQ(0, log.size());
        } else {
          ASSERT_EQ(1, log.size());
          EXPECT_THAT(log[0].output,
                      ::testing::HasSubstr(
                          fmt::format("checksum error in section: {}", type)));
        }
        auto info = fs.info_as_json(
            {.features = reader::fsinfo_features::for_level(3)});
        ASSERT_EQ(1, info.count("sections"));
        ASSERT_EQ(section_offsets.size(), info["sections"].size());
        for (auto const& [i, section] :
             ranges::views::enumerate(info["sections"])) {
          EXPECT_EQ(section["checksum_ok"].get<bool>(), i != index)
              << type << ", " << index;
        }
        auto dump =
            fs.dump({.features = reader::fsinfo_features::for_level(3)});
        EXPECT_THAT(dump, ::testing::HasSubstr("CHECKSUM ERROR"));
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      if (is_metadata_section) {
        EXPECT_EQ(1, t.run({"image.dwarfs", "--no-check", "-j"})) << t.err();
      } else {
        EXPECT_EQ(0, t.run({"image.dwarfs", "--no-check", "-j"})) << t.err();
      }

      // for blocks, we skip checks with --no-check
      if (!is_block) {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "checksum error in section: {}", type)));
      }

      auto json = t.out();

      // std::cout << "[" << type << ", nocheck]\n" << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "-j"})) << t.err();

      EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                               "checksum error in section: {}", type)));

      auto json = t.out();

      // std::cout << "[" << type << "]\n" << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "--check-integrity", "-j"}))
          << t.err();

      if (is_block) {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "integrity check error in section: BLOCK")));
      } else {
        EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                                 "checksum error in section: {}", type)));
      }

      auto json = t.out();

      // std::cout << "[" << type << ", integrity]\n"  << json << "\n";

      if (is_metadata_section) {
        EXPECT_EQ(0, json.size()) << json;
      } else {
        EXPECT_GT(json.size(), 100) << json;
        EXPECT_TRUE(nlohmann::json::accept(json)) << json;
      }
    }

    {
      auto t = dwarfsck_tester::create_with_image(corrupt_image);

      EXPECT_EQ(1, t.run({"image.dwarfs", "-d3"})) << t.err();

      EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt::format(
                               "checksum error in section: {}", type)));

      if (is_metadata_section) {
        EXPECT_EQ(0, t.out().size()) << t.out();
      } else {
        EXPECT_THAT(t.out(), ::testing::HasSubstr("CHECKSUM ERROR"));
      }
    }

    ++index;
  }
}

TEST(dwarfsck_test, print_header_and_export_metadata) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header",
                      "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(
      t.err(),
      ::testing::ContainsRegex(
          "--print-header is mutually exclusive with.*--export-metadata"));
}

TEST(dwarfsck_test, print_header_and_check_integrity) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--print-header", "--check-integrity"}))
      << t.err();
  EXPECT_THAT(
      t.err(),
      ::testing::ContainsRegex(
          "--print-header is mutually exclusive with.*--check-integrity"));
}

TEST(dwarfsck_test, print_header_no_header) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(2, t.run({"image.dwarfs", "--print-header"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("filesystem does not contain a header"));
}

TEST(dwarfsck_test, export_metadata) {
  auto t = dwarfsck_tester::create_with_image();
  ASSERT_EQ(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  auto meta = t.fa->get_file("image.meta");
  ASSERT_TRUE(meta);
  EXPECT_GT(meta->size(), 1000);
  EXPECT_TRUE(nlohmann::json::accept(meta.value())) << meta.value();
}

TEST(dwarfsck_test, export_metadata_open_error) {
  auto t = dwarfsck_tester::create_with_image();
  t.fa->set_open_error(
      "image.meta", std::make_error_code(std::errc::device_or_resource_busy));
  EXPECT_NE(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("failed to open metadata output file"));
}

TEST(dwarfsck_test, export_metadata_close_error) {
  auto t = dwarfsck_tester::create_with_image();
  t.fa->set_close_error("image.meta",
                        std::make_error_code(std::errc::no_space_on_device));
  EXPECT_NE(0, t.run({"image.dwarfs", "--export-metadata=image.meta"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("failed to close metadata output file"));
}

TEST(dwarfsck_test, checksum_algorithm_not_available) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_NE(0, t.run({"image.dwarfs", "--checksum=grmpf"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("checksum algorithm not available: grmpf"));
}

TEST(dwarfsck_test, list_files) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--list"})) << t.err();
  auto out = t.out();

  auto files = split_to<std::set<std::string>>(out, '\n');

  std::set<std::string> const expected{
      "test.pl",     "somelink",      "somedir",   "foo.pl",
      "bar.pl",      "baz.pl",        "ipsum.txt", "somedir/ipsum.py",
      "somedir/bad", "somedir/empty", "empty",
  };

  EXPECT_EQ(expected, files);
}

TEST(dwarfsck_test, list_files_verbose) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--list", "--verbose"})) << t.err();
  auto out = t.out();

  auto num_lines = std::count(out.begin(), out.end(), '\n');
  EXPECT_EQ(12, num_lines);

  std::vector<std::string> expected_re{
      fmt::format("drwxrwxrwx\\s+1000/100\\s+8\\s+{:%Y-%m-%d %H:%M}\\s*\n",
                  fmt::localtime(2)),
      fmt::format(
          "-rw-------\\s+1337/  0\\s+{:L}\\s+{:%Y-%m-%d %H:%M}\\s+baz.pl\n",
          23456, fmt::localtime(8002)),
      fmt::format("lrwxrwxrwx\\s+1000/100\\s+16\\s+{:%Y-%m-%d "
                  "%H:%M}\\s+somelink -> somedir/ipsum.py\n",
                  fmt::localtime(2002)),
  };

  for (auto const& str : expected_re) {
    std::regex re{str};
    EXPECT_TRUE(std::regex_search(out, re)) << "[" << str << "]\n" << out;
  }
}

TEST(dwarfsck_test, checksum_files) {
  auto t = dwarfsck_tester::create_with_image();
  EXPECT_EQ(0, t.run({"image.dwarfs", "--checksum=md5"})) << t.err();
  auto out = t.out();

  auto num_lines = std::count(out.begin(), out.end(), '\n');
  EXPECT_EQ(8, num_lines);

  std::map<std::string, std::string> actual;

  for (auto line : split_view<std::string_view>(out, '\n')) {
    if (line.empty()) {
      continue;
    }
    auto pos = line.find("  ");
    ASSERT_NE(std::string::npos, pos);
    auto hash = line.substr(0, pos);
    auto file = line.substr(pos + 2);
    EXPECT_TRUE(actual.emplace(file, hash).second);
  }

  std::map<std::string, std::string> const expected{
      {"empty", "d41d8cd98f00b204e9800998ecf8427e"},
      {"somedir/empty", "d41d8cd98f00b204e9800998ecf8427e"},
      {"test.pl", "d41d8cd98f00b204e9800998ecf8427e"},
      {"baz.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"somedir/ipsum.py", "70fe813c36ed50ebd7f4991857683676"},
      {"foo.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"bar.pl", "e2bd36391abfd15dcc83cbdfb60a6bc3"},
      {"ipsum.txt", "0782b6a546cedd8be8fc86ac47dc6d96"},
  };

  EXPECT_EQ(expected, actual);
}

class mkdwarfs_sim_order_test : public testing::TestWithParam<char const*> {};

TEST(mkdwarfs_test, max_similarity_size) {
  static constexpr std::array sizes{50, 100, 200, 500, 1000, 2000, 5000, 10000};

  auto make_tester = [] {
    std::mt19937_64 rng{42};
    auto t = mkdwarfs_tester::create_empty();

    t.add_root_dir();

    for (auto size : sizes) {
      auto data = test::create_random_string(size, rng);
      t.os->add_file("/file" + std::to_string(size), data);
    }

    return t;
  };

  auto get_sizes_in_offset_order = [](reader::filesystem_v2 const& fs) {
    std::vector<std::pair<size_t, size_t>> tmp;

    for (auto size : sizes) {
      auto path = "/file" + std::to_string(size);
      auto iv = fs.find(path.c_str());
      assert(iv);
      auto info = fs.get_inode_info(*iv);
      assert(1 == info["chunks"].size());
      auto const& chunk = info["chunks"][0];
      tmp.emplace_back(chunk["offset"].get<int>(), chunk["size"].get<int>());
    }

    std::sort(tmp.begin(), tmp.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });

    std::vector<size_t> sizes;

    std::transform(tmp.begin(), tmp.end(), std::back_inserter(sizes),
                   [](auto const& p) { return p.second; });

    return sizes;
  };

  auto partitioned_sizes = [&](std::vector<size_t> in, size_t max_size) {
    auto mid = std::stable_partition(
        in.begin(), in.end(), [=](auto size) { return size > max_size; });

    std::sort(in.begin(), mid, std::greater<size_t>());

    return in;
  };

  std::vector<size_t> sim_ordered_sizes;
  std::vector<size_t> nilsimsa_ordered_sizes;

  {
    auto t = make_tester();
    ASSERT_EQ(0, t.run("-i / -o - -l0 --order=similarity")) << t.err();
    auto fs = t.fs_from_stdout();
    sim_ordered_sizes = get_sizes_in_offset_order(fs);
  }

  {
    auto t = make_tester();
    ASSERT_EQ(0, t.run("-i / -o - -l0 --order=nilsimsa")) << t.err();
    auto fs = t.fs_from_stdout();
    nilsimsa_ordered_sizes = get_sizes_in_offset_order(fs);
  }

  EXPECT_FALSE(
      std::is_sorted(sim_ordered_sizes.begin(), sim_ordered_sizes.end()));

  static constexpr std::array max_sim_sizes{0,    1,    200,  999,
                                            1000, 1001, 5000, 10000};

  std::set<std::string> nilsimsa_results;

  for (auto max_sim_size : max_sim_sizes) {
    {
      auto t = make_tester();
      EXPECT_EQ(0,
                t.run(fmt::format(
                    "-i / -o - -l0 --order=similarity --max-similarity-size={}",
                    max_sim_size)))
          << t.err();

      auto fs = t.fs_from_stdout();

      auto ordered_sizes = get_sizes_in_offset_order(fs);

      if (max_sim_size == 0) {
        EXPECT_EQ(sim_ordered_sizes, ordered_sizes) << max_sim_size;
      } else {
        auto partitioned = partitioned_sizes(sim_ordered_sizes, max_sim_size);
        EXPECT_EQ(partitioned, ordered_sizes) << max_sim_size;
      }
    }

    {
      auto t = make_tester();
      EXPECT_EQ(0,
                t.run(fmt::format(
                    "-i / -o - -l0 --order=nilsimsa --max-similarity-size={}",
                    max_sim_size)))
          << t.err();

      auto fs = t.fs_from_stdout();

      auto ordered_sizes = get_sizes_in_offset_order(fs);

      nilsimsa_results.insert(fmt::format("{}", fmt::join(ordered_sizes, ",")));

      if (max_sim_size == 0) {
        EXPECT_EQ(nilsimsa_ordered_sizes, ordered_sizes) << max_sim_size;
      } else {
        std::vector<size_t> expected;
        std::copy_if(sizes.begin(), sizes.end(), std::back_inserter(expected),
                     [=](auto size) { return size > max_sim_size; });
        std::sort(expected.begin(), expected.end(), std::greater<size_t>());
        ordered_sizes.resize(expected.size());
        EXPECT_EQ(expected, ordered_sizes) << max_sim_size;
      }
    }
  }

  EXPECT_GE(nilsimsa_results.size(), 3);
}

TEST(mkdwarfs_test, low_memory_limit) {
  {
    mkdwarfs_tester t;
    EXPECT_EQ(
        0, t.run("-i / -o - -l5 --log-level=warn -S 27 --num-workers=8 -L 1g"));
    EXPECT_THAT(t.err(),
                ::testing::Not(::testing::HasSubstr("low memory limit")));
  }

  {
    mkdwarfs_tester t;
    EXPECT_EQ(
        0, t.run("-i / -o - -l5 --log-level=warn -S 28 --num-workers=8 -L 1g"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("low memory limit"));
  }
}

TEST(mkdwarfs_test, recoverable_errors) {
  {
    mkdwarfs_tester t;
    t.os->set_access_fail("/somedir/ipsum.py");
    EXPECT_EQ(2, t.run("-i / -o - -l4")) << t.err();
    EXPECT_THAT(t.err(),
                ::testing::HasSubstr("filesystem created with 1 error"));
  }

  {
    mkdwarfs_tester t;
    t.os->set_access_fail("/somedir/ipsum.py");
    t.os->set_access_fail("/baz.pl");
    EXPECT_EQ(2, t.run("-i / -o - -l4")) << t.err();
    EXPECT_THAT(t.err(),
                ::testing::HasSubstr("filesystem created with 2 errors"));
  }
}

TEST(mkdwarfs_test, filesystem_read_error) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-i / -o -")) << t.err();
  auto fs = t.fs_from_stdout();
  auto iv = fs.find("/somedir");
  ASSERT_TRUE(iv);
  EXPECT_TRUE(iv->is_directory());
  EXPECT_THAT([&] { fs.open(*iv); }, ::testing::Throws<std::system_error>());
  {
    std::error_code ec;
    auto res = fs.open(*iv, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
    EXPECT_EQ(0, res);
  }
  {
    char buf[1];
    std::error_code ec;
    auto num = fs.read(iv->inode_num(), buf, sizeof(buf), ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(0, num);
    EXPECT_EQ(EINVAL, ec.value());
    EXPECT_THAT([&] { fs.read(iv->inode_num(), buf, sizeof(buf)); },
                ::testing::Throws<std::system_error>());
  }
  {
    reader::iovec_read_buf buf;
    std::error_code ec;
    auto num = fs.readv(iv->inode_num(), buf, 42, ec);
    EXPECT_EQ(0, num);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }
  {
    std::error_code ec;
    auto res = fs.readv(iv->inode_num(), 42, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }
  EXPECT_THAT([&] { fs.readv(iv->inode_num(), 42); },
              ::testing::Throws<std::system_error>());
}

class segmenter_repeating_sequence_test : public testing::TestWithParam<char> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(segmenter_repeating_sequence_test, github161) {
  auto byte = GetParam();

  static constexpr int const final_bytes{10'000'000};
  static constexpr int const repetitions{2'000};
  auto match = test::create_random_string(5'000);
  auto suffix = test::create_random_string(50);
  auto sequence = std::string(3'000, byte);

  std::string content;
  content.reserve(match.size() + suffix.size() +
                  (sequence.size() + match.size()) * repetitions + final_bytes);

  content += match + suffix;
  for (int i = 0; i < repetitions; ++i) {
    content += sequence + match;
  }
  content += std::string(final_bytes, byte);

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file("/bug", content);

  ASSERT_EQ(0, t.run("-i / -o - -C lz4 -W12 --log-level=verbose --no-progress"))
      << t.err();

  auto log = t.err();

  {
    std::regex const re{fmt::format(
        "avoided \\d\\d\\d\\d+ collisions in 0x{:02x}-byte sequences", byte)};
    EXPECT_TRUE(std::regex_search(log, re)) << log;
  }

  {
    std::regex const re{"segment matches: good=(\\d+), bad=(\\d+), "
                        "collisions=(\\d+), total=(\\d+)"};
    std::smatch m;

    ASSERT_TRUE(std::regex_search(log, m, re)) << log;

    auto good = std::stoi(m[1]);
    auto bad = std::stoi(m[2]);
    auto collisions = std::stoi(m[3]);
    auto total = std::stoi(m[4]);

    EXPECT_GT(good, 2000);
    EXPECT_EQ(0, bad);
    EXPECT_EQ(0, collisions);
    EXPECT_GT(total, 2000);
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, segmenter_repeating_sequence_test,
                         ::testing::Values('\0', 'G', '\xff'));

TEST(mkdwarfs_test, map_file_error) {
  mkdwarfs_tester t;
  t.os->set_map_file_error(
      "/somedir/ipsum.py",
      std::make_exception_ptr(std::runtime_error("map_file_error")));

  EXPECT_EQ(2, t.run("-i / -o - --categorize")) << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("map_file_error, creating empty inode"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("filesystem created with 1 error"));
}

class map_file_error_test : public testing::TestWithParam<char const*> {};

TEST_P(map_file_error_test, delayed) {
  std::string extra_args{GetParam()};

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  auto files = t.add_random_file_tree({.avg_size = 64.0,
                                       .dimension = 20,
                                       .max_name_len = 8,
                                       .with_errors = true});

  static constexpr size_t const kSizeSmall{1 << 10};
  static constexpr size_t const kSizeLarge{1 << 20};
  auto gen_small = [] { return test::loremipsum(kSizeLarge); };
  auto gen_large = [] { return test::loremipsum(kSizeLarge); };
  t.os->add("large_link1", {43, 0100755, 2, 1000, 100, kSizeLarge, 42, 0, 0, 0},
            gen_large);
  t.os->add("large_link2", {43, 0100755, 2, 1000, 100, kSizeLarge, 42, 0, 0, 0},
            gen_large);
  t.os->add("small_link1", {44, 0100755, 2, 1000, 100, kSizeSmall, 42, 0, 0, 0},
            gen_small);
  t.os->add("small_link2", {44, 0100755, 2, 1000, 100, kSizeSmall, 42, 0, 0, 0},
            gen_small);
  for (auto const& link :
       {"large_link1", "large_link2", "small_link1", "small_link2"}) {
    t.os->set_map_file_error(
        fs::path{"/"} / link,
        std::make_exception_ptr(std::runtime_error("map_file_error")), 0);
  }

  {
    std::mt19937_64 rng{42};

    for (auto const& p : fs::recursive_directory_iterator(audio_data_dir)) {
      if (p.is_regular_file()) {
        auto fp = fs::relative(p.path(), audio_data_dir);
        files.emplace_back(fp, read_file(p.path()));

        if (rng() % 2 == 0) {
          t.os->set_map_file_error(
              fs::path{"/"} / fp,
              std::make_exception_ptr(std::runtime_error("map_file_error")),
              rng() % 4);
        }
      }
    }
  }

  t.os->setenv("DWARFS_DUMP_INODES", "inodes.dump");
  // t.iol->use_real_terminal(true);

  std::string args = "-i / -o test.dwarfs --no-progress --log-level=verbose";
  if (!extra_args.empty()) {
    args += " " + extra_args;
  }

  EXPECT_EQ(2, t.run(args)) << t.err();

  auto fs = t.fs_from_file("test.dwarfs", {.metadata = {.enable_nlink = true}});
  // fs.dump(std::cout, {.features = reader::fsinfo_features::for_level(2)});

  {
    auto large_link1 = fs.find("/large_link1");
    auto large_link2 = fs.find("/large_link2");
    auto small_link1 = fs.find("/small_link1");
    auto small_link2 = fs.find("/small_link2");

    ASSERT_TRUE(large_link1);
    ASSERT_TRUE(large_link2);
    ASSERT_TRUE(small_link1);
    ASSERT_TRUE(small_link2);
    EXPECT_EQ(large_link1->inode_num(), large_link2->inode_num());
    EXPECT_EQ(small_link1->inode_num(), small_link2->inode_num());
    EXPECT_EQ(0, fs.getattr(*large_link1).size());
    EXPECT_EQ(0, fs.getattr(*small_link1).size());
  }

  std::unordered_map<fs::path, std::string, fs_path_hash> actual_files;
  fs.walk([&](auto const& dev) {
    auto iv = dev.inode();
    if (iv.is_regular_file()) {
      std::string data;
      auto stat = fs.getattr(iv);
      data.resize(stat.size());
      ASSERT_EQ(data.size(), fs.read(iv.inode_num(), data.data(), data.size()));
      ASSERT_TRUE(actual_files.emplace(dev.fs_path(), std::move(data)).second);
    }
  });

  // check that:
  // - all original files are present
  // - they're either empty (in case of errors) or have the original content

  size_t num_non_empty = 0;

  auto failed_expected = t.os->get_failed_paths();
  std::set<fs::path> failed_actual;

  for (auto const& [path, data] : files) {
    auto it = actual_files.find(path);
    ASSERT_NE(actual_files.end(), it);
    if (!it->second.empty()) {
      EXPECT_EQ(data, it->second);
      ++num_non_empty;
    } else if (!data.empty()) {
      failed_actual.insert(fs::path("/") / path);
    } else {
      failed_expected.erase(fs::path("/") / path);
    }
  }

  EXPECT_LE(failed_actual.size(), failed_expected.size());

  EXPECT_GT(files.size(), 8000);
  EXPECT_GT(num_non_empty, 4000);

  // Ensure that files which never had any errors are all present

  std::set<fs::path> surprisingly_missing;

  std::set_difference(
      failed_actual.begin(), failed_actual.end(), failed_expected.begin(),
      failed_expected.end(),
      std::inserter(surprisingly_missing, surprisingly_missing.begin()));

  std::unordered_map<fs::path, std::string, fs_path_hash> original_files(
      files.begin(), files.end());

  EXPECT_EQ(0, surprisingly_missing.size());

  for (auto const& path : surprisingly_missing) {
    std::cout << "surprisingly missing: " << path << "\n";
    auto it = original_files.find(path.relative_path());
    ASSERT_NE(original_files.end(), it);
    std::cout << "--- original (" << it->second.size() << " bytes) ---\n";
    // std::cout << folly::hexDump(it->second.data(), it->second.size()) <<
    // "\n";
  }

  auto dump = t.fa->get_file("inodes.dump");
  ASSERT_TRUE(dump);
  if (extra_args.find("--file-hash=none") == std::string::npos) {
    EXPECT_THAT(dump.value(), ::testing::HasSubstr("(invalid)"))
        << dump.value();
  }
  if (extra_args.find("--order=revpath") != std::string::npos) {
    EXPECT_THAT(dump.value(), ::testing::HasSubstr("similarity: none"))
        << dump.value();
  } else {
    EXPECT_THAT(dump.value(), ::testing::HasSubstr("similarity: nilsimsa"))
        << dump.value();
  }
  if (extra_args.find("--categorize") != std::string::npos) {
    EXPECT_THAT(dump.value(), ::testing::HasSubstr("[incompressible]"))
        << dump.value();
  }
}

namespace {

std::array const map_file_error_args{
    "",
    "--categorize",
    "--order=revpath",
    "--order=revpath --categorize",
    "--file-hash=none",
    "--file-hash=none --categorize",
    "--file-hash=none --order=revpath",
    "--file-hash=none --order=revpath --categorize",
};

} // namespace

INSTANTIATE_TEST_SUITE_P(dwarfs, map_file_error_test,
                         ::testing::ValuesIn(map_file_error_args));

TEST(block_cache, sequential_access_detector) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  auto paths = t.add_random_file_tree({.avg_size = 4096.0, .dimension = 10});
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "-S14", "--file-hash=none"}))
      << t.err();
  auto image = t.out();

  std::sort(paths.begin(), paths.end(), [](auto const& a, auto const& b) {
    return a.first.string() < b.first.string();
  });

  t.lgr = std::make_unique<test::test_logger>(logger::VERBOSE);
  auto test_lgr = dynamic_cast<test::test_logger*>(t.lgr.get());

  for (size_t thresh : {0, 1, 2, 4, 8, 16, 32}) {
    size_t block_count{0};
    test_lgr->clear();

    {
      auto fs = t.fs_from_data(
          image,
          {.block_cache = {.max_bytes = 256 * 1024,
                           .sequential_access_detector_threshold = thresh}});
      auto info =
          fs.info_as_json({.features = reader::fsinfo_features::for_level(3)});
      for (auto const& s : info["sections"]) {
        if (s["type"] == "BLOCK") {
          ++block_count;
        }
      }

      for (auto const& [path, data] : paths) {
        auto pstr = path.string();
#ifdef _WIN32
        std::replace(pstr.begin(), pstr.end(), '\\', '/');
#endif
        auto iv = fs.find(pstr.c_str());
        ASSERT_TRUE(iv);
        ASSERT_TRUE(iv->is_regular_file());
        auto st = fs.getattr(*iv);
        ASSERT_EQ(data.size(), st.size());
        std::string buffer;
        buffer.resize(data.size());
        auto nread = fs.read(iv->inode_num(), buffer.data(), st.size());
        EXPECT_EQ(data.size(), nread);
        EXPECT_EQ(data, buffer);
      }
    }

    auto log = test_lgr->get_log();
    std::optional<size_t> sequential_prefetches;
    for (auto const& ent : log) {
      if (ent.output.starts_with("sequential prefetches: ")) {
        sequential_prefetches = std::stoul(ent.output.substr(23));
        break;
      }
    }

    ASSERT_TRUE(sequential_prefetches);
    if (thresh == 0) {
      EXPECT_EQ(0, sequential_prefetches.value());
    } else {
      EXPECT_EQ(sequential_prefetches.value(), block_count - thresh);
    }
  }
}

TEST(file_scanner, large_file_handling) {
  using namespace std::chrono_literals;

  /*
    We have 5 files, each 1MB in size. Files 0 and 3 are identical, as are
    files 1, 2 and 4. In order to reproduce the bug from github #217, we must
    ensure the following order of events. Note that this description is only
    accurate for the old, buggy code.

    [10ms] `f0` is discovered; the first 4K are hashed; unique_size_ is
           updated with (s, h0) -> f0; inode i0 is created

    [20ms] `f1` is discovered; the first 4K are hashed; unique_size_ is
           updated with (s, h1) -> f1; inode i1 is created

    [30ms] `f2` is discovered; the first 4K are hashed; (s, h2) == (s, h1)
           is found in unique_size_; latch l0 is created in slot s; a hash
           job is started for f1; unique_size_[(s, h2)] -> []; a hash job is
           started for f2

    [40ms] `f3` is discovered; the first 4K are hashed; (s, h3) == (s, h0)
           is found in unique_size_; latch l1 is created but cannot be
           stored in slot s because it's occupied by l0; a hash job is
           started for f0; unique_size_[(s, h3)] -> []; a hash job is
           started for f3

    [50ms] `f4` is discovered; the first 4K are hashed; (s, h4) == (s, h0)
           is found in unique_size_; latch l0 is found in slot s [where we
           would have rather expected l1]; a hash job is started for f4

    [60ms] the hash job for f1 completes; latch l0 is released; f1 (i1) is
           added to `by_hash_`; latch l0 is removed from slot s

    [70ms] the hash job for f4 completes; latch l0 has already been released;
           the hash for f4 is not in `by_hash_`; a new inode i2 is created;
           f4 (i2) is added to `by_hash_` [THIS IS THE BUG]

    [80ms] the hash job for f0 completes; latch l1 is released; the hash for
           f0 is already in `by_hash_` [per f4, which shouldn't be there yet];
           f0 (i0) is added to `by_hash_`; an attempt is made to remove latch
           l1 from slot s [but it's not there, which isn't checked]

    [90ms] the hash job for f2 completes; latch l0 has already been released;
           the hash for f2 == f1 is already in `by_hash_`; f2 (i1) is added
           [this is irrelevant]

   [100ms] the hash job for f3 completes; latch l1 has already been released;
           the hash for f3 == f0 is already in `by_hash_`; f3 (i0) is added
           [this is irrelevant]
  */

  std::vector<std::string> data(5, test::loremipsum(1 << 20));
  std::vector<std::chrono::milliseconds> delays{40ms, 30ms, 60ms, 60ms, 20ms};

  data[1][100] ^= 0x01;
  data[2][100] ^= 0x01;

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();

  for (size_t i = 0; i < data.size(); ++i) {
    auto file = fmt::format("f{}", i);
    t.os->add_file(file, data[i]);
    t.os->set_map_file_delay(fs::path{"/"} / file, delays[i]);
  }

  t.os->set_map_file_delay_min_size(10'000);
  t.os->set_dir_reader_delay(10ms);

  ASSERT_EQ(0, t.run("-i / -o - -l1")) << t.err();

  auto fs = t.fs_from_stdout();

  for (size_t i = 0; i < data.size(); ++i) {
    auto iv = fs.find(fmt::format("f{}", i).c_str());
    ASSERT_TRUE(iv) << i;
    auto st = fs.getattr(*iv);
    std::string buffer;
    buffer.resize(st.size());
    auto nread = fs.read(iv->inode_num(), buffer.data(), st.size());
    EXPECT_EQ(data[i].size(), nread) << i;
    EXPECT_EQ(data[i], buffer) << i;
  }
}

TEST(mkdwarfs_test, file_scanner_dump) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.add_random_file_tree({.avg_size = 1024.0, .dimension = 10});

  t.os->setenv("DWARFS_DUMP_FILES_RAW", "raw.json");
  t.os->setenv("DWARFS_DUMP_FILES_FINAL", "final.json");

  ASSERT_EQ(0, t.run("-l1 -i / -o -")) << t.err();

  auto raw = t.fa->get_file("raw.json");
  ASSERT_TRUE(raw);
  EXPECT_GT(raw->size(), 100'000);
  EXPECT_TRUE(nlohmann::json::accept(raw.value())) << raw.value();

  auto finalized = t.fa->get_file("final.json");
  ASSERT_TRUE(finalized);
  EXPECT_GT(finalized->size(), 100'000);
  EXPECT_TRUE(nlohmann::json::accept(finalized.value())) << finalized.value();

  EXPECT_NE(*raw, *finalized);
}

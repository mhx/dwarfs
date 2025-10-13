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

#include <ranges>
#include <regex>

#include <gmock/gmock.h>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <nlohmann/json.hpp>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/file_util.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/iovec_read_buf.h>
#include <dwarfs/writer/filter_debug.h>

#include "filter_test_data.h"
#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

using namespace std::literals::string_view_literals;

namespace fs = std::filesystem;

class mkdwarfs_build_options_test
    : public testing::TestWithParam<std::string_view> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(mkdwarfs_build_options_test, basic) {
  auto opts = GetParam();
  auto options = test::parse_args(opts);
  std::string const image_file = "test.dwarfs";
  std::vector<std::string> args = {
      "-i", "/", "-o", image_file, "-C", "zstd:level=9", "--log-level=debug"};
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

namespace {

constexpr std::array<std::string_view, 6> const debug_filter_mode_names = {
    "included", "excluded", "included-files", "excluded-files", "files", "all",
};

std::map<std::string_view, writer::debug_filter_mode> const debug_filter_modes{
    {"included", writer::debug_filter_mode::INCLUDED},
    {"included-files", writer::debug_filter_mode::INCLUDED_FILES},
    {"excluded", writer::debug_filter_mode::EXCLUDED},
    {"excluded-files", writer::debug_filter_mode::EXCLUDED_FILES},
    {"files", writer::debug_filter_mode::FILES},
    {"all", writer::debug_filter_mode::ALL},
};

} // namespace

class tool_filter_test
    : public testing::TestWithParam<
          std::tuple<test::filter_test_data, std::string_view>> {};

TEST_P(tool_filter_test, debug_filter) {
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
    mkdwarfs_test, tool_filter_test,
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
  EXPECT_EQ(header, hdr->as_string());

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
  auto dev = fs.find("/somedir");
  ASSERT_TRUE(dev);
  auto iv = dev->inode();
  EXPECT_TRUE(iv.is_directory());
  EXPECT_THAT([&] { fs.open(iv); }, ::testing::Throws<std::system_error>());
  {
    std::error_code ec;
    auto res = fs.open(iv, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
    EXPECT_EQ(0, res);
  }
  {
    char buf[1];
    std::error_code ec;
    auto num = fs.read(iv.inode_num(), buf, sizeof(buf), ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(0, num);
    EXPECT_EQ(EINVAL, ec.value());
    EXPECT_THAT([&] { fs.read(iv.inode_num(), buf, sizeof(buf)); },
                ::testing::Throws<std::system_error>());
  }
  {
    reader::iovec_read_buf buf;
    std::error_code ec;
    auto num = fs.readv(iv.inode_num(), buf, 42, ec);
    EXPECT_EQ(0, num);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }
  {
    std::error_code ec;
    auto res = fs.readv(iv.inode_num(), 42, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }
  EXPECT_THAT([&] { fs.readv(iv.inode_num(), 42); },
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

  ASSERT_EQ(
      0,
      t.run("-i / -o - -C zstd:level=3 -W12 --log-level=verbose --no-progress"))
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

TEST(mkdwarfs_test, force_segmenter_collisions) {
  // don't go overboard, otherwise this is too slow
  static constexpr int const final_bytes{100'000};
  static constexpr int const repetitions{50};
  auto match = test::create_random_string(5'000);
  auto suffix = test::create_random_string(50);
  std::string sequence;
  sequence.reserve(3000);
  for (int i = 0; i < 1500; ++i) {
    sequence += "ab";
  }

  std::string content;
  content.reserve(match.size() + suffix.size() +
                  (sequence.size() + match.size()) * repetitions + final_bytes);

  content += match + suffix;
  for (int i = 0; i < repetitions; ++i) {
    content += sequence + match;
  }
  for (int i = 0; i < final_bytes; i += 2) {
    content += "ab";
  }

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file("/bug", content);

  ASSERT_EQ(
      0,
      t.run("-i / -o - -C zstd:level=3 -W12 --log-level=verbose --no-progress"))
      << t.err();
}

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
  DWARFS_SLOW_TEST();

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

  auto fs = t.fs_from_file("test.dwarfs");
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
    EXPECT_EQ(large_link1->inode().inode_num(),
              large_link2->inode().inode_num());
    EXPECT_EQ(small_link1->inode().inode_num(),
              small_link2->inode().inode_num());
    EXPECT_EQ(0, fs.getattr(large_link1->inode()).size());
    EXPECT_EQ(0, fs.getattr(small_link1->inode()).size());
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
        auto dev = fs.find(pstr);
        ASSERT_TRUE(dev);
        auto iv = dev->inode();
        ASSERT_TRUE(iv.is_regular_file());
        auto st = fs.getattr(iv);
        ASSERT_EQ(data.size(), st.size());
        std::string buffer;
        buffer.resize(data.size());
        auto nread = fs.read(iv.inode_num(), buffer.data(), st.size());
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
    auto dev = fs.find(fmt::format("f{}", i));
    ASSERT_TRUE(dev) << i;
    auto iv = dev->inode();
    auto st = fs.getattr(iv);
    std::string buffer;
    buffer.resize(st.size());
    auto nread = fs.read(iv.inode_num(), buffer.data(), st.size());
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
      auto dev = fs.find(path);
      assert(dev);
      auto info = fs.get_inode_info(dev->inode());
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

namespace {

std::array const fragment_orders{
    std::pair{"none"sv, "a/c,b,c/a,c/d,e"sv},
    std::pair{"path"sv, "a/c,b,c/a,c/d,e"sv},
    std::pair{"revpath"sv, "c/a,b,a/c,c/d,e"sv},
    std::pair{"explicit:file=order.dat"sv, "c/d,b,a/c,e,c/a"sv},
};

} // namespace

class fragment_order_test : public testing::TestWithParam<
                                std::pair<std::string_view, std::string_view>> {
};

TEST_P(fragment_order_test, basic) {
  auto [option, expected] = GetParam();
  std::string const image_file = "test.dwarfs";

  auto t = mkdwarfs_tester::create_empty();

  t.fa->set_file("order.dat", "c/d\nb\na/c\ne\nc/a\n");

  t.add_root_dir();
  t.os->add_dir("a");
  t.os->add_file("a/c", 2, true);
  t.os->add_file("b", 4, true);
  t.os->add_dir("c");
  t.os->add_file("c/a", 8, true);
  t.os->add_file("c/d", 16, true);
  t.os->add_file("e", 32, true);

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--log-level=verbose",
                      "--order=" + std::string{option}, "-B0"}))
      << t.err();

  auto fs = t.fs_from_file(image_file);

  std::vector<std::pair<std::string, size_t>> file_offsets;

  fs.walk([&](auto const& dev) {
    auto iv = dev.inode();
    if (iv.is_regular_file()) {
      auto info = fs.get_inode_info(iv);
      file_offsets.emplace_back(
          dev.unix_path(), info["chunks"][0]["offset"].template get<size_t>());
    }
  });

  EXPECT_EQ(file_offsets.size(), 5);

  if (option == "none") {
    // just make sure everything is there, order doesn't matter
    std::ranges::sort(file_offsets, std::less{},
                      &std::pair<std::string, size_t>::first);
  } else {
    std::ranges::sort(file_offsets, std::less{},
                      &std::pair<std::string, size_t>::second);
  }

  auto got = file_offsets | ranges::views::keys | ranges::views::join(","sv) |
             ranges::to<std::string>();

  EXPECT_EQ(expected, got) << option;
}

INSTANTIATE_TEST_SUITE_P(dwarfs, fragment_order_test,
                         ::testing::ValuesIn(fragment_orders));

class mkdwarfs_progress_test : public testing::TestWithParam<char const*> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(mkdwarfs_progress_test, basic) {
  std::string mode{GetParam()};
  std::string const image_file = "test.dwarfs";

  std::vector<std::string> args{"-i",
                                "/",
                                "-o",
                                image_file,
                                "-l1",
                                "--file-hash=sha512",
                                "--categorize",
                                "--incompressible-zstd-level=19",
                                "--order=nilsimsa",
                                "--progress",
                                mode};

  auto t = mkdwarfs_tester::create_empty();

  t.iol->set_terminal_is_tty(true);
  t.iol->set_terminal_fancy(true);

  t.add_root_dir();
  t.add_random_file_tree({
#ifdef DWARFS_TEST_CROSS_COMPILE
      .avg_size = 2.0 * 1024 * 1024,
#else
      .avg_size = 16.0 * 1024 * 1024,
#endif
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

TEST(mkdwarfs_test, hotness_categorizer) {
  std::string const image_file = "test.dwarfs";

  std::ostringstream hot_files;
  hot_files << "foo.pl" << '\n' << "ipsum.txt" << '\n';

  mkdwarfs_tester t;

  t.fa->set_file("hot", hot_files.str());

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize=hotness",
                      "--hotness-list=hot", "-B0", "-l1", "--log-level=debug"}))
      << t.err();

  auto fs = t.fs_from_file(image_file);

  {
    auto dev = fs.find("/foo.pl");
    ASSERT_TRUE(dev) << t.err();
    auto info = fs.get_inode_info(dev->inode());
    EXPECT_EQ(info["chunks"][0]["category"].get<std::string>(), "hotness");
  }

  {
    auto dev = fs.find("/ipsum.txt");
    ASSERT_TRUE(dev) << t.err();
    auto info = fs.get_inode_info(dev->inode());
    EXPECT_EQ(info["chunks"][0]["category"].get<std::string>(), "hotness");
  }

  {
    auto dev = fs.find("/somedir/ipsum.py");
    ASSERT_TRUE(dev) << t.err();
    auto info = fs.get_inode_info(dev->inode());
    EXPECT_EQ(info["chunks"][0]["category"].get<std::string>(), "<default>");
  }
}

TEST(mkdwarfs_test, hotness_categorizer_cannot_open_hotness_file) {
  mkdwarfs_tester t;

  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--categorize=hotness",
                      "--hotness-list=hot", "-B0"}))
      << t.err();

  EXPECT_THAT(t.err(), ::testing::HasSubstr("failed to open file 'hot':"));
}

TEST(mkdwarfs_test, hotness_categorizer_empty_hotness_list) {
  mkdwarfs_tester t;

  t.fa->set_file("hot", "");

  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "--categorize=hotness",
                      "--hotness-list=hot", "-B0", "-l1"}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("hotness categorizer: empty hotness list"));
}

TEST(mkdwarfs_test, hotness_categorizer_no_hotness_list_provided) {
  mkdwarfs_tester t;

  EXPECT_EQ(0,
            t.run({"-i", "/", "-o", "-", "--categorize=hotness", "-B0", "-l1"}))
      << t.err();

  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "hotness categorizer: no hotness list provided"));
}

TEST(mkdwarfs_test, hotness_categorizer_duplicate_path_in_hotness_list) {
  std::ostringstream hot_files;
  hot_files << "foo.pl" << '\n' << "ipsum.txt" << '\n' << "foo.pl" << '\n';

  mkdwarfs_tester t;

  t.fa->set_file("hot", hot_files.str());

  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--categorize=hotness",
                      "--hotness-list=hot", "-B0"}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("duplicate path in hotness list: 'foo.pl'"));
}

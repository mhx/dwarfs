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

#include <gmock/gmock.h>

#include <fmt/format.h>

#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/sorted_array_map.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

class mkdwarfs_input_list_test
    : public testing::TestWithParam<std::tuple<input_mode, path_type>> {};

TEST_P(mkdwarfs_input_list_test, basic) {
  using namespace std::string_view_literals;
  static constexpr sorted_array_map input_lists{
      std::pair{path_type::absolute,
                "/somelink\n/foo.pl\n/somedir/ipsum.py\n"sv},
      std::pair{path_type::relative, "somelink\nfoo.pl\nsomedir/ipsum.py\n"sv},
      std::pair{path_type::mixed, "somelink\n/foo.pl\nsomedir/ipsum.py\n"sv},
  };

  auto [mode, type] = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list{input_lists.at(type)};
  mkdwarfs_tester t;
  std::string input_file;

  if (mode == input_mode::from_file) {
    input_file = "input_list.txt";
    t.fa->set_file(input_file, input_list);
  } else {
    input_file = "-";
    t.iol->set_in(input_list);
  }

  std::vector<std::string> args = {"--input-list", input_file, "-o", image_file,
                                   "--log-level=trace"};
  if (type != path_type::relative) {
    args.push_back("-i");
    args.push_back("/");
  }

  ASSERT_EQ(0, t.run(args)) << t.err();

  std::ostringstream oss;
  t.add_stream_logger(oss, logger::DEBUG);

  auto fs = t.fs_from_file(image_file);

  auto link = fs.find("/somelink");
  auto foo = fs.find("/foo.pl");
  auto ipsum = fs.find("/somedir/ipsum.py");

  ASSERT_TRUE(link);
  ASSERT_TRUE(foo);
  ASSERT_TRUE(ipsum);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(link->inode().is_symlink());
  EXPECT_TRUE(foo->inode().is_regular_file());
  EXPECT_TRUE(ipsum->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "somelink", "foo.pl", "somedir",
                                       fs::path("somedir") / "ipsum.py"};
  std::set<fs::path> actual;
  fs.walk([&](auto const& e) { actual.insert(e.fs_path()); });

  EXPECT_EQ(expected, actual);
}

TEST_P(mkdwarfs_input_list_test, with_abs_input_dir) {
  using namespace std::string_view_literals;
  static constexpr sorted_array_map input_lists{
      std::pair{path_type::absolute,
                "/somedir/ipsum.py\n/somedir/empty\n/foo/bar\n"sv},
      std::pair{path_type::relative, "ipsum.py\nempty\n"sv},
      std::pair{path_type::mixed, "/somedir/ipsum.py\nempty\n"sv},
  };

  auto [mode, type] = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list{input_lists.at(type)};
  mkdwarfs_tester t;
  std::string input_file;

  if (mode == input_mode::from_file) {
    input_file = "input_list.txt";
    t.fa->set_file(input_file, input_list);
  } else {
    input_file = "-";
    t.iol->set_in(input_list);
  }

  ASSERT_EQ(0, t.run({"--input-list", input_file, "-i", "/somedir", "-o",
                      image_file, "--log-level=trace"}))
      << t.err();

  if (type == path_type::absolute) {
    EXPECT_THAT(
        t.err(),
        ::testing::HasSubstr(
            "ignoring path '/foo/bar' not below input path '/somedir'"));
  } else {
    EXPECT_THAT(t.err(), ::testing::Not(::testing::HasSubstr("ignoring path")));
  }

  std::ostringstream oss;
  t.add_stream_logger(oss, logger::DEBUG);

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/ipsum.py");
  auto empty = fs.find("/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "ipsum.py", "empty"};
  std::set<fs::path> actual;
  fs.walk([&](auto const& e) { actual.insert(e.fs_path()); });

  EXPECT_EQ(expected, actual);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_input_list_test,
                         ::testing::Combine(::testing::ValuesIn(input_modes),
                                            ::testing::ValuesIn(path_types)));

TEST(mkdwarfs_test, input_list_with_rel_input_dir) {
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "ipsum.py\nipsum.py\nempty\n";
  mkdwarfs_tester t;

  t.iol->set_in(input_list);

  ASSERT_EQ(0, t.run({"--input-list", "-", "-i", "somedir", "-o", image_file,
                      "--log-level=trace"}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "skipping duplicate entry 'ipsum.py' in input list"));

  std::ostringstream oss;
  t.add_stream_logger(oss, logger::DEBUG);

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/ipsum.py");
  auto empty = fs.find("/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "ipsum.py", "empty"};
  std::set<fs::path> actual;
  fs.walk([&](auto const& e) { actual.insert(e.fs_path()); });

  EXPECT_EQ(expected, actual);
}

TEST(mkdwarfs_test, input_list_abs_list_path_requires_abs_root_dir) {
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "/ipsum.py\n";
  mkdwarfs_tester t;

  t.iol->set_in(input_list);

  EXPECT_NE(0, t.run({"--input-list", "-", "-i", "somedir", "-o", image_file,
                      "--log-level=trace"}))
      << t.err();

  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "absolute paths in input list require absolute "
                           "input path, but input path is 'somedir'"));
}

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

TEST(mkdwarfs_test, metadata_inode_info) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);

  ASSERT_EQ(0, t.run("-l3 -i / -o - --categorize -S10")) << t.err();

  auto fs = t.fs_from_stdout();

  {
    auto dev = fs.find("/test8.aiff");
    ASSERT_TRUE(dev);

    auto info = fs.get_inode_info(dev->inode());
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    EXPECT_GE(info["chunks"].size(), 2);

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
    auto dev = fs.find("/test.fits");
    ASSERT_TRUE(dev);

    auto info = fs.get_inode_info(dev->inode());
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    auto chunk_count = info["chunks"].size();

    EXPECT_GE(chunk_count, 12);

    for (auto chunk : info["chunks"]) {
      ASSERT_TRUE(chunk.count("category") > 0);
      categories.insert(chunk["category"].get<std::string>());
    }

    std::set<std::string> expected{
        "fits/image",
        "fits/metadata",
    };

    EXPECT_EQ(expected, categories);

    info = fs.get_inode_info(dev->inode(), 5);
    ASSERT_TRUE(info.count("chunks") > 0);

    EXPECT_EQ(fmt::format("too many chunks ({})", chunk_count),
              info["chunks"].get<std::string>());
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

  auto dev = fs.find(d1.string());
  ASSERT_TRUE(dev);
  auto iv = dev->inode();

  EXPECT_EQ(iv.mode_string(), "---drwxr-xr-x");
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

  EXPECT_EQ(d1->inode().mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d2->inode().mode_string(), "----rw-------");
  EXPECT_EQ(d3->inode().mode_string(), "---lrwxrwxrwx");
  EXPECT_EQ(d4->inode().mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d5->inode().mode_string(), "---prw-r--r--");
  EXPECT_EQ(d6->inode().mode_string(), "---crw-rw-rw-");
  EXPECT_EQ(d7->inode().mode_string(), "U---rwxr-xr-x");
  EXPECT_EQ(d8->inode().mode_string(), "-G--rwxr-xr-x");
  EXPECT_EQ(d9->inode().mode_string(), "--S-rwxr-xr-x");
  EXPECT_EQ(d10->inode().mode_string(), "---brw-rw-rw-");
  EXPECT_EQ(d11->inode().mode_string(), "---srw-rw-rw-");
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

  auto dev = fs.find("/block");
  ASSERT_TRUE(dev);

  std::error_code ec;
  auto stat = fs.getattr(dev->inode(), ec);
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

  auto dev = fs.find("/suid");
  ASSERT_TRUE(dev);

  std::error_code ec;
  auto stat = fs.getattr(dev->inode(), ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(3300, stat.atime());
  EXPECT_EQ(2220, stat.mtime());
  EXPECT_EQ(1080, stat.ctime());
}

TEST(mkdwarfs_test, metadata_readdir) {
  mkdwarfs_tester t;
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  auto dev = fs.find("/somedir");
  ASSERT_TRUE(dev);
  auto iv = dev->inode();

  auto dir = fs.opendir(iv);
  ASSERT_TRUE(dir);

  {
    auto r = fs.readdir(dir.value(), 0);
    ASSERT_TRUE(r);

    EXPECT_EQ(".", r->name());
    EXPECT_EQ(r->inode().inode_num(), iv.inode_num());
  }

  {
    auto r = fs.readdir(dir.value(), 1);
    ASSERT_TRUE(r);

    EXPECT_EQ("..", r->name());

    auto parent = fs.find("/");
    ASSERT_TRUE(parent);
    EXPECT_EQ(r->inode().inode_num(), parent->inode().inode_num());
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
    auto dev = fs.find(path);
    ASSERT_TRUE(dev) << path;

    auto dir = fs.opendir(dev->inode());
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

    auto dev = fs.find("/access");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();

    EXPECT_TRUE(fs.access(iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(iv, R_OK, 1, 1));
    EXPECT_TRUE(fs.access(iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(iv, R_OK, 1, 3333));
    EXPECT_TRUE(fs.access(iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(iv, R_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, x_ok, 222, 7));
  }

  {
    auto fs = t.fs_from_stdout({.metadata = {.readonly = true}});

    auto dev = fs.find("/access");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();

    EXPECT_TRUE(fs.access(iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(iv, R_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(iv, R_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(iv, R_OK, 222, 7));
    EXPECT_FALSE(fs.access(iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, x_ok, 222, 7));
  }
}

TEST(mkdwarfs_test, chmod_errors) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=invalid"}));
  EXPECT_THAT(
      t.err(),
      ::testing::HasSubstr(
          "invalid metadata option: missing whom in chmod mode: invalid"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=a+r,"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "invalid metadata option: empty chmod mode"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=,a+r"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "invalid metadata option: empty chmod mode"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=1799"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "invalid metadata option: invalid octal chmod mode: 1799"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=-1799"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("invalid metadata option: invalid octal "
                                   "chmod mode after operation: -1799"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=u+777"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("invalid metadata option: cannot combine "
                                   "whom with octal chmod mode: u+777"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=u+"}));
  EXPECT_THAT(
      t.err(),
      ::testing::HasSubstr(
          "invalid metadata option: missing permissions in chmod mode: u+"));

  t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--chmod=u+wpp"}));
  EXPECT_THAT(
      t.err(),
      ::testing::HasSubstr(
          "invalid metadata option: trailing characters in chmod mode: u+wpp"));
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

TEST(mkdwarfs_test, rules_must_start_with_plus_or_minus) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", "% *"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("rules must start with + or -"));
}

TEST(mkdwarfs_test, empty_filter_rule) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", ""}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("empty filter rule"));
}

TEST(mkdwarfs_test, invalid_filter_rule) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", "+i"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid filter rule"));
}

TEST(mkdwarfs_test, no_pattern_in_filter_rule) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", "+  "}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("no pattern in filter rule"));
}

TEST(mkdwarfs_test, no_prefix_in_filter_rule) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", " foo"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("no prefix in filter rule"));
}

TEST(mkdwarfs_test, unknown_option_in_filter_rule) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "-F", "+x foo"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("unknown option 'x' in filter rule"));
}

TEST_F(mkdwarfs_main_test, cannot_open_input_list_file) {
  EXPECT_NE(0, run({"--input-list", "missing.list", "-o", "-"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("cannot open input list file"));
}

TEST_F(mkdwarfs_main_test, order_invalid) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--order=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("invalid inode order mode"));
}

TEST_F(mkdwarfs_main_test, order_does_not_support_options) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--order=path:foo=42"}));
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "inode order mode 'path' does not support options"));
}

TEST_F(mkdwarfs_main_test, order_explicit_failed_to_open_file) {
  EXPECT_NE(0,
            run({"-i", "/", "-o", "-", "--order=explicit:file=explicit.txt"}));
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "failed to open explicit order file 'explicit.txt':"));
}

TEST_F(mkdwarfs_main_test, order_nilsimsa_invalid_option) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--order=nilsimsa:grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "invalid option(s) for choice nilsimsa: grmpf"));
}

TEST_F(mkdwarfs_main_test, order_nilsimsa_invalid_max_childre_value) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--order=nilsimsa:max-children=0"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("invalid max-children value: 0"));
}

TEST_F(mkdwarfs_main_test, order_nilsimsa_invalid_max_cluster_size_value_zero) {
  EXPECT_NE(0,
            run({"-i", "/", "-o", "-", "--order=nilsimsa:max-cluster-size=0"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("invalid max-cluster-size value: 0"));
}

TEST_F(mkdwarfs_main_test,
       order_nilsimsa_invalid_max_cluster_size_value_negative) {
  EXPECT_NE(
      0, run({"-i", "/", "-o", "-", "--order=nilsimsa:max-cluster-size=-1"}));
  EXPECT_THAT(err(),
              ::testing::HasSubstr("invalid max-cluster-size value: -1"));
}

TEST_F(mkdwarfs_main_test, order_nilsimsa_duplicate_option) {
  EXPECT_NE(0,
            run({"-i", "/", "-o", "-",
                 "--order=nilsimsa:max-cluster-size=1:max-cluster-size=10"}));
  EXPECT_THAT(err(),
              ::testing::HasSubstr(
                  "duplicate option max-cluster-size for choice nilsimsa"));
}

TEST_F(mkdwarfs_main_test, unknown_file_hash) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--file-hash=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("unknown file hash function"));
}

TEST_F(mkdwarfs_main_test, unknown_categorizer) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--categorize=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("unknown categorizer: grmpf"));
}

TEST_F(mkdwarfs_main_test, invalid_filter_debug_mode) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--debug-filter=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("invalid filter debug mode"));
}

TEST_F(mkdwarfs_main_test, invalid_progress_mode) {
  iol->set_terminal_is_tty(true);
  iol->set_terminal_fancy(true);
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--progress=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("invalid progress mode"));
}

TEST_F(mkdwarfs_main_test, time_resolution_zero) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--time-resolution=0"}));
  EXPECT_THAT(err(),
              ::testing::HasSubstr("'--time-resolution' must be nonzero"));
}

TEST_F(mkdwarfs_main_test, time_resolution_invalid) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--time-resolution=grmpf"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("'--time-resolution' is invalid"));
}

TEST_F(mkdwarfs_main_test, filesystem_header_error) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--header=header.txt"})) << err();
  EXPECT_THAT(err(), ::testing::HasSubstr("cannot open header file"));
}

TEST(mkdwarfs_test, input_must_be_a_directory) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/test.pl", "-o", "-"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("'/test.pl' must be a directory"));
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
TEST_F(mkdwarfs_main_test, compression_cannot_be_used_without_category) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "-C", "ricepp"}));
  EXPECT_THAT(err(), ::testing::HasSubstr("cannot be used without a category"));
}

TEST_F(mkdwarfs_main_test, compression_cannot_be_used_for_category) {
  EXPECT_NE(0, run({"-i", "/", "-o", "-", "--categorize", "-C",
                    "incompressible::ricepp"}));
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "cannot be used for category 'incompressible': "
                         "metadata requirements not met"));
}
#endif

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

TEST(mkdwarfs_test, block_number_out_of_range) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-l4"})) << t.err();
  auto fs = t.fs_from_stdout();
  EXPECT_THAT([&] { fs.read_raw_block_data(4711, 0, 1024).get(); },
              ::testing::ThrowsMessage<runtime_error>(
                  ::testing::HasSubstr("block number out of range")));
}

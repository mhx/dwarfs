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
#include <filesystem>
#include <system_error>

#include <fmt/format.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/device_number.h>
#include <dwarfs/file_type.h>
#include <dwarfs/reader/filesystem_v2.h>

#include "test_tool_main_tester.h"

using namespace dwarfs;
using namespace dwarfs::test;

TEST(mkdwarfs_input_list, format_names_in_help) {
  auto t = mkdwarfs_tester::create_empty();
  ASSERT_EQ(0, t.run({"--help"})) << t.err();
  EXPECT_THAT(t.out(), testing::HasSubstr("--input-list-format"));
  EXPECT_THAT(t.out(), testing::HasSubstr("gen_init_cpio"));
  EXPECT_THAT(t.out(), testing::HasSubstr("jsonl"));
}

TEST(mkdwarfs_input_list, gen_init_cpio_synthetic_entries_and_hardlinks) {
  for (auto const mode : input_modes) {
    SCOPED_TRACE(static_cast<int>(mode));

    for (bool const dedupe : {true, false}) {
      SCOPED_TRACE(dedupe);

      auto t = mkdwarfs_tester::create_empty();
      t.os->add_dir("/");
      t.os->add_dir("/staging");
      t.os->add_dir("/staging/data");
      t.os->add_file("/staging/data/payload", "test-content");

      auto const input = t.set_input_list(mode,
                                          R"(# gen_init_cpio format
dir /etc 0750 100 200
file /etc/config data/payload 0640 123 456 /etc/alias
slink /etc/link config 0777 111 222
dir /dev 0755 0 0
nod /dev/null 0666 0 0 c 1 3
dir /run 0755 0 0
pipe /run/fifo 0600 23 24
sock /run/sock 0600 23 24
)");

      std::vector<std::string> args{
          "-i",
          "/staging",
          "--input-list",
          input,
          "--input-list-format=gen_init_cpio",
          "--order=none",
          "-l1",
          "-o",
          "test.dwarfs",
      };

      if (!dedupe) {
        args.emplace_back("--no-dedupe");
      }

      ASSERT_EQ(0, t.run(args)) << t.err();

      auto fs = t.fs_from_file("test.dwarfs");
      auto dir = fs.find("/etc");

      ASSERT_TRUE(dir);

      auto dst = fs.getattr(dir->inode());
      EXPECT_EQ(0750, dst.mode() & 07777);
      EXPECT_EQ(100, dst.uid());
      EXPECT_EQ(200, dst.gid());

      auto primary = fs.find("/etc/config");
      auto alias = fs.find("/etc/alias");
      ASSERT_TRUE(primary);
      ASSERT_TRUE(alias);
      EXPECT_EQ(primary->inode().inode_num(), alias->inode().inode_num());

      auto stat = fs.getattr(primary->inode());
      EXPECT_EQ(0640, stat.mode() & 07777);
      EXPECT_EQ(123, stat.uid());
      EXPECT_EQ(456, stat.gid());
      EXPECT_EQ(2, stat.nlink());
      EXPECT_EQ("test-content", fs.read_string(fs.open(primary->inode())));

      auto link = fs.find("/etc/link");
      ASSERT_TRUE(link);
      EXPECT_TRUE(link->inode().is_symlink());
      EXPECT_EQ("config", fs.readlink(link->inode()));
      EXPECT_EQ(111, fs.getattr(link->inode()).uid());

      auto device = fs.find("/dev/null");
      ASSERT_TRUE(device);
      EXPECT_TRUE(device->inode().is_device());

      auto fifo = fs.find("/run/fifo");
      auto sock = fs.find("/run/sock");
      ASSERT_TRUE(fifo);
      ASSERT_TRUE(sock);
      EXPECT_EQ(posix_file_type::fifo, fs.getattr(fifo->inode()).type());
      EXPECT_EQ(posix_file_type::socket, fs.getattr(sock->inode()).type());
    }
  }
}

TEST(mkdwarfs_input_list, gen_init_cpio_device_numbers_round_trip) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");

  auto const input =
      t.set_input_list(input_mode::from_stdin, R"(# gen_init_cpio format
dir /dev 0755 0 0
nod /dev/char 0600 0 0 c 42 74565
nod /dev/block 0660 0 0 b 7 144470
)");

  ASSERT_EQ(0, t.run({"-i", "/staging", "--input-list", input,
                      "--input-list-format=gen_init_cpio", "-o",
                      "devices.dwarfs", "-l1"}))
      << t.err();

  auto fs = t.fs_from_file("devices.dwarfs");

  auto check_device = [&](char const* path, posix_file_type::value type,
                          std::uint32_t major, std::uint32_t minor) {
    SCOPED_TRACE(path);
    auto entry = fs.find(path);
    ASSERT_TRUE(entry);

    auto const stat = fs.getattr(entry->inode());
    EXPECT_EQ(type, stat.type());

    std::error_code ec;
    auto const stored = fs.get_device(entry->inode(), ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(device_number(major, minor), stored);

    ec.clear();
    device_number const decoded(stat.rdev(), ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(device_number(major, minor), decoded);
  };

  check_device("/dev/char", posix_file_type::character, 42, 74565);
  check_device("/dev/block", posix_file_type::block, 7, 144470);
}

TEST(mkdwarfs_input_list, gen_init_cpio_hardlink_groups_are_local) {
  for (bool const dedupe : {true, false}) {
    SCOPED_TRACE(dedupe);
    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/first", "first contents");
    t.os->add_file("/staging/second", "second contents");

    auto const input =
        t.set_input_list(input_mode::from_stdin, R"(# gen_init_cpio format
dir /a 0755 0 0
dir /b 0755 0 0
dir /b/c 0755 0 0
file /a/one first 0644 11 12 /b/c/one-link
file /a/two second 0600 21 22 /b/two-link
)");

    std::vector<std::string> args{
        "-i",
        "/staging",
        "--input-list",
        input,
        "--input-list-format=gen_init_cpio",
        "--order=none",
        "-l1",
        "-o",
        "test.dwarfs",
    };

    if (!dedupe) {
      args.emplace_back("--no-dedupe");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();

    auto fs = t.fs_from_file("test.dwarfs");
    auto one = fs.find("/a/one");
    auto one_link = fs.find("/b/c/one-link");
    auto two = fs.find("/a/two");
    auto two_link = fs.find("/b/two-link");

    ASSERT_TRUE(one);
    ASSERT_TRUE(one_link);
    ASSERT_TRUE(two);
    ASSERT_TRUE(two_link);
    EXPECT_EQ(one->inode().inode_num(), one_link->inode().inode_num());
    EXPECT_EQ(two->inode().inode_num(), two_link->inode().inode_num());
    EXPECT_EQ(2, fs.getattr(one->inode()).nlink());
    EXPECT_EQ(2, fs.getattr(two->inode()).nlink());
    EXPECT_EQ("first contents", fs.read_string(fs.open(one->inode())));
    EXPECT_EQ("second contents", fs.read_string(fs.open(two->inode())));
  }
}

TEST(mkdwarfs_input_list, rejected_alias_does_not_change_link_count) {
  for (bool const dedupe : {true, false}) {
    SCOPED_TRACE(dedupe);
    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/source", "contents");

    auto const input = t.set_input_list(input_mode::from_stdin,
                                        "file /a source 0644 0 0 /b /b /c\n");

    std::vector<std::string> args{
        "-i",
        "/staging",
        "--input-list",
        input,
        "--input-list-format=gen_init_cpio",
        "-l1",
        "-o",
        "test.dwarfs",
    };

    if (!dedupe) {
      args.emplace_back("--no-dedupe");
    }

    EXPECT_EQ(2, t.run(args)) << t.err();

    auto fs = t.fs_from_file("test.dwarfs");
    auto a = fs.find("/a");
    auto b = fs.find("/b");
    auto c = fs.find("/c");

    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(c);
    EXPECT_EQ(a->inode().inode_num(), b->inode().inode_num());
    EXPECT_EQ(a->inode().inode_num(), c->inode().inode_num());
    EXPECT_EQ(3, fs.getattr(a->inode()).nlink());
  }
}

TEST(mkdwarfs_input_list, gen_init_cpio_rejects_invalid_option) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  EXPECT_NE(0, t.run({"--input-list-format=unknown", "--input-list=-", "-o",
                      "test.dwarfs"}));
  EXPECT_THAT(t.err(), testing::HasSubstr("invalid --input-list-format"));
}

TEST(mkdwarfs_input_list, gen_init_cpio_rejects_late_directory) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/payload", "content");
  auto const input =
      t.set_input_list(input_mode::from_stdin, R"(# gen_init_cpio format
file /etc/config /payload 0644 0 0
dir /etc 0700 0 0
)");
  EXPECT_NE(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "-o", "test.dwarfs"}));
  EXPECT_THAT(t.err(), testing::HasSubstr("missing or non-directory parent"));
}

TEST(mkdwarfs_input_list, explicit_plain_format_preserves_paths) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/payload", "plain");
  auto const input = t.set_input_list(input_mode::from_stdin, "payload\n");
  ASSERT_EQ(0, t.run({"-i", "/", "--input-list", input,
                      "--input-list-format=plain", "-l1", "-o", "test.dwarfs"}))
      << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  auto entry = fs.find("/payload");
  ASSERT_TRUE(entry);
  EXPECT_EQ("plain", fs.read_string(fs.open(entry->inode())));
}

TEST(mkdwarfs_input_list, gen_init_cpio_explicit_type_controls_dereference) {
  for (bool const dereference : {false, true}) {
    SCOPED_TRACE(dereference);
    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/data", "payload");
    t.os->add_symlink("/staging/source-link", "data");
    auto const input = t.set_input_list(input_mode::from_stdin,
                                        "file /copied source-link 0644 17 18\n"
                                        "slink /symbolic copied 0777 19 20\n");

    std::vector<std::string> args{
        "-i",
        "/staging",
        "--input-list",
        input,
        "--input-list-format=gen_init_cpio",
        "--order=none",
        "-l1",
        "-o",
        "test.dwarfs",
    };

    if (dereference) {
      args.emplace_back("--dereference");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();
    auto fs = t.fs_from_file("test.dwarfs");
    auto file = fs.find("/copied");
    auto link = fs.find("/symbolic");
    ASSERT_TRUE(file);
    ASSERT_TRUE(link);
    EXPECT_TRUE(file->inode().is_regular_file());
    EXPECT_EQ("payload", fs.read_string(fs.open(file->inode())));
    EXPECT_TRUE(link->inode().is_symlink());
    EXPECT_EQ("copied", fs.readlink(link->inode()));
  }
}

TEST(mkdwarfs_input_list, gen_init_cpio_unset_variable_counts_as_error) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");
  t.os->add_dir("/staging/data");
  t.os->add_file("/staging/data/payload", "contents");
  auto const input = t.set_input_list(
      input_mode::from_stdin,
      "file /copied ${DWARFS_TEST_UNSET_CPIO_PREFIX}data/payload 0644 0 0\n");

  // Substitution succeeds and the scanner can read the resulting file,
  // but the reader's reported error still makes the exit status nonzero.
  EXPECT_EQ(2, t.run({"-i", "/staging", "--input-list", input,
                      "--input-list-format=gen_init_cpio", "-l1", "-o",
                      "test.dwarfs"}))
      << t.err();
  EXPECT_THAT(t.err(),
              testing::HasSubstr("environment variable "
                                 "'DWARFS_TEST_UNSET_CPIO_PREFIX' is not set"));

  auto fs = t.fs_from_file("test.dwarfs");
  auto file = fs.find("/copied");
  ASSERT_TRUE(file);
  EXPECT_EQ("contents", fs.read_string(fs.open(file->inode())));
}

TEST(mkdwarfs_input_list, gen_init_cpio_missing_parent_is_error) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/payload", "data");
  auto const input = t.set_input_list(input_mode::from_stdin,
                                      "file /etc/config /payload 0644 0 0\n");
  EXPECT_NE(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "-o", "test.dwarfs"}));
  EXPECT_THAT(t.err(), testing::HasSubstr("missing or non-directory parent"));
}

TEST(mkdwarfs_input_list, duplicate_definitions_keep_first_and_continue) {
  for (auto const mode : input_modes) {
    SCOPED_TRACE(static_cast<int>(mode));

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/first", "first contents");
    t.os->add_file("/staging/second", "second contents");

    auto const input = t.set_input_list(mode, R"(# gen_init_cpio format
dir /etc 0750 11 12
file /etc/config first 0644 21 22
dir /etc 0777 99 99
file /etc/config second 0600 31 32
file /etc/after second 0640 41 42
)");

    EXPECT_EQ(2, t.run({"-i", "/staging", "--input-list", input,
                        "--input-list-format=gen_init_cpio", "-l1", "-o",
                        "duplicate.dwarfs"}))
        << t.err();
    EXPECT_THAT(t.err(), testing::HasSubstr(
                             "skipping duplicate entry 'etc' in input list"));
    EXPECT_THAT(t.err(),
                testing::HasSubstr(fmt::format(
                    "skipping duplicate entry '{}' in input list",
                    (std::filesystem::path("etc") / "config").string())));

    auto fs = t.fs_from_file("duplicate.dwarfs");
    auto dir = fs.find("/etc");
    auto config = fs.find("/etc/config");
    auto after = fs.find("/etc/after");

    ASSERT_TRUE(dir);
    ASSERT_TRUE(config);
    ASSERT_TRUE(after);
    EXPECT_EQ(0750, fs.getattr(dir->inode()).mode() & 07777);
    EXPECT_EQ(11, fs.getattr(dir->inode()).uid());
    EXPECT_EQ(0644, fs.getattr(config->inode()).mode() & 07777);
    EXPECT_EQ("first contents", fs.read_string(fs.open(config->inode())));
    EXPECT_EQ("second contents", fs.read_string(fs.open(after->inode())));
  }
}

TEST(mkdwarfs_input_list, skipped_file_does_not_create_aliases) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");
  t.os->add_file("/staging/after", "valid contents");

  auto const input = t.set_input_list(input_mode::from_stdin,
                                      "file /missing absent 0644 0 0 /alias\n"
                                      "file /after after 0644 0 0\n");
  EXPECT_EQ(2, t.run({"-i", "/staging", "--input-list", input,
                      "--input-list-format=gen_init_cpio", "-l1", "-o",
                      "test.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  EXPECT_FALSE(fs.find("/missing"));
  EXPECT_FALSE(fs.find("/alias"));
  auto after = fs.find("/after");
  ASSERT_TRUE(after);
  EXPECT_EQ("valid contents", fs.read_string(fs.open(after->inode())));
}

TEST(mkdwarfs_input_list, rejected_duplicate_does_not_create_aliases) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");
  t.os->add_file("/staging/first", "first contents");
  t.os->add_file("/staging/second", "second contents");

  auto const input = t.set_input_list(input_mode::from_stdin,
                                      "file /a first 0644 0 0\n"
                                      "file /a second 0644 0 0 /wrong-alias\n"
                                      "file /after second 0644 0 0\n");
  EXPECT_EQ(2, t.run({"-i", "/staging", "--input-list", input,
                      "--input-list-format=gen_init_cpio", "-l1", "-o",
                      "test.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  EXPECT_FALSE(fs.find("/wrong-alias"));
  auto first = fs.find("/a");
  auto after = fs.find("/after");
  ASSERT_TRUE(first);
  ASSERT_TRUE(after);
  EXPECT_EQ("first contents", fs.read_string(fs.open(first->inode())));
  EXPECT_EQ("second contents", fs.read_string(fs.open(after->inode())));
}

TEST(mkdwarfs_input_list, plain_explicit_directory_is_not_implicit_duplicate) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");
  t.os->add_dir("/staging/subdir");
  t.os->add_file("/staging/subdir/file", "contents");

  auto const input =
      t.set_input_list(input_mode::from_stdin, "subdir\nsubdir/file\n");

  ASSERT_EQ(0, t.run({"-i", "/staging", "--input-list", input, "-l1", "-o",
                      "dir-list.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("dir-list.dwarfs");

  EXPECT_TRUE(fs.find("/subdir"));
  EXPECT_TRUE(fs.find("/subdir/file"));
}

TEST(mkdwarfs_input_list, gen_init_cpio_explicit_root_and_overrides) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging");
  auto const input =
      t.set_input_list(input_mode::from_stdin, "dir / 0700 17 18\n"
                                               "dir /etc 0755 0 0\n");

  ASSERT_EQ(0,
            t.run({"-i", "/staging", "--input-list", input,
                   "--input-list-format=gen_init_cpio", "--synthetic-time=4567",
                   "--root-group=999", "-l1", "-o", "root.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("root.dwarfs");
  auto root = fs.find("/");
  auto etc = fs.find("/etc");

  ASSERT_TRUE(root);
  ASSERT_TRUE(etc);

  auto const root_st = fs.getattr(root->inode());

  EXPECT_EQ(0700, root_st.mode() & 07777);
  EXPECT_EQ(17, root_st.uid());
  EXPECT_EQ(999, root_st.gid());
  EXPECT_EQ(4567, root_st.mtime());
  EXPECT_EQ(4567, fs.getattr(etc->inode()).mtime());
}

TEST(mkdwarfs_input_list, gen_init_cpio_implicit_root_uses_synthetic_time) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  auto const input =
      t.set_input_list(input_mode::from_stdin, "dir /etc 0750 11 12\n");

  ASSERT_EQ(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "--synthetic-time=1234", "--root-time=5678", "-l1", "-o",
                   "root.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("root.dwarfs");
  auto root = fs.find("/");
  auto etc = fs.find("/etc");

  ASSERT_TRUE(root);
  ASSERT_TRUE(etc);
  EXPECT_EQ(5678, fs.getattr(root->inode()).mtime());
  EXPECT_EQ(1234, fs.getattr(etc->inode()).mtime());
}

TEST(mkdwarfs_input_list, plain_root_retains_physical_metadata) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/staging", {.mtim = file_stat::timespec_type{7373, 0}});
  t.os->add_file("/staging/file", "contents");
  auto const input = t.set_input_list(input_mode::from_stdin, "file\n");
  ASSERT_EQ(0, t.run({"-i", "/staging", "--input-list", input,
                      "--synthetic-time=3333", "-l1", "-o", "plain.dwarfs"}))
      << t.err();
  auto fs = t.fs_from_file("plain.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  EXPECT_EQ(7373, fs.getattr(root->inode()).mtime());
}

TEST(mkdwarfs_input_list, file_manifest_mtime_defaults_synthetic_time) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  auto const input =
      t.set_input_list(input_mode::from_file, "dir /etc 0755 0 0\n");
  t.os->add_file(input, "dir /etc 0755 0 0\n",
                 {.mtim = file_stat::timespec_type{98765, 0}});

  ASSERT_EQ(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "-l1", "-o", "file-list.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("file-list.dwarfs");
  auto root = fs.find("/");
  auto etc = fs.find("/etc");

  ASSERT_TRUE(root);
  ASSERT_TRUE(etc);
  EXPECT_EQ(98765, fs.getattr(root->inode()).mtime());
  EXPECT_EQ(98765, fs.getattr(etc->inode()).mtime());
}

TEST(mkdwarfs_input_list, reproducible_stdin_synthetic_timestamp_is_zero) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  auto const input =
      t.set_input_list(input_mode::from_stdin, "dir /etc 0755 0 0\n");

  ASSERT_EQ(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "--reproducible", "-l1", "-o", "stdin.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("stdin.dwarfs");
  auto root = fs.find("/");
  auto etc = fs.find("/etc");

  ASSERT_TRUE(root);
  ASSERT_TRUE(etc);
  EXPECT_EQ(0, fs.getattr(root->inode()).mtime());
  EXPECT_EQ(0, fs.getattr(etc->inode()).mtime());
}

TEST(mkdwarfs_input_list, plain_first_root_record_is_accepted) {
  for (auto const& first : {"/staging", "."}) {
    SCOPED_TRACE(first);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging", {.mtim = file_stat::timespec_type{7373, 0}});
    t.os->add_dir("/staging/sub");
    t.os->add_file("/staging/sub/file", "data");

    auto const input = t.set_input_list(
        input_mode::from_stdin,
        std::string(first) + "\n/staging/sub\n/staging/sub/file\n");

    ASSERT_EQ(0, t.run({"-i", "/staging", "--input-list", input, "-l1", "-o",
                        "plain-root.dwarfs"}))
        << t.err();

    auto fs = t.fs_from_file("plain-root.dwarfs");
    auto root = fs.find("/");

    ASSERT_TRUE(root);
    EXPECT_EQ(7373, fs.getattr(root->inode()).mtime());
    EXPECT_TRUE(fs.find("/sub/file"));
  }
}

TEST(mkdwarfs_input_list, plain_later_root_record_is_error) {
  for (auto const& contents : {
           "file\n.\n",
           "/staging\nfile\n/staging\n",
       }) {
    SCOPED_TRACE(contents);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/file", "data");
    auto const input = t.set_input_list(input_mode::from_stdin, contents);

    EXPECT_EQ(2, t.run({"-i", "/staging", "--input-list", input, "-l1", "-o",
                        "late-plain-root.dwarfs"}))
        << t.err();

    EXPECT_THAT(t.err(),
                testing::HasSubstr("root directory entry must precede"));

    auto fs = t.fs_from_file("late-plain-root.dwarfs");

    EXPECT_TRUE(fs.find("/file"));
  }
}

TEST(mkdwarfs_input_list, later_root_definition_is_error) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  auto const input = t.set_input_list(input_mode::from_stdin,
                                      "dir /etc 0755 0 0\ndir / 0700 10 11\n");
  EXPECT_EQ(2,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "-l1", "-o", "late-root.dwarfs"}))
      << t.err();
  EXPECT_THAT(t.err(), testing::HasSubstr("root directory entry must precede"));
  auto fs = t.fs_from_file("late-root.dwarfs");
  EXPECT_TRUE(fs.find("/etc"));
}

TEST(mkdwarfs_input_list, synthetic_time_requires_list) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  EXPECT_NE(0,
            t.run({"--synthetic-time=100", "-i", "/", "-o", "no-list.dwarfs"}));
  EXPECT_THAT(t.err(),
              testing::HasSubstr("--synthetic-time requires --input-list"));
}

TEST(mkdwarfs_input_list, root_like_overrides_manifest_root) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/template", {.mtim = file_stat::timespec_type{9123, 0}});
  auto const input =
      t.set_input_list(input_mode::from_stdin, "dir / 0700 17 18\n");

  ASSERT_EQ(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "--root-like=/template", "--root-owner=1234",
                   "--synthetic-time=4567", "-l1", "-o", "root.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("root.dwarfs");
  auto root = fs.find("/");

  ASSERT_TRUE(root);

  auto const st = fs.getattr(root->inode());

  EXPECT_EQ(0755, st.mode() & 07777); // Root-like wins over dir /.
  EXPECT_EQ(1234, st.uid());          // Individual override wins over both.
  EXPECT_EQ(9123, st.mtime());
}

TEST(mkdwarfs_input_list, source_date_epoch_supplies_synthetic_time) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->setenv("SOURCE_DATE_EPOCH", "4321");
  auto const input =
      t.set_input_list(input_mode::from_stdin, "dir /etc 0755 0 0\n");

  ASSERT_EQ(0,
            t.run({"--input-list", input, "--input-list-format=gen_init_cpio",
                   "-l1", "-o", "epoch.dwarfs"}))
      << t.err();

  auto fs = t.fs_from_file("epoch.dwarfs");
  auto root = fs.find("/");
  auto etc = fs.find("/etc");

  ASSERT_TRUE(root);
  ASSERT_TRUE(etc);
  EXPECT_EQ(4321, fs.getattr(root->inode()).mtime());
  EXPECT_EQ(4321, fs.getattr(etc->inode()).mtime());
}

TEST(mkdwarfs_input_list, jsonl_entries_metadata_and_hardlinks) {
  for (bool const dedupe : {true, false}) {
    SCOPED_TRACE(dedupe);
    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/staging");
    t.os->add_file("/staging/file with spaces", "file contents");

    auto const input = t.set_input_list(input_mode::from_stdin, R"(
{"type":"dir","path":"/","uid":11,"mtime":100}
{"type":"dir","path":"dir with spaces","mode":"0700","uid":12}
{"type":"file","path":"dir with spaces/a file","source":"file with spaces","mode":"0640","gid":13,"mtime":[101,123456789],"aliases":["dir with spaces/alias"]}
{"type":"hardlink","path":"dir with spaces/chain","target":"dir with spaces/alias"}
{"type":"symlink","path":"a link","target":"dir with spaces/a file"}
{"type":"dir","path":"dev"}
{"type":"char","path":"dev/null","major":1,"minor":3}
{"type":"block","path":"dev/loop0","major":7,"minor":0}
{"type":"fifo","path":"a fifo"}
{"type":"socket","path":"a socket"}
)");

    std::vector<std::string> args{"-i",
                                  "/staging",
                                  "--input-list",
                                  input,
                                  "--input-list-format=jsonl",
                                  "--keep-all-times",
                                  "--time-resolution=ns",
                                  "-l1",
                                  "-o",
                                  "test.dwarfs"};
    if (!dedupe) {
      args.emplace_back("--no-dedupe");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();
    auto fs = t.fs_from_file("test.dwarfs");

    auto primary = fs.find("/dir with spaces/a file");
    auto alias = fs.find("/dir with spaces/alias");
    auto chain = fs.find("/dir with spaces/chain");
    ASSERT_TRUE(primary);
    ASSERT_TRUE(alias);
    ASSERT_TRUE(chain);
    EXPECT_EQ(primary->inode().inode_num(), alias->inode().inode_num());
    EXPECT_EQ(primary->inode().inode_num(), chain->inode().inode_num());
    EXPECT_EQ(3, fs.getattr(primary->inode()).nlink());
    EXPECT_EQ("file contents", fs.read_string(fs.open(primary->inode())));
    auto const st = fs.getattr(primary->inode());
    EXPECT_EQ(0640, st.mode() & 07777);
    EXPECT_EQ(13, st.gid());
    EXPECT_EQ((file_stat::timespec_type{101, 123456789}), st.mtimespec());

    auto dir = fs.find("/dir with spaces");
    ASSERT_TRUE(dir);
    EXPECT_EQ(0700, fs.getattr(dir->inode()).mode() & 07777);
    EXPECT_EQ(12, fs.getattr(dir->inode()).uid());

    auto link = fs.find("/a link");
    ASSERT_TRUE(link);
    EXPECT_EQ("dir with spaces/a file", fs.readlink(link->inode()));

    auto device = fs.find("/dev/null");
    ASSERT_TRUE(device);
    EXPECT_EQ(posix_file_type::character, fs.getattr(device->inode()).type());
    auto block = fs.find("/dev/loop0");
    ASSERT_TRUE(block);
    EXPECT_EQ(posix_file_type::block, fs.getattr(block->inode()).type());
    auto fifo = fs.find("/a fifo");
    auto socket = fs.find("/a socket");
    ASSERT_TRUE(fifo);
    ASSERT_TRUE(socket);
    EXPECT_EQ(posix_file_type::fifo, fs.getattr(fifo->inode()).type());
    EXPECT_EQ(posix_file_type::socket, fs.getattr(socket->inode()).type());
    auto root = fs.find("/");
    ASSERT_TRUE(root);
    EXPECT_EQ(11, fs.getattr(root->inode()).uid());
    EXPECT_EQ((file_stat::timespec_type{100, 0}),
              fs.getattr(root->inode()).mtimespec());
  }
}

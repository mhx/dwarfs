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

#include <sstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/file_type.h>
#include <dwarfs/writer/input_entry_reader.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;
namespace fs = std::filesystem;

TEST(input_entry_reader, manifest_sources_use_native_separators) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
#ifdef _WIN32
  fs::path const root{LR"(\\?\C:\staging)"};
#else
  fs::path const root{"/staging"};
#endif

  for (auto const format : {"gen_init_cpio", "jsonl"}) {
    SCOPED_TRACE(format);
    std::istringstream list{
        std::string(format) == "gen_init_cpio"
            ? "file /sub/beta sub/beta 0644 0 0\n"
            : R"({"type":"file","path":"sub/beta","source":"sub/beta"})"
              "\n"};
    auto reader = writer::input_entry_reader_factory::create(format, lgr, *os,
                                                             &list, root);
    auto entry = reader->next();
    ASSERT_TRUE(entry);
    ASSERT_TRUE(entry->spec);
    auto const* file =
        std::get_if<writer::input_entry_spec::file>(&entry->spec->content);
    ASSERT_TRUE(file);
    EXPECT_EQ((root / "sub" / "beta").native(), file->source.native());
  }
}

TEST(input_entry_reader, gen_init_cpio_streaming_and_hardlinks) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list{
      "# header\n"
      "  dir /etc 0755 0 0\n"
      "file /etc/config relative/config 0640 123 456 /etc/config2 "
      "/etc/config3\n"
      "slink /etc/target ../config 0777 4 5\n"
      "nod /dev/null 0666 0 0 c 1 3\n"
      "nod /dev/loop 0600 0 0 b 7 0\n"
      "pipe /run/fifo 0600 7 8\n"
      "sock /run/sock 0600 7 8\n"};

  auto reader = writer::input_entry_reader_factory::create("gen_init_cpio", lgr,
                                                           *os, &list, "/root");

  EXPECT_EQ(std::streampos(0), list.tellg());

  using spec = writer::input_entry_spec;
  auto dir = reader->next();
  ASSERT_TRUE(dir);
  ASSERT_TRUE(dir->spec);
  EXPECT_EQ(fs::path("etc"), dir->path);
  EXPECT_TRUE(std::holds_alternative<spec::directory>(dir->spec->content));
  EXPECT_EQ(0755, dir->spec->attributes.permissions);

  auto primary = reader->next();
  ASSERT_TRUE(primary);
  ASSERT_TRUE(primary->spec);
  EXPECT_EQ(fs::path("etc/config"), primary->path);
  auto const* file = std::get_if<spec::file>(&primary->spec->content);
  ASSERT_TRUE(file);
  EXPECT_EQ(fs::path("/root/relative/config"), file->source);
  EXPECT_EQ((std::vector<fs::path>{"etc/config2", "etc/config3"}),
            file->aliases);
  EXPECT_EQ(0640, primary->spec->attributes.permissions);
  EXPECT_EQ(123, primary->spec->attributes.uid);
  EXPECT_EQ(456, primary->spec->attributes.gid);
  EXPECT_FALSE(primary->spec->attributes.atime);
  EXPECT_FALSE(primary->spec->attributes.mtime);
  EXPECT_FALSE(primary->spec->attributes.ctime);

  auto symlink = reader->next();
  ASSERT_TRUE(symlink);
  ASSERT_TRUE(symlink->spec);
  auto const* link = std::get_if<spec::symlink>(&symlink->spec->content);
  ASSERT_TRUE(link);
  EXPECT_EQ("../config", link->target);

  auto chr = reader->next();
  ASSERT_TRUE(chr);
  ASSERT_TRUE(chr->spec);
  auto const* character = std::get_if<spec::device>(&chr->spec->content);
  ASSERT_TRUE(character);
  EXPECT_EQ(posix_file_type::character, character->type);
  EXPECT_EQ(1, character->major);
  EXPECT_EQ(3, character->minor);

  auto blk = reader->next();
  ASSERT_TRUE(blk);
  ASSERT_TRUE(blk->spec);
  auto const* block = std::get_if<spec::device>(&blk->spec->content);
  ASSERT_TRUE(block);
  EXPECT_EQ(posix_file_type::block, block->type);
  EXPECT_EQ(7, block->major);
  EXPECT_EQ(0, block->minor);

  auto pipe = reader->next();
  ASSERT_TRUE(pipe);
  ASSERT_TRUE(pipe->spec);
  EXPECT_EQ(posix_file_type::fifo,
            std::get<spec::special>(pipe->spec->content).type);

  auto sock = reader->next();
  ASSERT_TRUE(sock);
  ASSERT_TRUE(sock->spec);
  EXPECT_EQ(posix_file_type::socket,
            std::get<spec::special>(sock->spec->content).type);
  EXPECT_FALSE(reader->next());
  EXPECT_FALSE(reader->next());
}

TEST(input_entry_reader, gen_init_cpio_rejects_invalid_records) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  for (auto const& record : {
           "dir /dev 0899 0 0\n",
           "nod /dev/null 0666 0 0 x 1 3\n",
           "pipe /run/pipe 0600 -1 0\n",
           "file /etc/x contents 0644 0\n",
           "dir /../escape 0755 0 0\n",
           "unknown /etc/foo 0644 0 0\n",
           "file /broken ${UNTERMINATED 0644 0 0\n",
           "file /broken ${} 0644 0 0\n",
       }) {
    SCOPED_TRACE(record);
    std::istringstream list(record);
    auto reader = writer::input_entry_reader_factory::create(
        "gen_init_cpio", lgr, *os, &list, "/root");
    EXPECT_ANY_THROW(reader->next());
  }
}

TEST(input_entry_reader, gen_init_cpio_reports_unset_environment_variables) {
  auto os = std::make_shared<test::os_access_mock>();
  os->setenv("DWARFS_TEST_CPIO_SET", "prefix");
  os->setenv("DWARFS_TEST_CPIO_EMPTY", "");
  test::test_logger lgr;
  std::istringstream list{
      "file /one ${DWARFS_TEST_UNSET_CPIO_SOURCE}first 0644 0 0\n"
      "file /two pre${DWARFS_TEST_UNSET_CPIO_SOURCE}mid"
      "${DWARFS_TEST_UNSET_CPIO_OTHER}post 0644 0 0\n"
      "file /three ${DWARFS_TEST_CPIO_SET}/"
      "${DWARFS_TEST_CPIO_EMPTY}tail 0644 0 0\n"
      "dir /after 0755 0 0\n"};
  auto reader = writer::input_entry_reader_factory::create("gen_init_cpio", lgr,
                                                           *os, &list, "/root");

  EXPECT_EQ(0, reader->error_count());
  auto one = reader->next();
  ASSERT_TRUE(one);
  EXPECT_EQ(fs::path("one"), one->path);
  ASSERT_TRUE(one->spec);
  EXPECT_EQ(
      fs::path("/root/first"),
      std::get<writer::input_entry_spec::file>(one->spec->content).source);
  EXPECT_EQ(1, reader->error_count());

  auto two = reader->next();
  ASSERT_TRUE(two);
  EXPECT_EQ(fs::path("two"), two->path);
  ASSERT_TRUE(two->spec);
  EXPECT_EQ(
      fs::path("/root/premidpost"),
      std::get<writer::input_entry_spec::file>(two->spec->content).source);
  EXPECT_EQ(3, reader->error_count());

  auto three = reader->next();
  ASSERT_TRUE(three);
  EXPECT_EQ(fs::path("three"), three->path);
  ASSERT_TRUE(three->spec);
  EXPECT_EQ(
      fs::path("/root/prefix/tail"),
      std::get<writer::input_entry_spec::file>(three->spec->content).source);
  EXPECT_EQ(3, reader->error_count());

  auto after = reader->next();
  ASSERT_TRUE(after);
  EXPECT_EQ(fs::path("after"), after->path);
  EXPECT_FALSE(reader->next());
  EXPECT_EQ(3, reader->error_count());

  auto const& log = lgr.get_log();
  ASSERT_EQ(3, log.size());
  for (auto const& record : log) {
    EXPECT_EQ(logger::ERROR, record.level);
    EXPECT_THAT(record.output,
                testing::HasSubstr("environment variable 'DWARFS_TEST_UNSET_"));
  }
  EXPECT_THAT(log[0].output, testing::HasSubstr("gen_init_cpio line 1:"));
  EXPECT_THAT(log[1].output, testing::HasSubstr("gen_init_cpio line 2:"));
  EXPECT_THAT(log[2].output, testing::HasSubstr("gen_init_cpio line 2:"));
}

TEST(input_entry_reader, gen_init_cpio_owns_stream_until_destruction) {
  class owned_manifest final : public input_stream {
   public:
    explicit owned_manifest(int& destroyed)
        : stream_("dir /tmp 0755 0 0\n")
        , destroyed_(destroyed) {}
    ~owned_manifest() override { ++destroyed_; }
    std::istream& is() override { return stream_; }
    void close() override {}
    void close(std::error_code& ec) override { ec.clear(); }

   private:
    std::istringstream stream_;
    int& destroyed_;
  };

  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  int destroyed = 0;
  std::unique_ptr<input_stream> source =
      std::make_unique<owned_manifest>(destroyed);
  auto reader = writer::input_entry_reader_factory::create(
      "gen_init_cpio", lgr, *os, std::move(source), "/root");
  EXPECT_EQ(0, destroyed);
  auto entry = reader->next();
  ASSERT_TRUE(entry);
  EXPECT_EQ(fs::path("tmp"), entry->path);
  EXPECT_FALSE(reader->next());
  EXPECT_EQ(0, destroyed);
  reader.reset();
  EXPECT_EQ(1, destroyed);
}

TEST(input_entry_reader, plain_invalid_path_reports_line_number) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list{"../outside\nvalid\n"};
  auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                           &list, "/root");
  auto valid = reader->next();
  ASSERT_TRUE(valid);
  EXPECT_EQ(fs::path("valid"), valid->path);
  EXPECT_EQ(1, reader->error_count());
  ASSERT_FALSE(lgr.get_log().empty());
  EXPECT_THAT(lgr.get_log().front().output,
              testing::HasSubstr("input list line 1:"));
}

TEST(input_entry_reader, plain_does_not_reemit_explicit_directory_as_parent) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  os->add_dir("/");
  os->add_dir("/root");
  os->add_dir("/root/etc");
  os->add_file("/root/etc/config", "contents");
  std::istringstream list{"etc\netc/config\n"};

  auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                           &list, "/root");

  auto root = reader->implicit_root();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);
  EXPECT_EQ(fs::path("/root"), root->source);

  auto dir = reader->next();
  ASSERT_TRUE(dir);
  EXPECT_EQ(fs::path("etc"), dir->path);

  auto file = reader->next();
  ASSERT_TRUE(file);
  EXPECT_EQ(fs::path("etc/config"), file->path);
  EXPECT_FALSE(reader->next());
  EXPECT_EQ(0, reader->error_count());
}

TEST(input_entry_reader, plain_emits_logical_parents_and_counts_errors) {
  auto os = std::make_shared<test::os_access_mock>();
  os->add_dir("/");
  os->add_dir("/staging");
  test::test_logger lgr;
  std::istringstream list{"dir/sub/file\n/outside/file\ndir/other\n"};

  auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                           &list, "/staging");

  EXPECT_EQ(0, list.tellg());
  auto root = reader->implicit_root();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);
  EXPECT_EQ(fs::path("/staging"), root->source);
  EXPECT_EQ(0, list.tellg());

  for (auto const& [logical, physical] :
       std::vector<std::pair<fs::path, fs::path>>{
           {"dir", "/staging/dir"},
           {"dir/sub", "/staging/dir/sub"},
           {"dir/sub/file", "/staging/dir/sub/file"},
           {"dir/other", "/staging/dir/other"},
       }) {
    SCOPED_TRACE(logical.string());

    auto entry = reader->next();

    ASSERT_TRUE(entry);
    EXPECT_EQ(logical, entry->path);
    ASSERT_TRUE(entry->source);
    EXPECT_EQ(physical, *entry->source);
    EXPECT_FALSE(entry->spec);
  }

  EXPECT_FALSE(reader->next());
  EXPECT_EQ(1, reader->error_count());
}

TEST(input_entry_reader, plain_missing_sources_do_not_abort_reading) {
  test::test_logger lgr;
  auto os = std::make_shared<test::os_access_mock>();
  os->add_dir("/");
  os->add_dir("/staging");
  os->add_symlink("/staging/missing-dir", "/absent");
  os->add_file("/staging/valid", "contents");

  std::istringstream list{"missing-dir/child\nmissing-file\nvalid\n"};

  auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                           &list, "/staging");

  auto root = reader->implicit_root();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);

  for (auto const& name :
       {"missing-dir", "missing-dir/child", "missing-file", "valid"}) {
    SCOPED_TRACE(name);
    auto entry = reader->next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(fs::path(name), entry->path);
  }
  EXPECT_FALSE(reader->next());
  EXPECT_EQ(0, reader->error_count());
}

TEST(input_entry_reader, factory_rejects_unknown_format) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list;
  EXPECT_ANY_THROW(writer::input_entry_reader_factory::create(
      "unsupported", lgr, *os, &list, "/"));
}

TEST(input_entry_reader, plain_absolute_entries_require_absolute_root) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list{"/file\n"};
  auto reader = writer::input_entry_reader_factory::create(
      "plain", lgr, *os, &list, "relative-root");

  auto root = reader->implicit_root();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);
  EXPECT_THAT([&] { (void)reader->next(); },
              testing::ThrowsMessage<dwarfs::runtime_error>(testing::HasSubstr(
                  "absolute paths in input list require absolute input path")));
}

TEST(input_entry_reader, plain_empty_list_has_source_backed_implicit_root) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list;
  auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                           &list, "/staging");
  EXPECT_EQ(0, list.tellg());
  auto root = reader->implicit_root();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);
  EXPECT_EQ(fs::path("/staging"), root->source);
  EXPECT_FALSE(root->spec);
  EXPECT_EQ(0, list.tellg());
  EXPECT_FALSE(reader->next());
}

TEST(input_entry_reader, plain_returns_listed_root_in_input_order) {
  auto os = std::make_shared<test::os_access_mock>();
  os->add_dir("/");
  os->add_dir("/staging");
  os->add_dir("/staging/sub");
  os->add_file("/staging/sub/file", "data");
  test::test_logger lgr;

  for (auto const& first : {"/staging", "."}) {
    SCOPED_TRACE(first);
    std::istringstream list{std::string(first) + "\nsub/file\n/staging\n"};
    auto reader = writer::input_entry_reader_factory::create("plain", lgr, *os,
                                                             &list, "/staging");
    auto fallback = reader->implicit_root();
    ASSERT_TRUE(fallback);
    EXPECT_EQ(fs::path("."), fallback->path);
    EXPECT_EQ(fs::path("/staging"), fallback->source);
    EXPECT_EQ(std::streampos(0), list.tellg());

    for (auto const& expected : {".", "sub", "sub/file", "."}) {
      auto entry = reader->next();
      ASSERT_TRUE(entry);
      EXPECT_EQ(fs::path(expected), entry->path);
    }
    EXPECT_FALSE(reader->next());
    EXPECT_EQ(0, reader->error_count());
  }
}

TEST(input_entry_reader, gen_init_cpio_explicit_root_is_regular_entry) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list{"dir / 0700 17 18\ndir /etc 0755 0 0\n"};
  auto reader = writer::input_entry_reader_factory::create(
      "gen_init_cpio", lgr, *os, &list, "/staging");
  EXPECT_FALSE(reader->implicit_root());
  auto root = reader->next();
  ASSERT_TRUE(root);
  EXPECT_EQ(fs::path("."), root->path);
  EXPECT_FALSE(root->source);
  ASSERT_TRUE(root->spec);
  EXPECT_TRUE(std::holds_alternative<writer::input_entry_spec::directory>(
      root->spec->content));
  EXPECT_EQ(0700, root->spec->attributes.permissions);
  EXPECT_EQ(17, root->spec->attributes.uid);
  EXPECT_EQ(18, root->spec->attributes.gid);
  auto etc = reader->next();
  ASSERT_TRUE(etc);
  EXPECT_EQ(fs::path("etc"), etc->path);
  EXPECT_FALSE(reader->next());
}

TEST(input_entry_reader, jsonl_streaming_and_metadata) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  std::istringstream list{R"(
{"type":"dir","path":"/etc/my app","mode":"0750","uid":7}
{"type":"file","path":"etc/my app/file name","source":"physical file","aliases":["etc/my app/copy 1"],"mtime":-0.25,"atime":[-2,123456789],"ctime":"1970-01-02T00:00:00"}
{"type":"hardlink","path":"etc/my app/copy 2","target":"etc/my app/copy 1"}
{"type":"symlink","path":"link name","target":"file name\nnext"}
{"type":"char","path":"dev/null","major":1,"minor":3}
{"type":"block","path":"dev/loop0","major":7,"minor":0}
{"type":"fifo","path":"a fifo"}
{"type":"socket","path":"a socket"}
{"type":"dir","path":"root-time","mtime":1762941600}
)"};
  auto reader = writer::input_entry_reader_factory::create("jsonl", lgr, *os,
                                                           &list, "/staging");
  EXPECT_EQ(std::streampos(0), list.tellg());
  EXPECT_FALSE(reader->implicit_root());

  using spec = writer::input_entry_spec;
  auto dir = reader->next();
  ASSERT_TRUE(dir);
  ASSERT_TRUE(dir->spec);
  EXPECT_EQ(fs::path("etc/my app"), dir->path);
  EXPECT_EQ(0750, dir->spec->attributes.permissions);
  EXPECT_EQ(7, dir->spec->attributes.uid);
  EXPECT_FALSE(dir->spec->attributes.gid);

  auto file = reader->next();
  ASSERT_TRUE(file);
  ASSERT_TRUE(file->spec);
  auto const* content = std::get_if<spec::file>(&file->spec->content);
  ASSERT_TRUE(content);
  EXPECT_EQ(fs::path("/staging/physical file"), content->source);
  EXPECT_EQ((std::vector<fs::path>{"etc/my app/copy 1"}), content->aliases);
  EXPECT_EQ((file_stat::timespec_type{-1, 750000000}),
            file->spec->attributes.mtime);
  EXPECT_EQ((file_stat::timespec_type{-2, 123456789}),
            file->spec->attributes.atime);
  EXPECT_EQ((file_stat::timespec_type{86400, 0}), file->spec->attributes.ctime);

  auto hardlink = reader->next();
  ASSERT_TRUE(hardlink);
  ASSERT_TRUE(hardlink->spec);
  EXPECT_EQ(fs::path("etc/my app/copy 1"),
            std::get<spec::hardlink>(hardlink->spec->content).target);

  auto symlink = reader->next();
  ASSERT_TRUE(symlink);
  ASSERT_TRUE(symlink->spec);
  EXPECT_EQ("file name\nnext",
            std::get<spec::symlink>(symlink->spec->content).target);

  auto device = reader->next();
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->spec);
  auto const* dev = std::get_if<spec::device>(&device->spec->content);
  ASSERT_TRUE(dev);
  EXPECT_EQ(posix_file_type::character, dev->type);
  EXPECT_EQ(1, dev->major);
  EXPECT_EQ(3, dev->minor);

  auto block = reader->next();
  ASSERT_TRUE(block);
  ASSERT_TRUE(block->spec);
  auto const* block_dev = std::get_if<spec::device>(&block->spec->content);
  ASSERT_TRUE(block_dev);
  EXPECT_EQ(posix_file_type::block, block_dev->type);
  EXPECT_EQ(7, block_dev->major);
  EXPECT_EQ(0, block_dev->minor);

  auto fifo = reader->next();
  ASSERT_TRUE(fifo);
  EXPECT_EQ(posix_file_type::fifo,
            std::get<spec::special>(fifo->spec->content).type);
  auto socket = reader->next();
  ASSERT_TRUE(socket);
  EXPECT_EQ(posix_file_type::socket,
            std::get<spec::special>(socket->spec->content).type);
  auto final = reader->next();
  ASSERT_TRUE(final);
  EXPECT_EQ((file_stat::timespec_type{1762941600, 0}),
            final->spec->attributes.mtime);
  EXPECT_FALSE(reader->next());
  EXPECT_EQ(0, reader->error_count());
}

TEST(input_entry_reader, jsonl_rejects_invalid_records) {
  auto os = std::make_shared<test::os_access_mock>();
  test::test_logger lgr;
  for (
      auto const& record : {
          R"({"type":"file","path":"a"})",
          R"({"type":"dir","path":"../escape"})",
          R"({"type":"dir","path":"/","extra":1})",
          R"({"type":"file","path":"a","source":"b","nlink":2})",
          R"({"type":"dir","path":"a","uid":-1})",
          R"({"type":"dir","path":"a","gid":4294967296})",
          R"({"type":"dir","path":"a","mode":644})",
          R"({"type":"dir","path":"a","mode":"0899"})",
          R"({"type":"dir","path":"a","mtime":[0,1000000000]})",
          R"({"type":"dir","path":"a","mtime":[0,-1]})",
          R"({"type":"dir","path":"a","mtime":null})",
          R"({"type":"dir","path":"a","mtime":9223372036854775808})",
          R"({"type":"dir","path":"a","mtime":"nonsense"})",
          R"({"type":"dir","path":"a","uid":1,"uid":2})",
          R"({"type":"hardlink","path":"b","target":"a","mode":"0644"})",
          R"({"type":"hardlink","path":"b","target":"../a"})",
          R"({"type":"symlink","path":"b"})",
          R"({"type":"char","path":"a","major":1})",
          R"({"type":"block","path":"a","minor":2})",
          R"({"type":"char","path":"a","major":1,"minor":2,"device_type":"char"})",
          R"({"type":"device","path":"a","device_type":"char","major":1,"minor":2})",
          R"({"type":"dir","path":"a",})",
          R"([{"type":"dir","path":"a"}])",
      }) {
    SCOPED_TRACE(record);
    std::istringstream list(std::string(record) + "\n");
    auto reader = writer::input_entry_reader_factory::create("jsonl", lgr, *os,
                                                             &list, "/");
    try {
      reader->next();
      FAIL() << "expected an invalid record error";
    } catch (std::exception const& e) {
      EXPECT_THAT(e.what(), testing::HasSubstr("jsonl line 1:"));
    }
  }
}

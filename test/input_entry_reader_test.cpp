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
  EXPECT_EQ(0755, dir->spec->permissions);

  auto primary = reader->next();
  ASSERT_TRUE(primary);
  ASSERT_TRUE(primary->spec);
  EXPECT_EQ(fs::path("etc/config"), primary->path);
  auto const* file = std::get_if<spec::file>(&primary->spec->content);
  ASSERT_TRUE(file);
  EXPECT_EQ(fs::path("/root/relative/config"), file->source);
  EXPECT_EQ((std::vector<fs::path>{"etc/config2", "etc/config3"}),
            file->aliases);
  EXPECT_EQ(3, file->nlink);
  EXPECT_EQ(0640, primary->spec->permissions);
  EXPECT_EQ(123, primary->spec->uid);
  EXPECT_EQ(456, primary->spec->gid);

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

TEST(input_entry_reader, factory_lists_supported_formats) {
  EXPECT_THAT(writer::input_entry_reader_factory::names(),
              testing::ElementsAre("plain", "gen_init_cpio"));
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
  EXPECT_EQ(0700, root->spec->permissions);
  EXPECT_EQ(17, root->spec->uid);
  EXPECT_EQ(18, root->spec->gid);
  auto etc = reader->next();
  ASSERT_TRUE(etc);
  EXPECT_EQ(fs::path("etc"), etc->path);
  EXPECT_FALSE(reader->next());
}

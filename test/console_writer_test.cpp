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

#include <algorithm>
#include <locale>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/util.h>
#include <dwarfs/writer/console_writer.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/writer_progress.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::writer;
using namespace dwarfs::test;
using testing::HasSubstr;
using testing::Not;

namespace {

// Force a known grouping convention without relying on installed locales.
struct comma_grouping : std::numpunct<char> {
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

struct scoped_grouping_locale {
  scoped_grouping_locale()
      : old_locale(std::locale::global(
            std::locale(std::locale::classic(), new comma_grouping))) {}

  ~scoped_grouping_locale() { std::locale::global(old_locale); }

  std::locale old_locale;
};

struct active_context : dwarfs::writer::internal::progress::context {
  status get_status() const override {
    return {
        .color = termcolor::CYAN,
        .context = "hashing",
        .status_string = "processing",
        .path = std::string(150, 'x') + "/我爱你/filename\nwith\tesc\x1b[1m",
        .bytes_processed = 10000,
        .bytes_total = 20000,
    };
  }
};

struct aligned_context : dwarfs::writer::internal::progress::context {
  aligned_context(std::string label, std::string status_text, std::string path,
                  bool with_speed, bool with_total)
      : label(std::move(label))
      , status_text(std::move(status_text))
      , path(std::move(path))
      , with_speed(with_speed)
      , with_total(with_total) {}

  status get_status() const override {
    return {
        .color = termcolor::CYAN,
        .context = label,
        .status_string = status_text,
        .path = path,
        .bytes_processed =
            with_speed ? std::optional<file_size_t>{500} : std::nullopt,
        .bytes_total =
            with_total ? std::optional<file_size_t>{1000} : std::nullopt,
    };
  }

  std::string label;
  std::string status_text;
  std::string path;
  bool with_speed;
  bool with_total;
};

void populate(writer_progress& progress) {
  auto& p = progress.get_internal();
  p.dirs_scanned = 123456789;
  p.files_scanned = 123456789;
  p.files_found = 123456789;
  p.symlinks_scanned = 12345;
  p.hardlinks = 12345;
  p.specials_found = 12345;
  p.original_size = UINT64_C(1) << 40;
  p.hash.bytes = UINT64_C(1) << 39;
  p.hash.scans = 12345678;
  p.similarity.bytes = UINT64_C(1) << 38;
  p.similarity.scans = 12345678;
  p.saved_by_deduplication = UINT64_C(1) << 30;
  p.saved_by_segmentation = UINT64_C(1) << 29;
  p.filesystem_size = UINT64_C(1) << 37;
  p.block_count = 100000;
  p.blocks_written = 25000;
  p.compressed_size = UINT64_C(1) << 36;
  p.chunk_count = 100000;
  p.fragments_found = 200000;
  p.set_status_function(
      [](auto const&, size_t) { return std::string(100, 'a') + "\n\x1b[2K"; });
}

std::vector<std::string> lines(std::string const& frame) {
  std::vector<std::string> result;
  std::istringstream in(frame);
  std::string line;
  while (std::getline(in, line)) {
    result.push_back(line);
  }
  return result;
}

} // namespace

TEST(console_writer, fits_all_modes_and_widths) {
  for (auto const mode : {console_writer::NORMAL, console_writer::REWRITE}) {
    for (auto const progress_mode :
         {console_writer::ASCII, console_writer::UNICODE}) {
      for (auto const width : {0, 1, 39, 40, 41, 59, 60, 80, 120}) {
        SCOPED_TRACE(width);
        test_iolayer t;
        t.set_terminal_width(width);
        auto const& io = t.get();
        writer_progress progress;
        populate(progress);
        auto ctx = progress.get_internal().create_context<active_context>();
        console_writer cw(io.term, t.err_stream(), *io.os,
                          {.progress = progress_mode, .display = mode});
        cw.update(progress, false);
        auto const output = t.err();
        if (width < 40) {
          EXPECT_TRUE(output.empty());
          continue;
        }
        ASSERT_FALSE(output.empty());
        auto const rows = lines(output);
        EXPECT_EQ(rows.size(), (mode == console_writer::NORMAL ? 9 : 4) +
                                   (width >= 60 ? 1 : 0));
        for (auto const& row : rows) {
          EXPECT_LE(utf8_display_width(row), width - 1) << row;
          EXPECT_THAT(row, Not(HasSubstr("\x1b")));
          EXPECT_THAT(row, Not(HasSubstr("\t")));
        }
      }
    }
  }
}

TEST(console_writer, context_columns_are_aligned_and_fill_width) {
  for (auto const progress_mode :
       {console_writer::ASCII, console_writer::UNICODE}) {
    for (auto const width : {60, 80, 120, 180}) {
      SCOPED_TRACE(width);
      test_iolayer t;
      t.set_terminal_width(width);
      auto const& io = t.get();
      writer_progress progress;
      populate(progress);
      auto short_path = progress.get_internal().create_context<aligned_context>(
          "[scanning]", "", "/usr/bin/deno", true, true);
      auto long_path = progress.get_internal().create_context<aligned_context>(
          "[categorizing]", "processing",
          "/usr/src/linux-6.6.119-gentoo-dist/我爱你/vmlinux", true, true);
      auto no_total = progress.get_internal().create_context<aligned_context>(
          "[no-total]", "", "/tmp/example", true, false);
      auto no_speed = progress.get_internal().create_context<aligned_context>(
          "[waiting]", "", "/tmp/idle", false, false);

      console_writer cw(io.term, t.err_stream(), *io.os,
                        {.progress = progress_mode});
      cw.update(progress, false);
      auto const rows = lines(t.err());
      ASSERT_EQ(rows.size(), 13); // Nine summary rows, four contexts.

      std::vector<size_t> bar_end_columns;
      for (size_t i = 9; i < rows.size(); ++i) {
        auto const& row = rows[i];
        EXPECT_EQ(utf8_display_width(row), width - 1) << row;
        EXPECT_THAT(row, Not(HasSubstr("\x1b")));
        if (row.starts_with("[scanning]") ||
            row.starts_with("[categorizing]")) {
          auto const bar_end =
              row.rfind(progress_mode == console_writer::ASCII ? "=" : "▏");
          ASSERT_NE(bar_end, std::string::npos);
          bar_end_columns.push_back(utf8_display_width(row.substr(0, bar_end)));
          EXPECT_TRUE(row.ends_with("/s")) << row;
          if (width == 180 && row.starts_with("[categorizing]")) {
            EXPECT_THAT(row, HasSubstr("vmlinux"));
          }
        }
      }
      ASSERT_EQ(bar_end_columns.size(), 2);
      EXPECT_EQ(bar_end_columns[0], bar_end_columns[1]);
    }
  }
}

TEST(console_writer, counters_follow_locale_in_full_and_compact_lines) {
  scoped_grouping_locale locale;
  writer_progress progress;
  populate(progress);
  auto& p = progress.get_internal();
  p.duplicate_files = 1234;
  p.fragments_written = 188888;
  p.inodes_written = 123456;

  for (auto const mode : {console_writer::NORMAL, console_writer::REWRITE}) {
    test_iolayer wide;
    wide.set_terminal_width(240);
    auto const& io = wide.get();
    console_writer cw(io.term, wide.err_stream(), *io.os,
                      {.progress = console_writer::ASCII, .display = mode});
    cw.update(progress, false);

    auto const output = wide.err();
    EXPECT_THAT(output, HasSubstr(" in 100,000 blocks"));
    if (mode == console_writer::NORMAL) {
      EXPECT_THAT(output, HasSubstr("123,456,789 dirs"));
      EXPECT_THAT(output, HasSubstr("12,345/12,345 soft/hard links"));
      EXPECT_THAT(output, HasSubstr("123,456,789/123,456,789 files"));
      EXPECT_THAT(output, HasSubstr("12,345,678 files"));
      EXPECT_THAT(output, HasSubstr("1,234 files"));
      EXPECT_THAT(output,
                  HasSubstr("100,000 chunks, 188,888/200,000 fragments"));
      EXPECT_THAT(output, HasSubstr("25,000 blocks/"));
    } else {
      EXPECT_THAT(output, HasSubstr("100,000 chunks, 123,456 inodes"));
      EXPECT_THAT(output, HasSubstr("25,000/100,000 blocks/"));
    }
  }

  test_iolayer narrow;
  narrow.set_terminal_width(45);
  auto const& io = narrow.get();
  console_writer cw(io.term, narrow.err_stream(), *io.os,
                    {.progress = console_writer::ASCII});
  cw.update(progress, false);
  EXPECT_THAT(narrow.err(), HasSubstr("100,000 blocks"));
  for (auto const& row : lines(narrow.err())) {
    EXPECT_LE(utf8_display_width(row), 44) << row;
  }

  test_iolayer plain;
  auto const& plain_io = plain.get();
  console_writer plain_writer(plain_io.term, plain.err_stream(), *plain_io.os,
                              {.progress = console_writer::NONE},
                              {.threshold = logger::INFO});
  plain_writer.update(progress, true);
  EXPECT_THAT(plain.err(), HasSubstr("123,456,789 dirs"));
}

TEST(console_writer, redraws_only_when_width_is_unchanged) {
  test_iolayer t;
  t.set_terminal_width(80);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);
  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::ASCII});
  cw.update(progress, false);
  auto const initial = t.err();
  EXPECT_THAT(initial, Not(HasSubstr("<rewind>")));

  // Same-sized frame is erased in place.
  cw.update(progress, false);
  auto const same_width = t.err().substr(initial.size());
  EXPECT_EQ(std::count(same_width.begin(), same_width.end(), '\n'), 9);
  EXPECT_EQ(same_width.find("<cr>"), 0);
  EXPECT_EQ(std::string_view(same_width).starts_with("<cr><rewind><clear>"),
            true);
  EXPECT_EQ(std::ranges::count(same_width, '<'), 19);

  // A smaller terminal may have reflowed the old frame, do not rewind it.
  auto const before_resize = t.err().size();
  t.set_terminal_width(45);
  cw.update(progress, false);
  auto const after_resize = t.err().substr(before_resize);
  EXPECT_THAT(after_resize, Not(HasSubstr("<rewind>")));
  EXPECT_EQ(lines(after_resize).size(), 9);
  for (auto const& row : lines(after_resize)) {
    EXPECT_LE(utf8_display_width(row), 44);
  }

  t.set_terminal_width(25);
  auto const before_pause = t.err();
  cw.update(progress, false);
  EXPECT_EQ(t.err(), before_pause);

  t.set_terminal_width(60);
  cw.update(progress, false);
  auto const resumed = t.err().substr(before_pause.size());
  EXPECT_THAT(resumed, Not(HasSubstr("<rewind>")));
}

TEST(console_writer, context_rows_are_counted_from_the_rendered_frame) {
  test_iolayer t;
  t.set_terminal_width(80);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);
  auto context = progress.get_internal().create_context<active_context>();
  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::ASCII});
  cw.update(progress, false);
  EXPECT_EQ(lines(t.err()).size(), 10);

  // When a worker context disappears, the next update must clear all rows.
  auto const before = t.err().size();
  context.reset();
  cw.update(progress, false);
  auto const redraw = t.err().substr(before);
  EXPECT_EQ(std::ranges::count(redraw, '<'), 21);
  EXPECT_EQ(lines(redraw).size(), 9);
}

TEST(console_writer, log_messages_preserve_frame_unless_resized) {
  test_iolayer t;
  t.set_terminal_width(80);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);
  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::ASCII},
                    {.threshold = logger::INFO});
  cw.update(progress, false);
  auto before = t.err().size();

  cw.write(logger::INFO, "first log message", DWARFS_CURRENT_SOURCE_LOCATION);
  auto const with_log = t.err().substr(before);
  EXPECT_THAT(with_log, HasSubstr("first log message"));
  EXPECT_THAT(with_log, HasSubstr("<rewind><clear>"));
  EXPECT_THAT(with_log, HasSubstr("filesystem:"));

  t.set_terminal_width(45);
  before = t.err().size();
  cw.write(logger::INFO, "after resize", DWARFS_CURRENT_SOURCE_LOCATION);
  auto const resized_log = t.err().substr(before);
  EXPECT_THAT(resized_log, HasSubstr("after resize"));
  EXPECT_THAT(resized_log, Not(HasSubstr("<rewind>")));
  EXPECT_THAT(resized_log, Not(HasSubstr("filesystem:")));

  before = t.err().size();
  cw.update(progress, false);
  EXPECT_THAT(t.err().substr(before), Not(HasSubstr("<rewind>")));
}

TEST(console_writer, narrow_final_summary_is_not_a_live_frame) {
  test_iolayer t;
  t.set_terminal_width(20);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);
  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::ASCII});
  cw.update(progress, false);
  EXPECT_TRUE(t.err().empty());
  cw.update(progress, true);
  EXPECT_THAT(t.err(), HasSubstr("compressed filesystem:"));
  EXPECT_THAT(t.err(), Not(HasSubstr("<rewind>")));
}

TEST(console_writer, contexts_use_available_terminal_height) {
  for (auto const mode : {console_writer::NORMAL, console_writer::REWRITE}) {
    auto const fixed_rows = mode == console_writer::NORMAL ? 9 : 4;

    for (auto const height : {0, 1, 9, 10, 11, 12, 14, 20, 40, 64}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(height);
      test_iolayer t;
      t.set_terminal_width(120);
      t.set_terminal_height(height);
      auto const& io = t.get();
      writer_progress progress;
      populate(progress);

      std::vector<std::shared_ptr<aligned_context>> contexts;

      for (int i = 0; i < 50; ++i) {
        contexts.push_back(
            progress.get_internal().create_context<aligned_context>(
                "[scanning]", "", "/tmp/file", true, true));
      }

      console_writer cw(io.term, t.err_stream(), *io.os,
                        {.progress = console_writer::ASCII, .display = mode});
      cw.update(progress, false);

      auto const expected = height < 10 || height < fixed_rows + 2
                                ? 0
                                : std::min(fixed_rows + 50, height - 2);
      auto const rows = lines(t.err());
      EXPECT_EQ(rows.size(), expected);

      for (auto const& row : rows) {
        EXPECT_LE(utf8_display_width(row), 119) << row;
      }
    }
  }
}

TEST(console_writer, height_resize_abandons_previous_frame) {
  test_iolayer t;
  t.set_terminal_width(100);
  t.set_terminal_height(20);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);

  std::vector<std::shared_ptr<aligned_context>> contexts;

  for (int i = 0; i < 30; ++i) {
    contexts.push_back(progress.get_internal().create_context<aligned_context>(
        "[scanning]", "", "/tmp/file", true, true));
  }

  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::ASCII},
                    {.threshold = logger::INFO});
  cw.update(progress, false);
  EXPECT_EQ(lines(t.err()).size(), 18);

  // At a fixed size, the previous frame is erased normally.
  auto before = t.err().size();
  cw.update(progress, false);
  EXPECT_THAT(t.err().substr(before), HasSubstr("<rewind>"));

  // A height-only resize must not rewind the previous frame.
  t.set_terminal_height(30);
  before = t.err().size();
  cw.update(progress, false);
  auto const grown = t.err().substr(before);
  EXPECT_THAT(grown, Not(HasSubstr("<rewind>")));
  EXPECT_EQ(lines(grown).size(), 28);

  // A log message after a height change must not rewind the previous frame.
  t.set_terminal_height(25);
  before = t.err().size();
  cw.write(logger::INFO, "height changed", DWARFS_CURRENT_SOURCE_LOCATION);
  auto const log = t.err().substr(before);
  EXPECT_THAT(log, HasSubstr("height changed"));
  EXPECT_THAT(log, Not(HasSubstr("<rewind>")));
  EXPECT_THAT(log, Not(HasSubstr("filesystem:")));

  // Suspend the output if the terminal is too short.
  t.set_terminal_height(9);
  before = t.err().size();
  cw.update(progress, false);
  EXPECT_EQ(t.err().size(), before);

  // Start a new frame if the terminal becomes tall enough again.
  t.set_terminal_height(32);
  cw.update(progress, false);
  auto const resumed = t.err().substr(before);
  EXPECT_THAT(resumed, Not(HasSubstr("<rewind>")));
  EXPECT_EQ(lines(resumed).size(), 30);
}

TEST(console_writer, short_terminal_still_prints_final_summary) {
  test_iolayer t;
  t.set_terminal_width(80);
  t.set_terminal_height(9);
  auto const& io = t.get();
  writer_progress progress;
  populate(progress);
  console_writer cw(io.term, t.err_stream(), *io.os,
                    {.progress = console_writer::UNICODE});
  cw.update(progress, false);
  EXPECT_TRUE(t.err().empty());
  cw.update(progress, true);
  EXPECT_THAT(t.err(), HasSubstr("compressed filesystem:"));
  EXPECT_THAT(t.err(), Not(HasSubstr("<rewind>")));
}

TEST(console_writer, terminal_dimensions_include_height) {
  test_terminal term;
  EXPECT_EQ(term.dimensions(), (terminal_dimensions{80, 24}));
  EXPECT_EQ(term.width(), 80);
  term.set_width(123);
  term.set_height(17);
  EXPECT_EQ(term.dimensions(), (terminal_dimensions{123, 17}));
  EXPECT_EQ(term.width(), 123);
}

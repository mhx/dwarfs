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

#include <cstdlib>
#include <cstring>
#include <sstream>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/small_vector.h>

#include <fmt/format.h>

#include "dwarfs/console_writer.h"
#include "dwarfs/entry.h"
#include "dwarfs/entry_interface.h"
#include "dwarfs/inode.h"
#include "dwarfs/lazy_value.h"
#include "dwarfs/logger.h"
#include "dwarfs/progress.h"
#include "dwarfs/terminal.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

constexpr std::array<char const*, 8> asc_bar{
    {"=", "=", "=", "=", "=", "=", "=", "="}};
constexpr std::array<char const*, 8> uni_bar{
    {"▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"}};

bool is_debug_progress() {
  if (auto var = ::getenv("DWARFS_DEBUG_PROGRESS")) {
    if (auto val = folly::tryTo<bool>(var)) {
      return *val;
    }
  }
  return false;
}

std::string progress_bar(size_t width, double frac, bool unicode) {
  size_t barlen = 8 * width * frac;
  size_t w = barlen / 8;
  size_t c = barlen % 8;

  auto bar = unicode ? uni_bar.data() : asc_bar.data();
  std::string rv;

  for (size_t i = 0; i < width; ++i) {
    if (i == (width - 1)) {
      rv.append(bar[0]);
    } else if (i == w) {
      rv.append(bar[c]);
    } else {
      rv.append(i < w ? bar[7] : " ");
    }
  }

  return rv;
}

} // namespace

console_writer::console_writer(std::ostream& os, progress_mode pg_mode,
                               get_term_width_type get_term_width,
                               level_type threshold, display_mode mode,
                               bool with_context)
    : os_(os)
    , threshold_(threshold)
    , frac_(0.0)
    , pg_mode_(pg_mode)
    , get_term_width_(get_term_width)
    , mode_(mode)
    , color_(stream_is_fancy_terminal(os))
    , with_context_(with_context)
    , debug_progress_(is_debug_progress())
    , read_speed_{std::chrono::seconds(5)} {
  if (threshold > level_type::INFO) {
    set_policy<debug_logger_policy>();
  } else {
    set_policy<prod_logger_policy>();
  }
}

void console_writer::rewind() {
  if (!statebuf_.empty()) {
    int lines = 0;

    switch (mode_) {
    case NORMAL:
      lines = 9;
      break;
    case REWRITE:
      lines = 4;
      break;
    }

    os_ << '\r';

    for (int i = 0; i < lines; ++i) {
      os_ << "\x1b[A";
    }
  }
}

void console_writer::write(level_type level, const std::string& output,
                           char const* file, int line) {
  if (level <= threshold_) {
    auto t = get_current_time_string();
    const char* prefix = "";
    const char* suffix = "";
    const char* newline = pg_mode_ != NONE ? "\x1b[K\n" : "\n";

    if (color_) {
      switch (level) {
      case ERROR:
        prefix = terminal_color(termcolor::BOLD_RED);
        suffix = terminal_color(termcolor::NORMAL);
        break;

      case WARN:
        prefix = terminal_color(termcolor::BOLD_YELLOW);
        suffix = terminal_color(termcolor::NORMAL);
        break;

      default:
        break;
      }
    }

    char lchar = logger::level_char(level);
    std::string context;
    size_t context_len = 0;

    if (with_context_ && file) {
      context = get_logger_context(file, line);
      context_len = context.size();
      if (color_) {
        context = folly::to<std::string>(
            suffix, terminal_color(termcolor::MAGENTA), context,
            terminal_color(termcolor::NORMAL), prefix);
      }
    }

    folly::small_vector<std::string_view, 2> lines;
    folly::split('\n', output, lines);

    if (lines.back().empty()) {
      lines.pop_back();
    }

    std::lock_guard lock(mx_);

    rewind();

    for (auto l : lines) {
      os_ << prefix << lchar << ' ' << t << ' ' << context << l << suffix
          << newline;
      std::fill(t.begin(), t.end(), '.');
      context.assign(context_len, ' ');
    }

    if (pg_mode_ == UNICODE || pg_mode_ == ASCII) {
      os_ << statebuf_;
    }
  }
}

void console_writer::update(const progress& p, bool last) {
  if (pg_mode_ == NONE && !last) {
    return;
  }

  const char* newline = pg_mode_ != NONE ? "\x1b[K\n" : "\n";

  std::ostringstream oss;
  lazy_value width(get_term_width_);

  bool fancy = pg_mode_ == ASCII || pg_mode_ == UNICODE;

  if (last || fancy) {
    if (fancy) {
      for (size_t i = 0; i < width.get(); ++i) {
        oss << (pg_mode_ == UNICODE ? "⎯" : "-");
      }
      oss << "\n";
    }

    switch (mode_) {
    case NORMAL:
      if (writing_) {
        read_speed_.put(p.total_bytes_read.load());
      } else {
        if (p.total_bytes_read.load() > 0) {
          read_speed_.clear();
          writing_ = true;
        } else {
          read_speed_.put(p.similarity_bytes.load());
        }
      }

      if (fancy) {
        oss << terminal_colored(p.status(width.get()), termcolor::BOLD_CYAN,
                                color_)
            << newline;
      }

      {
        auto cur_size = p.current_size.load();
        double cur_offs = std::min(p.current_offset.load(), cur_size);
        double cur_frac = cur_size > 0 ? cur_offs / cur_size : 0.0;

        oss << progress_bar(64, cur_frac, pg_mode_ == UNICODE) << " "
            << size_with_unit(read_speed_.num_per_second()) << "/s" << newline;
      }

      oss << p.dirs_scanned << " dirs, " << p.symlinks_scanned << "/"
          << p.hardlinks << " soft/hard links, " << p.files_scanned << "/"
          << p.files_found << " files, " << p.specials_found << " other"
          << newline

          << "original size: " << size_with_unit(p.original_size)
          << ", scanned: " << size_with_unit(p.similarity_bytes)
          << ", hashed: " << size_with_unit(p.hash_bytes) << " ("
          << p.hash_scans << " files)" << newline

          << "saved by deduplication: "
          << size_with_unit(p.saved_by_deduplication) << " ("
          << p.duplicate_files << " files), saved by segmenting: "
          << size_with_unit(p.saved_by_segmentation) << newline

          << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
          << p.block_count << " blocks (" << p.chunk_count << " chunks, "
          << (p.inodes_written > 0 ? p.inodes_written : p.inodes_scanned) << "/"
          << p.files_found - p.duplicate_files - p.hardlinks << " inodes)"
          << newline

          << "compressed filesystem: " << p.blocks_written << " blocks/"
          << size_with_unit(p.compressed_size) << " written";

      if (debug_progress_) {
        oss << " [" << p.nilsimsa_depth << "/" << p.blockify_queue << "/"
            << p.compress_queue << "]";
      } else {
        if (p.nilsimsa_depth > 0) {
          oss << " [depth: " << p.nilsimsa_depth << "]";
        }
      }

      oss << newline;
      break;

    case REWRITE:
      oss << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
          << p.block_count << " blocks (" << p.chunk_count << " chunks, "
          << p.inodes_written << " inodes)" << newline

          << "compressed filesystem: " << p.blocks_written << "/"
          << p.block_count << " blocks/" << size_with_unit(p.compressed_size)
          << " written" << newline;
      break;
    }
  }

  if (pg_mode_ == NONE) {
    if (INFO <= threshold_) {
      std::lock_guard lock(mx_);
      os_ << oss.str();
    }
    return;
  }

  size_t orig = p.original_size - (p.saved_by_deduplication + p.symlink_size);
  double frac_fs =
      orig > 0 ? double(p.filesystem_size + p.saved_by_segmentation) / orig
               : 0.0;
  double frac_comp =
      p.block_count > 0 ? double(p.blocks_written) / p.block_count : 0.0;
  double frac = mode_ == NORMAL ? (frac_fs + frac_comp) / 2.0 : frac_comp;

  if (last) {
    frac = 1.0;
  }

  if (frac > frac_) {
    frac_ = frac;
  }

  if (pg_mode_ == SIMPLE) {
    std::string tmp =
        fmt::format(" ==> {0:.0f}% done, {1} blocks/{2} written", 100 * frac_,
                    p.blocks_written.load(), size_with_unit(p.compressed_size));
    if (tmp != statebuf_) {
      auto t = get_current_time_string();
      statebuf_ = tmp;
      std::lock_guard lock(mx_);
      os_ << "- " << t << statebuf_ << "\n";
    }
    if (last) {
      std::lock_guard lock(mx_);
      os_ << oss.str();
    }
  } else {
    oss << progress_bar(width.get() - 6, frac_, pg_mode_ == UNICODE)
        << fmt::format("{:3.0f}% ", 100 * frac_) << "-\\|/"[counter_ % 4]
        << '\n';

    ++counter_;

    std::lock_guard lock(mx_);

    rewind();

    statebuf_ = oss.str();

    os_ << statebuf_;
  }
}

} // namespace dwarfs

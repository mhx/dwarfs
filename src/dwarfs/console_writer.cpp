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

#include <locale>
#include <sstream>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <fmt/format.h>

#include "dwarfs/console_writer.h"
#include "dwarfs/entry.h"
#include "dwarfs/entry_interface.h"
#include "dwarfs/inode.h"
#include "dwarfs/progress.h"
#include "dwarfs/terminal.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

char const* const asc_bar[8] = {"=", "=", "=", "=", "=", "=", "=", "="};
char const* const uni_bar[8] = {"▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};

} // namespace

console_writer::console_writer(std::ostream& os, progress_mode pg_mode,
                               size_t width, level_type threshold,
                               display_mode mode)
    : os_(os)
    , threshold_(threshold)
    , frac_(0.0)
    , pg_mode_(pg_mode)
    , width_(width)
    , mode_(mode)
    , color_(stream_is_fancy_terminal(os)) {
  os_.imbue(std::locale(os_.getloc(),
                        new boost::posix_time::time_facet("%H:%M:%S.%f")));
  if (threshold > level_type::INFO) {
    set_policy<debug_logger_policy>();
  } else {
    set_policy<prod_logger_policy>();
  }
}

void console_writer::rewind() {
  if (!statebuf_.empty()) {
    switch (mode_) {
    case NORMAL:
      os_ << "\x1b[A\r\x1b[A\x1b[A\x1b[A\x1b[A\x1b[A\x1b[A";
      break;
    case REWRITE:
      os_ << "\x1b[A\r\x1b[A\x1b[A\x1b[A";
      break;
    }
  }
}

void console_writer::write(level_type level, const std::string& output) {
  if (level <= threshold_) {
    auto t = boost::posix_time::microsec_clock::local_time();
    const char* prefix = "";
    const char* suffix = "";

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

    std::lock_guard<std::mutex> lock(mx_);

    switch (pg_mode_) {
    case UNICODE:
    case ASCII:
      rewind();
      os_ << prefix << t << " " << output << suffix << "\x1b[K\n";
      os_ << statebuf_;
      break;

    default:
      os_ << t << " " << output << "\n";
      break;
    }
  }
}

void console_writer::update(const progress& p, bool last) {
  if (pg_mode_ == NONE && !last) {
    return;
  }

  const char* newline = pg_mode_ != NONE ? "\x1b[K\n" : "\n";

  std::ostringstream oss;

  bool fancy = pg_mode_ == ASCII || pg_mode_ == UNICODE;

  if (last || fancy) {
    if (fancy) {
      for (size_t i = 0; i < width_; ++i) {
        oss << (pg_mode_ == UNICODE ? "⎯" : "-");
      }
      oss << "\n";
    }

    switch (mode_) {
    case NORMAL:
      if (fancy) {
        oss << p.status(width_) << newline;
      }

      oss << p.dirs_scanned << " dirs, " << p.links_scanned << "/"
          << p.hardlinks << " soft/hard links, " << p.files_scanned << "/"
          << p.files_found << " files, " << p.specials_found << " other"
          << newline

          << "original size: " << size_with_unit(p.original_size)
          << ", dedupe: " << size_with_unit(p.saved_by_deduplication) << " ("
          << p.duplicate_files
          << " files), segment: " << size_with_unit(p.saved_by_segmentation)
          << newline

          << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
          << p.block_count << " blocks (" << p.chunk_count << " chunks, "
          << (p.inodes_written > 0 ? p.inodes_written : p.inodes_scanned) << "/"
          << p.files_found - p.duplicate_files << " inodes)" << newline

          << "compressed filesystem: " << p.blocks_written << " blocks/"
          << size_with_unit(p.compressed_size) << " written" << newline;
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
      std::lock_guard<std::mutex> lock(mx_);
      os_ << oss.str();
    }
    return;
  }

  size_t orig = p.original_size - p.saved_by_deduplication;
  double frac_fs =
      orig > 0 ? double(p.filesystem_size + p.saved_by_segmentation) / orig
               : 0.0;
  double frac_comp =
      p.block_count > 0 ? double(p.blocks_written) / p.block_count : 0.0;
  double frac = mode_ == NORMAL ? (frac_fs + frac_comp) / 2.0 : frac_comp;

  if (frac > frac_) {
    frac_ = frac;
  }

  size_t barlen = 8 * (width_ - 6) * frac_;
  size_t w = barlen / 8;
  size_t c = barlen % 8;

  char const* const* bar = pg_mode_ == UNICODE ? uni_bar : asc_bar;

  if (pg_mode_ == SIMPLE) {
    std::string tmp =
        fmt::format(" ==> {0:.0f}% done, {1} blocks/{2} written", 100 * frac_,
                    p.blocks_written, size_with_unit(p.compressed_size));
    if (tmp != statebuf_) {
      auto t = boost::posix_time::microsec_clock::local_time();
      statebuf_ = tmp;
      std::lock_guard<std::mutex> lock(mx_);
      os_ << t << statebuf_ << "\n";
    }
    if (last) {
      std::lock_guard<std::mutex> lock(mx_);
      os_ << oss.str();
    }
  } else {
    for (size_t i = 0; i < width_ - 6; ++i) {
      if (i == (width_ - 7)) {
        oss << bar[0];
      } else if (i == w && !last) {
        oss << bar[c];
      } else {
        oss << (i < w ? bar[7] : " ");
      }
    }
    oss << fmt::format("{:3.0f}% ", 100 * frac_) << "-\\|/"[counter_ % 4]
        << '\n';

    ++counter_;

    std::lock_guard<std::mutex> lock(mx_);

    rewind();

    statebuf_ = oss.str();

    os_ << statebuf_;
  }
}

} // namespace dwarfs

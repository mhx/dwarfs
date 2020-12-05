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
#include "dwarfs/util.h"

namespace dwarfs {

console_writer::console_writer(std::ostream& os, bool show_progress,
                               size_t width, level_type threshold,
                               display_mode mode)
    : os_(os)
    , threshold_(threshold)
    , frac_(0.0)
    , show_progress_(show_progress)
    , width_(width)
    , mode_(mode) {
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

    switch (level) {
    case ERROR:
      prefix = "\033[1;31m";
      suffix = "\033[0m";
      break;

    case WARN:
      prefix = "\033[1;33m";
      suffix = "\033[0m";
      break;

    default:
      break;
    }

    std::lock_guard<std::mutex> lock(mx_);

    if (show_progress_) {
      rewind();
      os_ << prefix << t << " " << output << suffix << "\x1b[K\n";
      os_ << statebuf_;
    } else {
      os_ << t << " " << output << "\n";
    }
  }
}

void console_writer::update(const progress& p, bool last) {
  if (!show_progress_ && !last) {
    return;
  }

  const char* newline = show_progress_ ? "\x1b[K\n" : "\n";

  std::ostringstream oss;

  if (show_progress_) {
    for (size_t i = 0; i < width_; ++i) {
      oss << "⎯";
    }
    oss << "\n";
  }

  switch (mode_) {
  case NORMAL:
    oss << p.status(width_) << newline

        << "scanned/found: " << p.dirs_scanned << "/" << p.dirs_found
        << " dirs, " << p.links_scanned << "/" << p.links_found << " links, "
        << p.files_scanned << "/" << p.files_found << " files" << newline

        << "original size: " << size_with_unit(p.original_size)
        << ", dedupe: " << size_with_unit(p.saved_by_deduplication) << " ("
        << p.duplicate_files
        << " files), segment: " << size_with_unit(p.saved_by_segmentation)
        << newline

        << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
        << p.block_count << " blocks (" << p.chunk_count << " chunks, "
        << p.inodes_written << "/" << p.files_found - p.duplicate_files
        << " inodes)" << newline

        << "compressed filesystem: " << p.blocks_written << " blocks/"
        << size_with_unit(p.compressed_size) << " written" << newline;
    break;

  case REWRITE:
    oss << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
        << p.block_count << " blocks (" << p.chunk_count << " chunks, "
        << p.inodes_written << " inodes)" << newline

        << "compressed filesystem: " << p.blocks_written << "/" << p.block_count
        << " blocks/" << size_with_unit(p.compressed_size) << " written"
        << newline;
    break;
  }

  if (show_progress_) {
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

    static const char* bar[8] = {"▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};

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
  } else {
    if (INFO <= threshold_) {
      std::lock_guard<std::mutex> lock(mx_);
      os_ << oss.str();
    }
  }
}
} // namespace dwarfs

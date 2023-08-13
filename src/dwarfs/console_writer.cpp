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

#include <cstring>
#include <sstream>

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
    : stream_logger(os, threshold, with_context)
    , frac_(0.0)
    , pg_mode_(pg_mode)
    , get_term_width_(get_term_width)
    , mode_(mode)
    , debug_progress_(getenv_is_enabled("DWARFS_DEBUG_PROGRESS"))
    , read_speed_{std::chrono::seconds(5)} {}

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

    auto& os = log_stream();

    os << '\r';

    for (int i = 0; i < lines; ++i) {
      os << "\x1b[A";
    }
  }
}

void console_writer::preamble() { rewind(); }

void console_writer::postamble() {
  if (pg_mode_ == UNICODE || pg_mode_ == ASCII) {
    log_stream() << statebuf_;
  }
}

std::string_view console_writer::get_newline() const {
  return pg_mode_ != NONE ? "\x1b[K\n" : "\n";
}

void console_writer::update(const progress& p, bool last) {
  if (pg_mode_ == NONE && !last) {
    return;
  }

  auto newline = get_newline();

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
                                log_is_colored())
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
    if (INFO <= log_threshold()) {
      std::lock_guard lock(log_mutex());
      log_stream() << oss.str();
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
      std::lock_guard lock(log_mutex());
      log_stream() << "- " << t << statebuf_ << "\n";
    }
    if (last) {
      std::lock_guard lock(log_mutex());
      log_stream() << oss.str();
    }
  } else {
    oss << progress_bar(width.get() - 6, frac_, pg_mode_ == UNICODE)
        << fmt::format("{:3.0f}% ", 100 * frac_) << "-\\|/"[counter_ % 4]
        << '\n';

    ++counter_;

    std::lock_guard lock(log_mutex());

    rewind();

    statebuf_ = oss.str();

    log_stream() << statebuf_;
  }
}

} // namespace dwarfs

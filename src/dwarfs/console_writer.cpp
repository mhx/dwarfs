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
#include <string_view>

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

constexpr std::array<std::string_view, 8> asc_bar{
    {"=", "=", "=", "=", "=", "=", "=", "="}};
constexpr std::array<std::string_view, 8> uni_bar{
    {"▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"}};

constexpr std::array<std::string_view, 4> asc_spinner_def{
    {"-", "\\", "|", "/"}};

constexpr std::array<std::string_view, 8> uni_spinner_def{
    {"🌑", "🌒", "🌓", "🌔", "🌕", "🌖", "🌗", "🌘"}};

constexpr std::span<std::string_view const> asc_spinner{asc_spinner_def};
constexpr std::span<std::string_view const> uni_spinner{uni_spinner_def};

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

void output_context_line(terminal const& term, std::ostream& os,
                         progress::context& ctx, size_t width, bool unicode_bar,
                         bool colored) {
  auto st = ctx.get_status();
  size_t progress_w = 0;
  size_t speed_w = 0;

  // Progress bar width and speed width are both fixed
  if (st.bytes_processed) {
    speed_w = 12;
    if (st.bytes_total) {
      progress_w = width / 4;
    }
  }

  assert(width >= progress_w + speed_w + 1);

  size_t status_w = width - (progress_w + speed_w + 1);
  auto path_len = st.path ? utf8_display_width(*st.path) : 0;
  size_t extra_len = st.path && !st.status_string.empty() ? 2 : 0;

  if (status_w <
      st.context.size() + st.status_string.size() + path_len + extra_len) {
    // need to shorten things
    if (path_len > 0) {
      auto max_path_len =
          status_w -
          std::min(status_w,
                   st.context.size() + st.status_string.size() + extra_len);

      if (max_path_len > 0) {
        shorten_path_string(
            *st.path,
            static_cast<char>(std::filesystem::path::preferred_separator),
            max_path_len);

        path_len = utf8_display_width(*st.path);
      }
    }

    if (path_len == 0 &&
        status_w < st.context.size() + st.status_string.size()) {
      if (status_w < st.context.size()) {
        st.context.clear();
      }

      st.status_string.resize(status_w - st.context.size());
    }
  }

  if (path_len > 0) {
    if (!st.status_string.empty()) {
      st.status_string += ": ";
    }
    st.status_string += *st.path;
  }

  std::string progress;
  std::string speed;

  if (st.bytes_processed) {
    ctx.speed.put(*st.bytes_processed);
    speed = fmt::format("{}/s", size_with_unit(ctx.speed.num_per_second()));

    if (st.bytes_total) {
      double frac = static_cast<double>(*st.bytes_processed) / *st.bytes_total;
      progress = progress_bar(progress_w, frac, unicode_bar);
    }
  }

  os << term.colored(st.context, st.color, colored, termstyle::BOLD);

  os << term.colored(fmt::format("{:<{}} {}{}", st.status_string,
                                 status_w - st.context.size(), progress, speed),
                     st.color, colored);
}

} // namespace

console_writer::console_writer(std::shared_ptr<terminal const> term,
                               std::ostream& os, progress_mode pg_mode,
                               display_mode mode, logger_options const& options)
    : stream_logger(term, os, options)
    , frac_(0.0)
    , pg_mode_(pg_mode)
    , mode_(mode) {}

void console_writer::rewind(std::ostream& os, int next_rewind_lines) {
  if (!statebuf_.empty()) {
    auto& term = this->term();
    auto clear_line = term.clear_line();
    auto rewind_line = term.rewind_line();

    os << term.carriage_return();

    int num_erase = rewind_lines_ - next_rewind_lines;

    for (int i = 0; i < rewind_lines_; ++i) {
      os << rewind_line;
      if (num_erase > 0) {
        os << clear_line;
        --num_erase;
      }
    }
  }

  rewind_lines_ = next_rewind_lines;
}

void console_writer::preamble(std::ostream& os) { rewind(os, rewind_lines_); }

void console_writer::postamble(std::ostream& os) {
  if (pg_mode_ == UNICODE || pg_mode_ == ASCII) {
    os << statebuf_;
  }
}

std::string_view console_writer::get_newline() const {
  return pg_mode_ != NONE ? "\x1b[K\n" : "\n";
}

void console_writer::update(progress& p, bool last) {
  if (pg_mode_ == NONE && !last) {
    return;
  }

  auto newline = get_newline();

  std::ostringstream oss;

  lazy_value<size_t> width([this] { return term().width(); });

  bool fancy = pg_mode_ == ASCII || pg_mode_ == UNICODE;

  auto update_chunk_size = [](progress::scan_progress& sp) {
    if (auto usec = sp.usec.load(); usec > 10'000) {
      auto bytes = sp.bytes.load();
      auto bytes_per_second = (bytes << 20) / usec;
      sp.chunk_size.store(std::min(
          UINT64_C(1) << 25,
          std::max(UINT64_C(1) << 15, std::bit_ceil(bytes_per_second / 32))));
      sp.bytes_per_sec.store(bytes_per_second);
    }
  };

  update_chunk_size(p.hash);
  update_chunk_size(p.similarity);
  update_chunk_size(p.categorize);

  if (last || fancy) {
    if (fancy) {
      for (size_t i = 0; i < width.get(); ++i) {
        oss << (pg_mode_ == UNICODE ? "⎯" : "-");
      }
      oss << "\n";
    }

    switch (mode_) {
    case NORMAL:
      if (fancy) {
        oss << term().colored(p.status(width.get()), termcolor::BOLD_CYAN,
                              log_is_colored())
            << newline;
      }

      oss << p.dirs_scanned << " dirs, " << p.symlinks_scanned << "/"
          << p.hardlinks << " soft/hard links, " << p.files_scanned << "/"
          << p.files_found << " files, " << p.specials_found << " other"
          << newline

          << "original size: " << size_with_unit(p.original_size)
          << ", hashed: " << size_with_unit(p.hash.bytes) << " ("
          << p.hash.scans << " files, " << size_with_unit(p.hash.bytes_per_sec)
          << "/s)" << newline

          << "scanned: " << size_with_unit(p.similarity.bytes) << " ("
          << p.similarity.scans << " files, "
          << size_with_unit(p.similarity.bytes_per_sec) << "/s)"
          << ", categorizing: " << size_with_unit(p.categorize.bytes_per_sec)
          << "/s" << newline

          << "saved by deduplication: "
          << size_with_unit(p.saved_by_deduplication) << " ("
          << p.duplicate_files << " files), saved by segmenting: "
          << size_with_unit(p.saved_by_segmentation) << newline

          << "filesystem: " << size_with_unit(p.filesystem_size) << " in "
          << p.block_count << " blocks (" << p.chunk_count << " chunks, ";

      if (p.fragments_written > 0) {
        oss << p.fragments_written << "/" << p.fragments_found
            << " fragments, ";
      } else {
        oss << p.fragments_found << " fragments, " << p.inodes_scanned << "/";
      }

      oss << p.files_found - p.duplicate_files - p.hardlinks << " inodes)"
          << newline

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
    if (INFO <= log_threshold()) {
      std::lock_guard lock(log_mutex());
      write_nolock(oss.str());
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
    std::lock_guard lock(log_mutex());
    if (tmp != statebuf_) {
      auto t = get_current_time_string();
      statebuf_ = tmp;
      write_nolock(fmt::format("- {}{}\n", t, statebuf_));
    }
    if (last) {
      write_nolock(oss.str());
    }
  } else {
    auto w = width.get();
    auto spinner{pg_mode_ == UNICODE ? uni_spinner : asc_spinner};

    oss << progress_bar(w - 8, frac_, pg_mode_ == UNICODE)
        << fmt::format("{:3.0f}% ", 100 * frac_)
        << spinner[counter_ % spinner.size()] << '\n';

    ++counter_;

    std::vector<std::shared_ptr<progress::context>> ctxs;

    if (w >= 60) {
      ctxs = p.get_active_contexts();
    }

    for (auto const& c : ctxs) {
      output_context_line(term(), oss, *c, w, pg_mode_ == UNICODE,
                          log_is_colored());
      oss << newline;
    }

    std::lock_guard lock(log_mutex());

    statebuf_ = oss.str();

    oss.clear();
    oss.seekp(0);

    rewind(oss, (mode_ == NORMAL ? 9 : 4) + ctxs.size());
    oss << statebuf_;

    write_nolock(oss.str());
  }
}

} // namespace dwarfs

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
#include <array>
#include <bit>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/logger.h>
#include <dwarfs/terminal.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/console_writer.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer {

namespace {

constexpr size_t kMinLiveWidth = 40;
constexpr int kMandatory = 100;

constexpr std::array<std::string_view, 8> asc_bar{
    {"=", "=", "=", "=", "=", "=", "=", "="}};
constexpr std::array<std::string_view, 8> uni_bar{
    {"▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"}};
constexpr std::array<std::string_view, 4> asc_spinner{{"-", "\\", "|", "/"}};
constexpr std::array<std::string_view, 8> uni_spinner{
    {"🌑", "🌒", "🌓", "🌔", "🌕", "🌖", "🌗", "🌘"}};

// A field includes its own leading separator. Dropping it never
// leaves behind a dangling comma, colon, or pair of parentheses.
struct field {
  std::string full;
  std::string short_text;
  int priority{kMandatory};
};

using line_fields = std::vector<field>;

// Clean up untrusted text before inspecing it further.
// A filename/status must not be able to insert rows or cursor controls.
std::string printable(std::string text) {
  utf8_sanitize(text);
  std::ranges::replace_if(
      text, [](unsigned char c) { return c < 0x20 || c == 0x7f; }, '?');
  return text;
}

size_t display_width(std::string const& text) {
  return utf8_display_width(text);
}

void truncate_to(std::string& text, size_t width) {
  if (display_width(text) > width) {
    utf8_truncate(text, width);
  }
}

// First try shorter lower-priority fields, then omit optional fields.
// Finally truncate the mandatory text if even that won't fit.
// Returning individual fields lets the context renderer color its label
// independently.
std::vector<std::string> fit_fields(line_fields fields, size_t width) {
  std::vector<std::string> values;
  values.reserve(fields.size());

  for (auto const& f : fields) {
    values.push_back(f.full);
  }

  auto total_width = [&] {
    return std::accumulate(values.begin(), values.end(), size_t{0},
                           [](size_t sum, std::string const& s) {
                             return sum + display_width(s);
                           });
  };

  std::vector<size_t> order(fields.size());
  std::iota(order.begin(), order.end(), 0);
  std::ranges::stable_sort(order, [&](auto const a, auto const b) {
    return fields[a].priority < fields[b].priority;
  });

  for (auto i : order) {
    if (total_width() <= width) {
      break;
    }

    if (!fields[i].short_text.empty()) {
      values[i] = fields[i].short_text;
    }
  }

  for (auto i : order) {
    if (total_width() <= width) {
      break;
    }

    if (fields[i].priority != kMandatory) {
      values[i].clear();
    }
  }

  for (auto i : order) {
    if (total_width() <= width) {
      break;
    }

    // Truncate mandatory fields as a last resort.
    size_t other_width = total_width() - display_width(values[i]);
    truncate_to(values[i], width > other_width ? width - other_width : 0);
  }

  return values;
}

std::string fit_line(line_fields fields, size_t width) {
  auto parts = fit_fields(std::move(fields), width);
  std::string result;
  for (auto const& part : parts) {
    result += part;
  }
  return result;
}

std::string progress_bar(size_t width, double fraction, bool unicode) {
  // The last column is the right border, just as in the old progress display.
  auto const parts = unicode ? uni_bar : asc_bar;
  auto const n =
      static_cast<size_t>(8 * width * std::clamp(fraction, 0.0, 1.0));
  std::string result;

  for (size_t i = 0; i < width; ++i) {
    result += i == width - 1 ? parts[0]
              : i == n / 8   ? parts[n % 8]
              : i < n / 8    ? parts[7]
                             : " ";
  }

  return result;
}

std::vector<std::string>
summary_lines(internal::progress const& p, console_writer::options const& opts,
              std::optional<size_t> width, std::optional<size_t> memory_usage) {
  auto line = [&](line_fields fields) {
    return fit_line(std::move(fields),
                    width.value_or(std::numeric_limits<size_t>::max()));
  };
  std::vector<std::string> lines;
  if (opts.display == console_writer::NORMAL) {
    auto const links =
        fmt::format("{:L}/{:L}", p.symlinks_scanned.load(), p.hardlinks.load());

    lines.push_back(line({
        {fmt::format("{:L} dirs", p.dirs_scanned.load()), ""},
        {fmt::format(", {} soft/hard links", links),
         fmt::format(", {} links", links), 40},
        {fmt::format(", {:L}/{:L} files", p.files_scanned.load(),
                     p.files_found.load()),
         "", 90},
        {fmt::format(", {:L} other", p.specials_found.load()), "", 10},
    }));

    auto const allocated = p.allocated_original_size.load();
    auto const original = p.original_size.load();

    line_fields sizes{
        {fmt::format("original size: {}", size_with_unit(original)), ""}};

    if (opts.enable_sparse_files && allocated != original) {
      sizes.push_back(
          {fmt::format(" ({})", size_with_unit(allocated)), "", 40});
    }

    auto const hashed_size = size_with_unit(p.hash.bytes);

    sizes.push_back({fmt::format(", hashed: {}", hashed_size),
                     fmt::format(", hash: {}", hashed_size), 80});
    sizes.push_back({fmt::format(" ({:L} files, {}/s)", p.hash.scans.load(),
                                 size_with_unit(p.hash.bytes_per_sec)),
                     "", 20});
    lines.push_back(line(std::move(sizes)));

    auto const scan_rate = size_with_unit(p.similarity.bytes_per_sec);
    auto const categorize_rate = size_with_unit(p.categorize.bytes_per_sec);

    lines.push_back(line({
        {fmt::format("scanned: {}", size_with_unit(p.similarity.bytes)), ""},
        {fmt::format(" ({:L} files, {}/s)", p.similarity.scans.load(),
                     scan_rate),
         fmt::format(" ({}/s)", scan_rate), 60},
        {fmt::format(", categorizing: {}/s", categorize_rate),
         fmt::format(", cat: {}/s", categorize_rate), 20},
    }));

    auto const dedup = opts.enable_sparse_files
                           ? p.allocated_saved_by_deduplication.load()
                           : p.saved_by_deduplication.load();
    auto const dedup_size = size_with_unit(dedup);
    auto const segment_size = size_with_unit(p.saved_by_segmentation);

    lines.push_back(line({
        {fmt::format("saved by deduplication: {}", dedup_size),
         fmt::format("dedup: {}", dedup_size)},
        {fmt::format(" ({:L} files)", p.duplicate_files.load()), "", 40},
        {fmt::format(", saved by segmenting: {}", segment_size),
         fmt::format(", segments: {}", segment_size), 70},
    }));
  }

  auto const blocks = fmt::format("{:L}", p.block_count.load());

  line_fields filesystem{
      {fmt::format("filesystem: {}", size_with_unit(p.filesystem_size)), ""},
      {fmt::format(" in {} blocks", blocks), fmt::format(", {} blocks", blocks),
       80}};

  if (opts.display == console_writer::NORMAL) {
    auto const fragments_written = p.fragments_written.load();
    auto const fragments_found = p.fragments_found.load();
    auto const inode_count =
        p.files_found.load() - p.duplicate_files.load() - p.hardlinks.load();
    auto const inodes = fmt::format("{:L}", inode_count);

    filesystem.push_back(
        {fragments_written > 0
             ? fmt::format(" ({:L} chunks, {:L}/{:L} fragments, {} inodes)",
                           p.chunk_count.load(), fragments_written,
                           fragments_found, inodes)
             : fmt::format(" ({:L} chunks, {:L} fragments, {:L}/{} inodes)",
                           p.chunk_count.load(), fragments_found,
                           p.inodes_scanned.load(), inodes),
         fmt::format(" ({} inodes)", inodes), 30});
  } else {
    auto const inodes = fmt::format("{:L}", p.inodes_written.load());
    filesystem.push_back(
        {fmt::format(" ({:L} chunks, {} inodes)", p.chunk_count.load(), inodes),
         fmt::format(" ({} inodes)", inodes), 30});
  }

  lines.push_back(line(std::move(filesystem)));

  auto const compressed_size = size_with_unit(p.compressed_size);
  line_fields compressed;

  if (opts.display == console_writer::NORMAL) {
    compressed.push_back(
        {fmt::format("compressed filesystem: {:L} blocks/{} written",
                     p.blocks_written.load(), compressed_size),
         fmt::format("compressed: {}", compressed_size)});
  } else {
    compressed.push_back(
        {fmt::format("compressed filesystem: {:L}/{} blocks/{} written",
                     p.blocks_written.load(), blocks, compressed_size),
         fmt::format("compressed: {}", compressed_size)});
  }

  if (memory_usage) {
    auto const ram = size_with_unit(*memory_usage);
    compressed.push_back({fmt::format(", using {} of RAM", ram),
                          fmt::format(", RAM: {}", ram), 40});
  }

  lines.push_back(line(std::move(compressed)));

  return lines;
}

struct context_display {
  termcolor color;
  std::string label;
  std::string status;
  std::string path;
  std::string speed;
  std::optional<double> fraction;
};

context_display prepare_context(internal::progress::context& ctx) {
  auto st = ctx.get_status();
  context_display result{st.color,
                         printable(std::move(st.context)),
                         printable(std::move(st.status_string)),
                         {},
                         {},
                         std::nullopt};
  if (st.path) {
    result.path = printable(std::move(*st.path));
  }

  if (st.bytes_processed) {
    ctx.speed.put(*st.bytes_processed);
    result.speed =
        fmt::format("{}/s", size_with_unit(ctx.speed.num_per_second()));

    if (st.bytes_total && *st.bytes_total > 0) {
      result.fraction =
          static_cast<double>(*st.bytes_processed) / *st.bytes_total;
    }
  }

  return result;
}

std::optional<std::string>
context_line(terminal const& term, context_display const& ctx, size_t width,
             size_t bar_width, size_t speed_width, bool unicode, bool colored) {
  auto const text_width = width - (bar_width ? bar_width + 1 : 0) -
                          (speed_width ? speed_width + 1 : 0);

  line_fields fields{{ctx.label, ""}};

  if (!ctx.status.empty()) {
    fields.push_back({" " + ctx.status, "", 80});
  }

  if (!ctx.path.empty()) {
    auto const separator = ctx.status.empty() ? " " : ": ";
    std::string short_path = ctx.path;
    auto const prefix_width =
        display_width(ctx.label) +
        (ctx.status.empty() ? 0 : 1 + display_width(ctx.status)) +
        std::string_view(separator).size();

    shorten_path_string(
        short_path,
        static_cast<char>(std::filesystem::path::preferred_separator),
        text_width > prefix_width ? text_width - prefix_width : 0);

    fields.push_back({separator + ctx.path,
                      short_path.empty() ? "" : separator + short_path, 30});
  }

  if (fields.empty()) {
    return std::nullopt;
  }

  auto parts = fit_fields(std::move(fields), text_width);
  size_t used = 0;

  for (auto const& part : parts) {
    used += display_width(part);
  }

  std::string tail;

  for (size_t i = 1; i < parts.size(); ++i) {
    tail += parts[i];
  }

  tail.append(text_width - used, ' ');

  if (bar_width) {
    tail += ' ';
    tail += ctx.fraction ? progress_bar(bar_width, *ctx.fraction, unicode)
                         : std::string(bar_width, ' ');
  }

  if (speed_width) {
    tail += ' ';
    auto speed = ctx.speed;
    truncate_to(speed, speed_width);
    tail.append(speed_width - display_width(speed), ' ');
    tail += speed;
  }

  return term.colored(parts.front(), ctx.color, colored, termstyle::BOLD) +
         term.colored(std::move(tail), ctx.color, colored);
}

struct rendered_frame {
  std::string text;
  size_t lines{0};
};

rendered_frame
render_frame(internal::progress& p, console_writer::options const& opts,
             terminal const& term, bool colored, size_t width, double fraction,
             size_t tick, std::optional<size_t> memory_usage) {
  rendered_frame result;
  auto append = [&](std::string text) {
    result.text += std::move(text);
    result.text += '\n';
    ++result.lines;
  };
  auto const available = width - 1;

  // Horizontal rules and everything else leave the terminal's last column
  // untouched, so no frame can produce an automatic line wrap.
  std::string rule;
  for (size_t i = 0; i < available; ++i) {
    rule += opts.progress == console_writer::UNICODE ? "⎯" : "-";
  }

  append(std::move(rule));

  if (opts.display == console_writer::NORMAL) {
    append(term.colored(
        fit_line({{printable(p.status(available)), ""}}, available),
        termcolor::BOLD_CYAN, colored));
  }

  for (auto& line : summary_lines(p, opts, available, memory_usage)) {
    append(std::move(line));
  }

  auto const spinner = opts.progress == console_writer::UNICODE
                           ? uni_spinner[tick % uni_spinner.size()]
                           : asc_spinner[tick % asc_spinner.size()];
  auto const suffix = fmt::format("{:3.0f}% {}", 100 * fraction, spinner);
  auto const suffix_width = display_width(suffix);
  auto const bar_width =
      available > suffix_width ? available - suffix_width : 0;

  append(progress_bar(bar_width, fraction,
                      opts.progress == console_writer::UNICODE) +
         suffix);

  // Contexts are already sorted by priority. The cap prevents an excessive
  // number of context lines from taking over a small terminal.
  if (width >= 60) {
    auto contexts = p.get_active_contexts();
    std::vector<context_display> displays;

    for (size_t i = 0; i < std::min<size_t>(contexts.size(), 8); ++i) {
      displays.push_back(prepare_context(*contexts[i]));
    }

    bool const has_bar = std::ranges::any_of(
        displays, [](auto const& ctx) { return ctx.fraction.has_value(); });
    bool const has_speed = std::ranges::any_of(
        displays, [](auto const& ctx) { return !ctx.speed.empty(); });
    auto const bar_width = has_bar ? available / 4 : 0;
    size_t speed_width = 0;

    if (has_speed) {
      speed_width = 12;

      for (auto const& ctx : displays) {
        speed_width = std::max(speed_width, display_width(ctx.speed));
      }

      // Reserve a useful text column, even for unexpectedly long speed values.
      speed_width = std::min(speed_width, available / 3);
    }

    for (auto const& ctx : displays) {
      if (auto line =
              context_line(term, ctx, available, bar_width, speed_width,
                           opts.progress == console_writer::UNICODE, colored)) {
        append(std::move(*line));
      }
    }
  }

  return result;
}

std::string
plain_summary(internal::progress const& p, console_writer::options const& opts,
              std::optional<size_t> memory_usage) {
  auto lines = summary_lines(p, opts, std::nullopt, memory_usage);
  std::string result;

  for (auto const& line : lines) {
    result += line;
    result += '\n';
  }

  return result;
}

void update_chunk_size(internal::progress::scan_progress& sp) {
  if (auto const usec = sp.usec.load(); usec > 10'000) {
    auto const bytes_per_second = (sp.bytes.load() << 20) / usec;
    sp.chunk_size.store(std::min(
        UINT64_C(1) << 24,
        std::max(UINT64_C(1) << 15, std::bit_ceil(bytes_per_second / 32))));
    sp.bytes_per_sec.store(bytes_per_second);
  }
}

} // namespace

console_writer::console_writer(std::shared_ptr<terminal const> term,
                               std::ostream& os, os_access const& acc,
                               options const& opts,
                               logger_options const& logger_opts)
    : stream_logger{std::move(term), os, acc, logger_opts}
    , opts_{opts} {}

// Only rewind when the terminal has not resized since the frame was printed.
// This prevents previous log output from being accidentally overwritten.
void console_writer::forget_resized_frame(size_t width) {
  if (!frame_.empty() && width != frame_width_) {
    frame_lines_ = 0;
    frame_.clear();
  }
}

void console_writer::erase_frame(std::ostream& os) {
  if (frame_lines_ == 0) {
    return;
  }

  os << term().carriage_return();

  for (size_t i = 0; i < frame_lines_; ++i) {
    os << term().rewind_line() << term().clear_line();
  }
}

void console_writer::preamble(std::ostream& os) {
  forget_resized_frame(term().width());
  erase_frame(os);
}

void console_writer::postamble(std::ostream& os) {
  if (!frame_.empty()) {
    // A resize can also occur between the logger's preamble and postamble.
    forget_resized_frame(term().width());
    if (!frame_.empty()) {
      os << frame_;
    }
  }
}

std::string_view console_writer::get_newline() const {
  return opts_.progress == ASCII || opts_.progress == UNICODE ? "\x1b[K\n"
                                                              : "\n";
}

void console_writer::update(writer_progress& prog, bool last) {
  if (opts_.progress == NONE && !last) {
    return;
  }

  auto& p = prog.get_internal();
  update_chunk_size(p.hash);
  update_chunk_size(p.similarity);
  update_chunk_size(p.categorize);

  auto const memory_usage =
      mem_usage_ ? std::optional{mem_usage_()} : std::nullopt;
  if (opts_.progress == NONE) {
    if (INFO <= log_threshold()) {
      std::lock_guard lock(log_mutex());
      write_nolock(plain_summary(p, opts_, memory_usage));
    }
    return;
  }

  auto const original = opts_.enable_sparse_files
                            ? p.allocated_original_size.load()
                            : p.original_size.load();
  auto const dedup = opts_.enable_sparse_files
                         ? p.allocated_saved_by_deduplication.load()
                         : p.saved_by_deduplication.load();
  auto const used = original > dedup ? original - dedup : 0;
  auto const data_size =
      used > p.symlink_size.load() ? used - p.symlink_size.load() : 0;
  auto const frac_fs =
      data_size > 0 ? static_cast<double>(p.filesystem_size.load() +
                                          p.saved_by_segmentation.load()) /
                          data_size
                    : 0.0;
  auto const blocks = p.block_count.load();
  auto const frac_comp =
      blocks > 0 ? static_cast<double>(p.blocks_written.load()) / blocks : 0.0;
  auto const frac = last                      ? 1.0
                    : opts_.display == NORMAL ? (frac_fs + frac_comp) / 2.0
                                              : frac_comp;
  frac_ = std::clamp(std::max(frac, frac_), 0.0, 1.0);

  if (opts_.progress == SIMPLE) {
    auto const message =
        fmt::format(" ==> {0:.0f}% done, {1} blocks/{2} written", 100 * frac_,
                    p.blocks_written.load(), size_with_unit(p.compressed_size));

    std::lock_guard lock(log_mutex());

    if (message != last_simple_message_) {
      last_simple_message_ = message;
      write_nolock(fmt::format("- {}{}\n", get_current_time_string(), message));
    }

    if (last) {
      write_nolock(plain_summary(p, opts_, memory_usage));
    }

    return;
  }

  // Render without holding the logger mutex.
  // If the width changes while we render, drop this frame.
  auto const width = term().width();
  std::optional<rendered_frame> next;

  if (width >= kMinLiveWidth) {
    next = render_frame(p, opts_, term(), log_is_colored(), width, frac_,
                        counter_++, memory_usage);
  }

  std::lock_guard lock(log_mutex());
  auto const current_width = term().width();
  forget_resized_frame(current_width);

  if (width != current_width) {
    return;
  }

  if (!next) {
    if (last) {
      // When progress was disabled by a narrow terminal, still report the
      // final statistics once at the end of processing.
      write_nolock(plain_summary(p, opts_, memory_usage));
    }
    return;
  }

  std::ostringstream os;
  erase_frame(os);
  os << next->text;
  write_nolock(os.str());
  frame_ = std::move(next->text);
  frame_lines_ = next->lines;
  frame_width_ = width;

  if (last) {
    frame_.clear();
    frame_lines_ = 0;
  }
}

} // namespace dwarfs::writer

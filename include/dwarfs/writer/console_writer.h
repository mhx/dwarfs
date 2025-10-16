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

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>

#include <dwarfs/logger.h>

namespace dwarfs {

class terminal;

namespace writer {

class writer_progress;

class console_writer : public stream_logger {
 public:
  enum display_mode { NORMAL, REWRITE };
  enum progress_mode { NONE, SIMPLE, ASCII, UNICODE };
  using mem_usage_fn = std::function<size_t()>;

  struct options {
    progress_mode progress{SIMPLE};
    display_mode display{NORMAL};
    bool enable_sparse_files{false};
  };

  console_writer(std::shared_ptr<terminal const> term, std::ostream& os,
                 options const& opts, logger_options const& logger_opts = {});

  void update(writer_progress& prog, bool last);

  void set_memory_usage_function(mem_usage_fn func) {
    mem_usage_ = std::move(func);
  }

 private:
  void preamble(std::ostream& os) override;
  void postamble(std::ostream& os) override;
  std::string_view get_newline() const override;
  void rewind(std::ostream& os, int next_rewind_lines);

  std::string statebuf_;
  int rewind_lines_{0};
  double frac_{0.0};
  std::atomic<size_t> counter_{0};
  options const opts_;
  mem_usage_fn mem_usage_;
};

} // namespace writer

} // namespace dwarfs

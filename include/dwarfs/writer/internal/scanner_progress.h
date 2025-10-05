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
#include <string>

#include <dwarfs/terminal.h>
#include <dwarfs/types.h>

#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer::internal {

class scanner_progress : public progress::context {
 public:
  using status = progress::context::status;

  scanner_progress(std::string_view context, std::string file,
                   file_size_t size);
  scanner_progress(termcolor color, std::string_view context, std::string file,
                   file_size_t size);

  status get_status() const override;

  void advance(file_size_t bytes) noexcept { bytes_processed_ += bytes; }

 private:
  std::atomic<file_size_t> bytes_processed_{0};
  termcolor const color_;
  std::string const context_;
  std::string const file_;
  file_size_t const bytes_total_;
};

} // namespace dwarfs::writer::internal

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

#ifndef _WIN32

#include <chrono>
#include <memory>

#include <sys/types.h>

namespace dwarfs::test {

class process_guard {
 public:
  explicit process_guard(pid_t pid);
  ~process_guard();

  process_guard(process_guard const&) = delete;
  process_guard& operator=(process_guard const&) = delete;

  bool wait_for(std::chrono::milliseconds timeout);

 private:
  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::test

#endif

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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <folly/portability/Windows.h>
#include <folly/system/ThreadName.h>

#include <dwarfs/error.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer {

writer_progress::writer_progress()
    : prog_{std::make_unique<internal::progress>()} {}

writer_progress::writer_progress(update_function_type func)
    : writer_progress(std::move(func), std::chrono::seconds(1)) {}

writer_progress::writer_progress(update_function_type func,
                                 std::chrono::microseconds interval)
    : prog_{std::make_unique<internal::progress>()}
    , running_(true)
    , thread_([this, interval, func = std::move(func)]() mutable {
      folly::setThreadName("progress");
#ifdef _WIN32
      ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
      std::unique_lock lock(running_mx_);
      while (running_) {
        func(*this, false);
        cond_.wait_for(lock, interval);
      }
      func(*this, true);
    }) {
}

writer_progress::~writer_progress() noexcept {
  if (running_) {
    try {
      {
        std::lock_guard lock(running_mx_);
        running_ = false;
      }
      cond_.notify_all();
      thread_.join();
    } catch (...) {
      DWARFS_PANIC(
          fmt::format("exception thrown in writer_progress destructor: {}",
                      exception_str(std::current_exception())));
    }
  }
}

size_t writer_progress::errors() const { return prog_->errors; }

} // namespace dwarfs::writer

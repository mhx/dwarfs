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

#include <chrono>
#include <utility>

#include <folly/system/ThreadName.h>

#include "dwarfs/progress.h"

namespace dwarfs {

progress::progress(folly::Function<void(const progress&, bool)>&& func,
                   unsigned interval_ms)
    : running_(true)
    , thread_([this, interval_ms, func = std::move(func)]() mutable {
      folly::setThreadName("progress");
      std::unique_lock<std::mutex> lock(mx_);
      while (running_) {
        func(*this, false);
        cond_.wait_for(lock, std::chrono::milliseconds(interval_ms));
      }
      func(*this, true);
    }) {}

progress::~progress() noexcept {
  try {
    running_ = false;
    cond_.notify_all();
    thread_.join();
  } catch (...) {
  }
}

void progress::set_status_function(status_function_type status_fun) {
  std::unique_lock<std::mutex> lock(mx_);
  status_fun_ = std::move(status_fun);
}

std::string progress::status(size_t max_len) const {
  if (status_fun_) {
    return status_fun_(*this, max_len);
  }
  return std::string();
}

} // namespace dwarfs

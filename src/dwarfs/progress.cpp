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

#include <algorithm>
#include <chrono>
#include <utility>

#include <folly/portability/Windows.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/progress.h"

namespace dwarfs {

progress::progress(folly::Function<void(progress&, bool)>&& func,
                   unsigned interval_ms)
    : running_(true)
    , thread_([this, interval_ms, func = std::move(func)]() mutable {
      folly::setThreadName("progress");
#ifdef _WIN32
      ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
      std::unique_lock lock(running_mx_);
      while (running_) {
        func(*this, false);
        cond_.wait_for(lock, std::chrono::milliseconds(interval_ms));
      }
      func(*this, true);
    }) {
}

progress::~progress() noexcept {
  try {
    {
      std::lock_guard lock(running_mx_);
      running_ = false;
    }
    cond_.notify_all();
    thread_.join();
  } catch (...) {
  }
}

void progress::add_context(std::shared_ptr<context> const& ctx) const {
  std::lock_guard lock(mx_);
  contexts_.push_back(ctx);
}

auto progress::get_active_contexts() const
    -> std::vector<std::shared_ptr<context>> {
  std::vector<std::shared_ptr<context>> rv;

  rv.reserve(16);

  {
    std::lock_guard lock(mx_);

    contexts_.erase(std::remove_if(contexts_.begin(), contexts_.end(),
                                   [&rv](auto& wp) {
                                     if (auto sp = wp.lock()) {
                                       rv.push_back(std::move(sp));
                                       return false;
                                     }
                                     return true;
                                   }),
                    contexts_.end());
  }

  std::stable_sort(rv.begin(), rv.end(), [](const auto& a, const auto& b) {
    return a->get_priority() > b->get_priority();
  });

  return rv;
}

void progress::set_status_function(status_function_type status_fun) {
  std::lock_guard lock(mx_);
  status_fun_ = std::make_shared<status_function_type>(std::move(status_fun));
}

std::string progress::status(size_t max_len) {
  std::shared_ptr<status_function_type> fun;
  {
    std::lock_guard lock(mx_);
    fun = status_fun_;
  }
  if (fun) {
    return (*fun)(*this, max_len);
  }
  return std::string();
}

} // namespace dwarfs

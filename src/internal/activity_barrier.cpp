/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <dwarfs/internal/activity_barrier.h>

namespace dwarfs::internal {

auto activity_barrier::enter() -> token { return token{*this}; }

auto activity_barrier::begin_new_epoch() -> std::uint64_t {
  std::lock_guard lock(mx_);
  return epoch_++;
}

void activity_barrier::wait_for_older_activity(std::uint64_t epoch) {
  std::unique_lock lock(mx_);

  cv_.wait(lock, [&] {
    return std::ranges::none_of(active_, [&](auto const& p) {
      auto const& [active_epoch, count] = p;
      return active_epoch <= epoch && count != 0;
    });
  });
}

auto activity_barrier::enter_activity() -> std::uint64_t {
  std::lock_guard lock(mx_);

  auto const epoch = epoch_;
  ++active_[epoch];

  return epoch;
}

void activity_barrier::leave_activity(std::uint64_t epoch) {
  std::lock_guard lock(mx_);

  if (auto it = active_.find(epoch); it != active_.end()) {
    if (--it->second == 0) {
      active_.erase(it);
      cv_.notify_all();
    }
  }
}

} // namespace dwarfs::internal

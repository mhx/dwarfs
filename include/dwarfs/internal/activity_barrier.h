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

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace dwarfs::internal {

class activity_barrier {
 public:
  class token {
   public:
    explicit token(activity_barrier& owner)
        : owner_{&owner}
        , epoch_{owner_->enter_activity()} {}

    token(token const&) = delete;
    token& operator=(token const&) = delete;

    token(token&& other) noexcept
        : owner_{std::exchange(other.owner_, nullptr)}
        , epoch_{other.epoch_} {}

    token& operator=(token&& other) noexcept {
      if (this != &other) {
        reset();
        owner_ = std::exchange(other.owner_, nullptr);
        epoch_ = other.epoch_;
      }
      return *this;
    }

    ~token() { reset(); }

   private:
    void reset() {
      if (owner_) {
        owner_->leave_activity(epoch_);
        owner_ = nullptr;
      }
    }

    activity_barrier* owner_;
    std::uint64_t epoch_;
  };

  [[nodiscard]] token enter();
  std::uint64_t begin_new_epoch();
  void wait_for_older_activity(std::uint64_t epoch);

 private:
  std::uint64_t enter_activity();
  void leave_activity(std::uint64_t epoch);

  std::mutex mx_;
  std::condition_variable cv_;
  std::uint64_t epoch_ = 0;
  std::unordered_map<std::uint64_t, std::size_t> active_;
};

} // namespace dwarfs::internal

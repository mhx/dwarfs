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

#include <thread>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L

#include <stop_token>

namespace dwarfs::compat {

using jthread = std::jthread;
using stop_token = std::stop_token;

} // namespace dwarfs::compat

#else

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

namespace dwarfs::compat {

namespace detail {

struct stop_state {
  std::atomic<bool> requested{false};
};

} // namespace detail

class jthread;

class stop_token {
 public:
  stop_token() noexcept = default;

  [[nodiscard]] bool stop_requested() const noexcept {
    return state_ && state_->requested.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool stop_possible() const noexcept {
    return static_cast<bool>(state_);
  }

 private:
  explicit stop_token(std::shared_ptr<detail::stop_state> state) noexcept
      : state_(std::move(state)) {}

  std::shared_ptr<detail::stop_state const> state_;

  friend class jthread;
};

class jthread {
 public:
  using id = std::thread::id;
  using native_handle_type = std::thread::native_handle_type;

  jthread() noexcept = default;

  template <typename F, typename... Args>
    requires(!std::is_same_v<std::decay_t<F>, jthread>)
  explicit jthread(F&& f, Args&&... args)
      : state_{std::make_shared<detail::stop_state>()} {
    using Fn = std::decay_t<F>;

    // Match std::jthread: prefer invoking the callable with a stop_token
    // inserted as the first argument if that is a valid invocation.
    if constexpr (std::is_invocable_v<Fn, stop_token, std::decay_t<Args>...>) {
      thread_ = std::thread(std::forward<F>(f), stop_token{state_},
                            std::forward<Args>(args)...);
    } else {
      static_assert(
          std::is_invocable_v<Fn, std::decay_t<Args>...>,
          "jthread callable is not invocable with the supplied arguments");

      thread_ = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
    }
  }

  ~jthread() {
    if (joinable()) {
      request_stop();
      join();
    }
  }

  jthread(jthread const&) = delete;
  jthread& operator=(jthread const&) = delete;

  jthread(jthread&&) noexcept = default;

  jthread& operator=(jthread&& other) noexcept {
    if (this != &other) {
      if (joinable()) {
        request_stop();
        join();
      }

      state_ = std::move(other.state_);
      thread_ = std::move(other.thread_);
    }

    return *this;
  }

  [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }

  [[nodiscard]] id get_id() const noexcept { return thread_.get_id(); }

  [[nodiscard]] native_handle_type native_handle() {
    return thread_.native_handle();
  }

  void join() { thread_.join(); }

  void detach() { thread_.detach(); }

  [[nodiscard]] stop_token get_stop_token() const noexcept {
    return stop_token{state_};
  }

  bool request_stop() noexcept {
    if (!state_) {
      return false;
    }

    return !state_->requested.exchange(true, std::memory_order_acq_rel);
  }

  void swap(jthread& other) noexcept {
    thread_.swap(other.thread_);
    state_.swap(other.state_);
  }

 private:
  std::shared_ptr<detail::stop_state> state_;
  std::thread thread_;
};

inline void swap(jthread& lhs, jthread& rhs) noexcept { lhs.swap(rhs); }

} // namespace dwarfs::compat

#endif

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

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace dwarfs::internal {

namespace detail {

template <typename Mutex>
struct lock_policy;

template <>
struct lock_policy<std::mutex> {
  using write_lock_type = std::unique_lock<std::mutex>;
  using read_lock_type = std::unique_lock<std::mutex>;
  static constexpr bool is_shared = false;
};

template <>
struct lock_policy<std::shared_mutex> {
  using write_lock_type = std::unique_lock<std::shared_mutex>;
  using read_lock_type = std::shared_lock<std::shared_mutex>;
  static constexpr bool is_shared = true;
};

} // namespace detail

template <typename T, typename Mutex = std::mutex>
class synchronized final {
 public:
  using value_type = T;
  using mutex_type = Mutex;
  using lock_policy = detail::lock_policy<mutex_type>;
  using read_lock_type = lock_policy::read_lock_type;
  using write_lock_type = lock_policy::write_lock_type;
  static constexpr bool is_shared = lock_policy::is_shared;

  synchronized()
    requires std::is_default_constructible_v<value_type>
  = default;

  template <typename... Args>
  explicit synchronized(std::in_place_t, Args&&... args)
      : value_(std::forward<Args>(args)...) {}

  explicit synchronized(value_type value)
      : value_(std::move(value)) {}

  synchronized(synchronized const&) = delete;
  synchronized& operator=(synchronized const&) = delete;
  synchronized(synchronized&&) = delete;
  synchronized& operator=(synchronized&&) = delete;

  class write_locked_ptr {
   public:
    write_locked_ptr(write_lock_type lock, value_type* ptr) noexcept
        : lock_{std::move(lock)}
        , ptr_{ptr} {}

    value_type* operator->() const noexcept { return ptr_; }
    value_type& operator*() const noexcept { return *ptr_; }
    value_type* get() const noexcept { return ptr_; }

   private:
    write_lock_type lock_;
    value_type* ptr_;
  };

  class read_locked_ptr {
   public:
    read_locked_ptr(read_lock_type lock, value_type const* ptr) noexcept
        : lock_{std::move(lock)}
        , ptr_{ptr} {}

    value_type const* operator->() const noexcept { return ptr_; }
    value_type const& operator*() const noexcept { return *ptr_; }
    value_type const* get() const noexcept { return ptr_; }

   private:
    read_lock_type lock_;
    value_type const* ptr_;
  };

  [[nodiscard]] write_locked_ptr lock()
    requires(!is_shared)
  {
    return write_locked_ptr(write_lock_type(mx_), &value_);
  }

  [[nodiscard]] read_locked_ptr lock() const
    requires(!is_shared)
  {
    return read_locked_ptr(read_lock_type(mx_), &value_);
  }

  template <typename F>
  auto with_lock(F&& f)
    requires(!is_shared)
  {
    write_lock_type lock(mx_);
    return std::forward<F>(f)(value_);
  }

  template <typename F>
  auto with_lock(F&& f) const
    requires(!is_shared)
  {
    read_lock_type lock(mx_);
    return std::forward<F>(f)(value_);
  }

  [[nodiscard]] write_locked_ptr wlock()
    requires is_shared
  {
    return write_locked_ptr(write_lock_type(mx_), &value_);
  }

  [[nodiscard]] read_locked_ptr rlock() const
    requires is_shared
  {
    return read_locked_ptr(read_lock_type(mx_), &value_);
  }

  template <typename F>
  auto with_wlock(F&& f)
    requires is_shared
  {
    write_lock_type lock(mx_);
    return std::forward<F>(f)(value_);
  }

  template <typename F>
  auto with_rlock(F&& f) const
    requires is_shared
  {
    read_lock_type lock(mx_);
    return std::forward<F>(f)(value_);
  }

 private:
  mutex_type mutable mx_;
  value_type value_;
};

} // namespace dwarfs::internal

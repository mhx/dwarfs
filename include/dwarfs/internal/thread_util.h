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

#include <memory>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#else
#include <pthread.h>
#endif

namespace dwarfs::internal {

#ifdef _WIN32
DWORD std_to_win_thread_id(std::thread::id tid);
#else
pthread_t std_to_pthread_id(std::thread::id tid);
#endif

bool set_thread_name(std::string_view name);
void set_thread_niceness(int niceness);

class thread_helper {
 public:
  class impl;

  class scoped_background_thread {
   public:
    explicit scoped_background_thread(thread_helper::impl const* helper)
        : helper_{helper} {
      if (helper_) {
        helper_->enter_background();
      }
    }

    ~scoped_background_thread() noexcept {
      if (helper_) {
        helper_->leave_background();
      }
    }

    scoped_background_thread(scoped_background_thread const&) = delete;
    scoped_background_thread&
    operator=(scoped_background_thread const&) = delete;
    scoped_background_thread(scoped_background_thread&&) = delete;
    scoped_background_thread& operator=(scoped_background_thread&&) = delete;

   private:
    thread_helper::impl const* helper_{nullptr};
  };

  thread_helper();
  ~thread_helper() noexcept;

  thread_helper(thread_helper&&) = delete;
  thread_helper& operator=(thread_helper&&) = delete;

  scoped_background_thread background_scope(bool is_background) const {
    return scoped_background_thread{is_background ? impl_.get() : nullptr};
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual bool enter_background() const = 0;
    virtual bool leave_background() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::internal

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

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

namespace dwarfs::reader::internal {

class periodic_executor {
 public:
  periodic_executor(std::mutex& mx, std::chrono::nanoseconds period,
                    std::string_view name, std::function<void()> func);

  void start() const { impl_->start(); }

  void stop() const { impl_->stop(); }

  bool running() const { return impl_->running(); }

  void set_period(std::chrono::nanoseconds period) const {
    impl_->set_period(period);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void start() const = 0;
    virtual void stop() const = 0;
    virtual bool running() const = 0;
    virtual void set_period(std::chrono::nanoseconds period) const = 0;
  };

 private:
  std::unique_ptr<impl const> impl_;
};

} // namespace dwarfs::reader::internal

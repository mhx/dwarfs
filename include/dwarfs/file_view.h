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

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>

#include <dwarfs/types.h>

namespace dwarfs {

enum class advice {
  normal,
  random,
  sequential,
  willneed,
  dontneed,
};

class file_view {
 public:
  file_view() = default;

  explicit operator bool() const { return static_cast<bool>(impl_); }

  bool valid() const { return static_cast<bool>(impl_); }

  void reset() { impl_.reset(); }

  // TODO: this is mostly all deprecated
  template <typename T, std::integral U>
  T const* as(U offset = 0) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto raw = static_cast<std::byte const*>(this->addr()) + offset;
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    return static_cast<T const*>(static_cast<void const*>(raw));
  }

  template <typename T = uint8_t, std::integral U>
  std::span<T const> span(U offset, size_t length) const {
    return std::span(this->as<T>(offset), length);
  }

  template <typename T = uint8_t, std::integral U>
  std::span<T const> span(U offset) const {
    return span<T>(offset, size() - offset);
  }

  template <typename T = uint8_t>
  std::span<T const> span() const {
    return span<T>(0);
  }

  void const* addr() const { return impl_->addr(); }
  size_t size() const { return impl_->size(); }

  std::error_code lock(file_off_t offset, size_t size) const {
    return impl_->lock(offset, size);
  }

  std::error_code release(file_off_t offset, size_t size) const {
    return impl_->release(offset, size);
  }

  std::error_code release_until(file_off_t offset) const {
    return impl_->release_until(offset);
  }

  std::error_code advise(advice adv) const { return impl_->advise(adv); }

  std::error_code advise(advice adv, file_off_t offset, size_t size) const {
    return impl_->advise(adv, offset, size);
  }

  std::filesystem::path const& path() const { return impl_->path(); }

  class impl {
   public:
    virtual ~impl() = default;

    // TODO: this is mostly all deprecated
    virtual void const* addr() const = 0;
    virtual size_t size() const = 0;

    virtual std::error_code lock(file_off_t offset, size_t size) const = 0;
    virtual std::error_code release(file_off_t offset, size_t size) const = 0;
    virtual std::error_code release_until(file_off_t offset) const = 0;

    virtual std::error_code advise(advice adv) const = 0;
    virtual std::error_code
    advise(advice adv, file_off_t offset, size_t size) const = 0;

    virtual std::filesystem::path const& path() const = 0;
  };

  explicit file_view(std::shared_ptr<impl const> impl)
      : impl_{std::move(impl)} {}

 private:
  std::shared_ptr<impl const> impl_;
};

} // namespace dwarfs

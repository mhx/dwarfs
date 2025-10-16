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

#include <cstddef>
#include <limits>
#include <memory>
#include <span>

#include <dwarfs/file_range.h>
#include <dwarfs/io_advice.h>

namespace dwarfs {

namespace detail {

class memory_mapping_impl {
 public:
  virtual ~memory_mapping_impl() = default;

  virtual file_range range() const = 0;
  virtual std::span<std::byte> mutable_span() = 0;
  virtual std::span<std::byte const> const_span() const = 0;
  virtual void advise(io_advice advice, size_t offset, size_t size,
                      io_advice_range range, std::error_code* ec) const = 0;
  virtual void lock(size_t offset, size_t size, std::error_code* ec) const = 0;
};

} // namespace detail

class memory_mapping;

class readonly_memory_mapping {
 public:
  readonly_memory_mapping() = default;
  explicit readonly_memory_mapping(
      std::unique_ptr<detail::memory_mapping_impl> impl)
      : impl_(std::move(impl)) {}

  explicit operator bool() const noexcept { return static_cast<bool>(impl_); }
  bool valid() const noexcept { return static_cast<bool>(impl_); }
  void reset() noexcept { impl_.reset(); }

  file_range range() const { return impl_->range(); }

  size_t size() const { return impl_->const_span().size(); }

  std::span<std::byte const> const_span() const { return impl_->const_span(); }

  template <typename T>
    requires(sizeof(T) == 1)
  std::span<T const> const_span() const {
    auto const s = impl_->const_span();
    return {reinterpret_cast<T const*>(s.data()), s.size()};
  }

  void advise(io_advice advice) const {
    impl_->advise(advice, 0, std::numeric_limits<size_t>::max(),
                  io_advice_range::include_partial, nullptr);
  }

  void advise(io_advice advice, std::error_code& ec) const {
    impl_->advise(advice, 0, std::numeric_limits<size_t>::max(),
                  io_advice_range::include_partial, &ec);
  }

  void advise(io_advice advice, size_t offset, size_t size) const {
    impl_->advise(advice, offset, size, io_advice_range::include_partial,
                  nullptr);
  }

  void advise(io_advice advice, size_t offset, size_t size,
              io_advice_range range) const {
    impl_->advise(advice, offset, size, range, nullptr);
  }

  void advise(io_advice advice, size_t offset, size_t size,
              std::error_code& ec) const {
    impl_->advise(advice, offset, size, io_advice_range::include_partial, &ec);
  }

  void advise(io_advice advice, size_t offset, size_t size,
              io_advice_range range, std::error_code& ec) const {
    impl_->advise(advice, offset, size, range, &ec);
  }

  void lock() const {
    impl_->lock(0, std::numeric_limits<size_t>::max(), nullptr);
  }

  void lock(std::error_code& ec) const {
    impl_->lock(0, std::numeric_limits<size_t>::max(), &ec);
  }

  void lock(size_t offset, size_t size) const {
    impl_->lock(offset, size, nullptr);
  }

  void lock(size_t offset, size_t size, std::error_code& ec) const {
    impl_->lock(offset, size, &ec);
  }

 private:
  friend class memory_mapping;

  std::unique_ptr<detail::memory_mapping_impl> impl_;
};

class memory_mapping final : public readonly_memory_mapping {
 public:
  memory_mapping() = default;
  explicit memory_mapping(std::unique_ptr<detail::memory_mapping_impl> impl)
      : readonly_memory_mapping{std::move(impl)} {}

  std::span<std::byte const> span() const { return impl_->mutable_span(); }

  template <typename T>
    requires(sizeof(T) == 1)
  std::span<T> span() const {
    auto const s = impl_->mutable_span();
    return {reinterpret_cast<T*>(s.data()), s.size()};
  }
};

} // namespace dwarfs

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

#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <dwarfs/detail/file_segment_impl.h>

namespace dwarfs {

class file_segment {
 public:
  file_segment() = default;
  explicit file_segment(std::shared_ptr<detail::file_segment_impl const> p)
      : impl_{std::move(p)} {}

  explicit operator bool() const noexcept { return static_cast<bool>(impl_); }
  bool valid() const noexcept { return static_cast<bool>(impl_); }
  void reset() noexcept { impl_.reset(); }

  file_off_t offset() const noexcept { return impl_->offset(); }

  file_size_t size() const noexcept { return impl_->size(); }

  file_range range() const noexcept { return impl_->range(); }

  bool is_zero() const noexcept { return impl_->is_zero(); }

  std::span<std::byte const> span() const { return impl_->raw_bytes(); }

  std::span<std::byte const> span(file_off_t offset) const {
    return span().subspan(offset);
  }

  std::span<std::byte const> span(file_off_t offset, size_t size) const {
    return span().subspan(offset, size);
  }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> span() const {
    auto bytes = impl_->raw_bytes();
    return std::span<T const>{reinterpret_cast<T const*>(bytes.data()),
                              bytes.size()};
  }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> span(file_off_t offset) const {
    return span<T>().subspan(offset);
  }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> span(file_off_t offset, size_t size) const {
    return span<T>().subspan(offset, size);
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void copy_to(T& t, file_off_t offset, std::error_code& ec) const {
    auto const bytes = impl_->raw_bytes().subspan(offset, sizeof(T));
    if (bytes.size() == sizeof(T)) {
      std::memcpy(&t, bytes.data(), sizeof(T));
    } else {
      ec = make_error_code(std::errc::result_out_of_range);
    }
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void copy_to(T& t, file_off_t offset = 0) const {
    std::error_code ec;
    copy_to(t, offset, ec);
    if (ec) {
      throw std::system_error(ec);
    }
  }

  template <typename T>
    requires(std::is_trivially_copyable_v<T> &&
             std::is_default_constructible_v<T>)
  T read(file_off_t offset, std::error_code& ec) const {
    T t;
    this->copy_to(t, offset, ec);
    return t;
  }

  template <typename T>
    requires(std::is_trivially_copyable_v<T> &&
             std::is_default_constructible_v<T>)
  T read(file_off_t offset = 0) const {
    std::error_code ec;
    auto t = this->read<T>(offset, ec);
    if (ec) {
      throw std::system_error(ec);
    }
    return t;
  }

  void advise(io_advice adv, file_range range, std::error_code& ec) const {
    impl_->advise(adv, range, ec);
  }

  void advise(io_advice adv, file_range range) const {
    std::error_code ec;
    impl_->advise(adv, range, ec);
    if (ec) {
      throw std::system_error(ec);
    }
  }

  void advise(io_advice adv, std::error_code& ec) const {
    impl_->advise(adv, this->range(), ec);
  }

  void advise(io_advice adv) const {
    std::error_code ec;
    impl_->advise(adv, this->range(), ec);
    if (ec) {
      throw std::system_error(ec);
    }
  }

  void lock(std::error_code& ec) const { impl_->lock(ec); }

  void lock() const {
    std::error_code ec;
    impl_->lock(ec);
    if (ec) {
      throw std::system_error(ec);
    }
  }

 private:
  std::shared_ptr<detail::file_segment_impl const> impl_;
};

} // namespace dwarfs

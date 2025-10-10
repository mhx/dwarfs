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

#include <utility>

#include <dwarfs/detail/file_view_impl.h>
#include <dwarfs/file_extents_iterable.h>
#include <dwarfs/file_segments_iterable.h>

namespace dwarfs {

class file_view {
 public:
  file_view() = default;

  explicit file_view(std::shared_ptr<detail::file_view_impl const> impl)
      : impl_{std::move(impl)} {}

  explicit operator bool() const { return static_cast<bool>(impl_); }

  bool valid() const { return static_cast<bool>(impl_); }

  void reset() { impl_.reset(); }

  file_size_t size() const { return impl_->size(); }

  file_range range() const { return {0, impl_->size()}; }

  std::filesystem::path const& path() const { return impl_->path(); }

  file_segment segment_at(file_range range) const {
    return impl_->segment_at(range);
  }

  file_segment segment_at(file_off_t offset, file_size_t size) const {
    return impl_->segment_at({offset, size});
  }

  file_segments_iterable segments(file_range range, size_t max_segment_size = 0,
                                  size_t overlap_size = 0) const {
    return file_segments_iterable{impl_, range, max_segment_size, overlap_size};
  }

  file_extents_iterable extents() const { return impl_->extents(std::nullopt); }

  file_extents_iterable extents(file_range range) const {
    return impl_->extents(range);
  }

  bool supports_raw_bytes() const noexcept {
    return impl_->supports_raw_bytes();
  }

  std::span<std::byte const> raw_bytes() const { return impl_->raw_bytes(); }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> raw_bytes() const {
    auto bytes = impl_->raw_bytes();
    return std::span<T const>{reinterpret_cast<T const*>(bytes.data()),
                              bytes.size()};
  }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> raw_bytes(file_off_t offset) const {
    return raw_bytes<T>().subspan(offset);
  }

  template <typename T>
    requires(sizeof(T) == 1 && std::is_fundamental_v<T>)
  std::span<T const> raw_bytes(file_off_t offset, size_t size) const {
    return raw_bytes<T>().subspan(offset, size);
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void
  copy_to(T& t, file_off_t offset, file_size_t len, std::error_code& ec) const {
    if (std::cmp_less_equal(len, sizeof(T))) {
      impl_->copy_bytes(&t, file_range{offset, len}, ec);
    } else {
      ec = make_error_code(std::errc::result_out_of_range);
    }
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void copy_to(T& t, file_off_t offset, std::error_code& ec) const {
    copy_to(t, offset, sizeof(T), ec);
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void copy_to(T& t, file_off_t offset, file_size_t len) const {
    std::error_code ec;
    copy_to(t, offset, len, ec);
    if (ec) {
      throw std::system_error(ec);
    }
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void copy_to(T& t, file_off_t offset = 0) const {
    copy_to(t, offset, sizeof(T));
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

  std::string
  read_string(file_off_t offset, file_size_t len, std::error_code& ec) const {
    std::string s;
    if (len > 0) {
      s.resize(len);
      impl_->copy_bytes(s.data(), file_range{offset, len}, ec);
    }
    return s;
  }

  std::string read_string(file_off_t offset, file_size_t len) const {
    std::error_code ec;
    auto s = read_string(offset, len, ec);
    if (ec) {
      throw std::system_error(ec);
    }
    return s;
  }

  // TODO: maybe deprecate this
  void release_until(file_off_t offset, std::error_code& ec) const {
    impl_->release_until(offset, ec);
  }

 private:
  std::shared_ptr<detail::file_view_impl const> impl_;
};

} // namespace dwarfs

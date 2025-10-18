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

#include <fmt/format.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/io_ops.h>
#include <dwarfs/internal/io_ops_helpers.h>
#include <dwarfs/internal/read_file_view.h>

namespace dwarfs::internal {

namespace {

using namespace binary_literals;

std::any open_file(io_ops const& ops, std::filesystem::path const& path) {
  std::error_code ec;
  auto hdl = ops.open(path, ec);
  if (ec) {
    throw std::system_error(ec, "failed to open file: " +
                                    path_to_utf8_string_sanitized(path));
  }
  return hdl;
}

class read_file_view final
    : public detail::file_view_impl,
      public std::enable_shared_from_this<read_file_view> {
 public:
  read_file_view(io_ops const& ops, std::filesystem::path const& path)
      : handle_{open_file(ops, path)}
      , path_{path}
      , extents_{get_file_extents_noexcept(ops, handle_)}
      , ops_{ops} {}

  ~read_file_view() override {
    std::error_code ec;
    ops_.close(handle_, ec);
  }

  file_size_t size() const override {
    return extents_.empty() ? 0 : extents_.back().range.end();
  }

  file_segment segment_at(file_range range) const override;

  file_extents_iterable
  extents(std::optional<file_range> range) const override {
    if (!range.has_value()) {
      range.emplace(0, size());
    }
    return {shared_from_this(), extents_, *range};
  }

  bool supports_raw_bytes() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    DWARFS_PANIC("read_file_view does not support raw_bytes()");
  }

  void
  copy_bytes(void* dest, file_range range, std::error_code& ec) const override;

  size_t default_segment_size() const override { return 1_MiB; }

  void
  release_until(file_off_t /*offset*/, std::error_code& /*ec*/) const override {
    // not implemented for read_file_view
  }

  std::filesystem::path const& path() const override { return path_; }

 private:
  std::any handle_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
  io_ops const& ops_;
};

class read_file_segment final : public detail::file_segment_impl {
 public:
  read_file_segment(shared_byte_buffer buf, file_range range)
      : buf_{std::move(buf)}
      , range_{range} {}

  file_off_t offset() const noexcept override { return range_.offset(); }

  file_size_t size() const noexcept override { return range_.size(); }

  file_range range() const noexcept override { return range_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    auto const data = buf_.span();
    return std::span<std::byte const>(
        reinterpret_cast<std::byte const*>(data.data()), data.size());
  }

  void advise(io_advice /*adv*/, std::error_code& /*ec*/) const override {
    // not implemented
  }

  void lock(std::error_code& /*ec*/) const override {
    // not implemented
  }

 private:
  shared_byte_buffer buf_;
  file_range const range_;
};

file_segment read_file_view::segment_at(file_range range) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (offset < 0 || size == 0 ||
      std::cmp_greater(offset + size, this->size())) {
    return {};
  }

  auto buf = malloc_byte_buffer::create(size);

  std::error_code ec;

  copy_bytes(buf.data(), range, ec);

  if (ec) {
    throw std::system_error(
        ec,
        fmt::format("failed to read segment (offset {}, size {}) from file: {}",
                    offset, size, path_to_utf8_string_sanitized(path_)));
  }

  return file_segment(std::make_shared<read_file_segment>(buf.share(), range));
}

void read_file_view::copy_bytes(void* dest, file_range range,
                                std::error_code& ec) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (size == 0) {
    return;
  }

  if (dest == nullptr || offset < 0) {
    ec = make_error_code(std::errc::invalid_argument);
    return;
  }

  if (std::cmp_greater(offset + size, this->size())) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  auto const rv = ops_.pread(handle_, dest, size, offset, ec);

  if (!ec && std::cmp_not_equal(rv, size)) {
    ec = make_error_code(std::errc::io_error);
  }
}

} // namespace

file_view
create_read_file_view(io_ops const& ops, std::filesystem::path const& path) {
  return file_view(std::make_shared<read_file_view>(ops, path));
}

} // namespace dwarfs::internal

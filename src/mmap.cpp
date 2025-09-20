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

#include <cerrno>
#include <vector>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/mmap.h>
#include <dwarfs/scope_exit.h>

#include <dwarfs/internal/mappable_file.h>

namespace dwarfs {

namespace {

using namespace binary_literals;

class mmap_file_view final
    : public detail::file_view_impl,
      public std::enable_shared_from_this<mmap_file_view> {
 public:
  explicit mmap_file_view(std::filesystem::path const& path);
  mmap_file_view(std::filesystem::path const& path, file_size_t size);

  file_size_t size() const override;

  file_segment segment_at(file_range range) const override;

  file_extents_iterable extents(std::optional<file_range> range) const override;

  bool supports_raw_bytes() const noexcept override;

  std::span<std::byte const> raw_bytes() const override;

  void
  copy_bytes(void* dest, file_range range, std::error_code& ec) const override;

  size_t default_segment_size() const override { return 16_MiB; }

  void release_until(file_off_t offset, std::error_code& ec) const override;

  std::filesystem::path const& path() const override;

  // Not exposed publicly
  internal::readonly_memory_mapping const& mapping() const noexcept {
    return mapping_;
  }

 private:
  internal::mappable_file file_;
  internal::readonly_memory_mapping mapping_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
};

class mmap_ref_file_segment final : public detail::file_segment_impl {
 public:
  mmap_ref_file_segment(std::shared_ptr<mmap_file_view const> const& mm,
                        file_range range)
      : mm_{mm}
      , range_{range} {}

  file_off_t offset() const noexcept override { return range_.offset(); }

  file_size_t size() const noexcept override { return range_.size(); }

  file_range range() const noexcept override { return range_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return mm_->mapping().const_span().subspan(range_.offset(), range_.size());
  }

  void
  advise(io_advice adv, file_range range, std::error_code& ec) const override {
    mm_->mapping().advise(adv, range.offset(), range.size(), ec);
  }

  void lock(std::error_code& ec) const override { mm_->mapping().lock(ec); }

 private:
  std::shared_ptr<mmap_file_view const> mm_;
  file_range const range_;
};

file_segment mmap_file_view::segment_at(file_range range) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (offset < 0 || size == 0 ||
      std::cmp_greater(offset + size, mapping_.size())) {
    return {};
  }

  return file_segment(
      std::make_shared<mmap_ref_file_segment>(shared_from_this(), range));
}

file_extents_iterable
mmap_file_view::extents(std::optional<file_range> range) const {
  if (!range.has_value()) {
    range.emplace(0, size());
  }
  return {shared_from_this(), extents_, *range};
}

bool mmap_file_view::supports_raw_bytes() const noexcept { return true; }

std::span<std::byte const> mmap_file_view::raw_bytes() const {
  return mapping_.const_span();
}

void mmap_file_view::copy_bytes(void* dest, file_range range,
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

  if (std::cmp_greater(offset + size, mapping_.size())) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  std::memcpy(dest, mapping_.const_span().data() + offset, size);
}

void mmap_file_view::release_until(file_off_t offset,
                                   std::error_code& ec) const {
  mapping_.advise(io_advice::dontneed, 0, offset, ec);
}

file_size_t mmap_file_view::size() const { return mapping_.size(); }

std::filesystem::path const& mmap_file_view::path() const { return path_; }

mmap_file_view::mmap_file_view(std::filesystem::path const& path)
    : file_{internal::mappable_file::create(path)}
    , mapping_{file_.map_readonly()}
    , path_{path}
    , extents_{file_.get_extents_noexcept()} {}

mmap_file_view::mmap_file_view(std::filesystem::path const& path,
                               file_size_t size)
    : file_{internal::mappable_file::create(path)}
    , mapping_{file_.map_readonly(0, size)}
    , path_{path}
    , extents_{file_.get_extents_noexcept()} {}

} // namespace

file_view create_mmap_file_view(std::filesystem::path const& path) {
  return file_view(std::make_shared<mmap_file_view>(path));
}

file_view
create_mmap_file_view(std::filesystem::path const& path, file_size_t size) {
  return file_view(std::make_shared<mmap_file_view>(path, size));
}

} // namespace dwarfs

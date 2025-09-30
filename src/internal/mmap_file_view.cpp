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

#include <dwarfs/binary_literals.h>

#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/internal/mmap_file_view.h>

namespace dwarfs::internal {

namespace {

using namespace binary_literals;

class mmap_file_view final
    : public detail::file_view_impl,
      public std::enable_shared_from_this<mmap_file_view> {
 public:
  mmap_file_view(internal::memory_mapping_ops const& ops,
                 std::filesystem::path const& path,
                 mmap_file_view_options const& opts)
      : file_{internal::mappable_file::create(ops, path)}
      , path_{path}
      , extents_{file_.get_extents_noexcept()} {
    if (!opts.max_eager_map_size.has_value() ||
        file_.size() <= opts.max_eager_map_size.value()) {
      mapping_.emplace(file_.map_readonly());
    }
  }

  file_size_t size() const override { return file_.size(); }

  file_segment segment_at(file_range range) const override;

  file_extents_iterable
  extents(std::optional<file_range> range) const override {
    if (!range.has_value()) {
      range.emplace(0, size());
    }
    return {shared_from_this(), extents_, *range};
  }

  bool supports_raw_bytes() const noexcept override {
    return mapping_.has_value();
  }

  std::span<std::byte const> raw_bytes() const override {
    return mapping_.value().const_span();
  }

  void
  copy_bytes(void* dest, file_range range, std::error_code& ec) const override;

  size_t default_segment_size() const override { return 16_MiB; }

  void release_until(file_off_t offset, std::error_code& ec) const override {
    if (mapping_.has_value()) {
      mapping_->advise(io_advice::dontneed, 0, offset, ec);
    }
  }

  std::filesystem::path const& path() const override { return path_; }

  // Not exposed publicly
  readonly_memory_mapping const& mapping() const { return mapping_.value(); }

 private:
  internal::mappable_file file_;
  std::optional<readonly_memory_mapping> mapping_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
};

class mmap_ref_file_segment final : public detail::file_segment_impl {
 public:
  mmap_ref_file_segment(std::shared_ptr<mmap_file_view const> const& mm,
                        file_range range)
      : mm_{mm}
      , range_{range} {}

  ~mmap_ref_file_segment() override {
    std::error_code ec;
    mm_->mapping().advise(io_advice::dontneed, range_.offset(), range_.size(),
                          ec);
  }

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

class mmap_file_segment final : public detail::file_segment_impl {
 public:
  mmap_file_segment(readonly_memory_mapping&& mm, file_range range)
      : mm_{std::move(mm)}
      , range_{range} {}

  file_off_t offset() const noexcept override { return range_.offset(); }

  file_size_t size() const noexcept override { return range_.size(); }

  file_range range() const noexcept override { return range_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return mm_.const_span();
  }

  void advise(io_advice /*adv*/, file_range /*range*/,
              std::error_code& /*ec*/) const override {
    // mm_->mapping().advise(adv, range.offset(), range.size(), ec);
  }

  void lock(std::error_code& /*ec*/) const override {
    // mm_->mapping().lock(ec);
  }

 private:
  readonly_memory_mapping mm_;
  file_range const range_;
};

file_segment mmap_file_view::segment_at(file_range range) const {
  auto const offset = range.offset();
  auto const size = range.size();

  if (offset < 0 || size == 0 ||
      std::cmp_greater(offset + size, file_.size())) {
    return {};
  }

  if (mapping_.has_value()) {
    return file_segment(
        std::make_shared<mmap_ref_file_segment>(shared_from_this(), range));
  }

  return file_segment(std::make_shared<mmap_file_segment>(
      file_.map_readonly(range.offset(), range.size()), range));
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

  if (std::cmp_greater(offset + size, file_.size())) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  if (mapping_.has_value()) {
    std::memcpy(dest, mapping_->const_span().data() + offset, size);
  } else {
    file_.read(dest, offset, size, ec);
  }
}

} // namespace

file_view create_mmap_file_view(internal::memory_mapping_ops const& ops,
                                std::filesystem::path const& path,
                                mmap_file_view_options const& opts) {
  return file_view(std::make_shared<mmap_file_view>(ops, path, opts));
}

file_view create_mmap_file_view(internal::memory_mapping_ops const& ops,
                                std::filesystem::path const& path) {
  return create_mmap_file_view(ops, path, {});
}

} // namespace dwarfs::internal

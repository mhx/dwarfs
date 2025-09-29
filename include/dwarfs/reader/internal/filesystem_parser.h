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

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/types.h>

#include <dwarfs/internal/fs_section.h>

namespace dwarfs {

class logger;

namespace reader::internal {

class filesystem_parser {
 public:
  filesystem_parser(
      logger& lgr, file_view const& mm, file_off_t image_offset = 0,
      file_off_t image_size = std::numeric_limits<file_off_t>::max());

  void rewind() { impl_->rewind(); }

  std::optional<dwarfs::internal::fs_section> next_section() {
    return impl_->next_section();
  }

  std::optional<file_extents_iterable> header() const {
    return impl_->header();
  }

  std::string version() const { return impl_->version(); }

  int header_version() const { return impl_->header_version(); }

  filesystem_version const& fs_version() const { return impl_->fs_version(); }
  int major_version() const { return impl_->fs_version().major; }
  int minor_version() const { return impl_->fs_version().minor; }

  file_off_t image_offset() const { return impl_->image_offset(); }

  bool has_checksums() const { return impl_->has_checksums(); }
  bool has_index() const { return impl_->has_index(); }

  size_t filesystem_size() const { return impl_->filesystem_size(); }

  file_segment segment(dwarfs::internal::fs_section const& s) const {
    return impl_->segment(s);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void rewind() = 0;
    virtual std::optional<dwarfs::internal::fs_section> next_section() = 0;

    virtual std::optional<file_extents_iterable> header() const = 0;

    virtual std::string version() const = 0;

    virtual int header_version() const = 0;
    virtual filesystem_version const& fs_version() const = 0;

    virtual file_off_t image_offset() const = 0;

    virtual bool has_checksums() const = 0;
    virtual bool has_index() const = 0;

    virtual size_t filesystem_size() const = 0;

    virtual file_segment
    segment(dwarfs::internal::fs_section const& s) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace reader::internal
} // namespace dwarfs

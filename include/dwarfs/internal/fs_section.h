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
#include <optional>
#include <span>
#include <string>

#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/types.h>

namespace dwarfs::internal {

class fs_section {
 public:
  fs_section(file_view const& mm, file_off_t offset, int version);
  fs_section(file_view const& mm, section_type type, file_off_t offset,
             size_t size, int version);

  file_off_t start() const { return impl_->start(); }
  size_t length() const { return impl_->length(); }
  bool is_known_compression() const { return impl_->is_known_compression(); }
  bool is_known_type() const { return impl_->is_known_type(); }
  compression_type compression() const { return impl_->compression(); }
  std::string compression_name() const {
    return get_compression_name(compression());
  }
  section_type type() const { return impl_->type(); }
  std::string name() const { return impl_->name(); }
  std::string description() const { return impl_->description(); }

  bool check_fast_mm(file_view const& mm) const {
    return impl_->check_fast_mm(mm);
  }

  bool check_fast(file_segment const& seg) const {
    return impl_->check_fast(seg);
  }

  file_segment segment(file_view const& mm) const { return impl_->segment(mm); }

  std::span<uint8_t const> data(file_segment const& seg) const {
    return impl_->data(seg);
  }

  std::optional<std::span<uint8_t const>>
  checksum_span(file_segment const& seg) const {
    return impl_->checksum_span(seg);
  }

  std::optional<std::span<uint8_t const>>
  integrity_span(file_segment const& seg) const {
    return impl_->integrity_span(seg);
  }

  std::span<uint8_t const> raw_bytes(file_view const& mm) const {
    return impl_->raw_bytes(mm);
  }

  file_off_t end() const { return start() + length(); }

  std::optional<uint32_t> section_number() const {
    return impl_->section_number();
  }

  std::optional<uint64_t> xxh3_64_value() const {
    return impl_->xxh3_64_value();
  }

  std::optional<std::span<uint8_t const>> sha2_512_256_value() const {
    return impl_->sha2_512_256_value();
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual file_off_t start() const = 0;
    virtual size_t length() const = 0;
    virtual bool is_known_compression() const = 0;
    virtual bool is_known_type() const = 0;
    virtual compression_type compression() const = 0;
    virtual section_type type() const = 0;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual bool check_fast_mm(file_view const& mm) const = 0;
    virtual bool check_fast(file_segment const& seg) const = 0;
    virtual file_segment segment(file_view const& mm) const = 0;
    virtual std::span<uint8_t const> data(file_segment const& seg) const = 0;
    virtual std::optional<std::span<uint8_t const>>
    checksum_span(file_segment const& seg) const = 0;
    virtual std::optional<std::span<uint8_t const>>
    integrity_span(file_segment const& seg) const = 0;
    virtual std::span<uint8_t const> raw_bytes(file_view const& mm) const = 0;
    virtual std::optional<uint32_t> section_number() const = 0;
    virtual std::optional<uint64_t> xxh3_64_value() const = 0;
    virtual std::optional<std::span<uint8_t const>>
    sha2_512_256_value() const = 0;
  };

 private:
  std::shared_ptr<impl const> impl_;
};

} // namespace dwarfs::internal

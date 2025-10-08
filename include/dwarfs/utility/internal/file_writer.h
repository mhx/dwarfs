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

#include <any>
#include <filesystem>
#include <memory>
#include <system_error>

#include <dwarfs/types.h>

#include <dwarfs/utility/internal/diagnostic_sink.h>

namespace dwarfs::utility::internal {

class file_writer {
 public:
  static file_writer create_native(std::filesystem::path const& path,
                                   diagnostic_sink& ds, std::error_code& ec);

  static file_writer
  create_native_temp(std::filesystem::path const& dir, diagnostic_sink& ds,
                     std::error_code& ec);

  file_writer() = default;

  void set_sparse(std::error_code& ec) { impl_->set_sparse(ec); }

  void truncate(file_size_t size, std::error_code& ec) {
    impl_->truncate(size, ec);
  }

  void write_data(file_off_t offset, void const* buffer, size_t count,
                  std::error_code& ec) {
    impl_->write_data(offset, buffer, count, ec);
  }

  void write_hole(file_off_t offset, file_size_t length, std::error_code& ec) {
    impl_->write_hole(offset, length, ec);
  }

  void commit(std::error_code& ec) { impl_->commit(ec); }

  std::any get_native_handle() const { return impl_->get_native_handle(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_sparse(std::error_code& ec) = 0;
    virtual void truncate(file_size_t size, std::error_code& ec) = 0;
    virtual void write_data(file_off_t offset, void const* buffer, size_t count,
                            std::error_code& ec) = 0;
    virtual void
    write_hole(file_off_t offset, file_size_t length, std::error_code& ec) = 0;
    virtual void commit(std::error_code& ec) = 0;
    virtual std::any get_native_handle() const = 0;
  };

  file_writer(std::unique_ptr<impl>&& impl)
      : impl_{std::move(impl)} {}

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::utility::internal

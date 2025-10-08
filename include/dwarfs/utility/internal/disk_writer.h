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

#include <filesystem>
#include <memory>
#include <system_error>

#include <dwarfs/file_stat.h>

#include <dwarfs/utility/internal/file_writer.h>

namespace dwarfs::utility::internal {

class disk_writer {
 public:
  // TODO: options, e.g. overwrite existing files, etc.

  static disk_writer
  create_native(std::filesystem::path const& base, diagnostic_sink& ds);

  disk_writer() = default;

  // can be a directory, device, fifo, socket
  void create_entry(std::filesystem::path const& path, file_stat const& stat,
                    std::error_code& ec) {
    impl_->create_entry(path, stat, ec);
  }

  void
  create_symlink(std::filesystem::path const& path, file_stat const& stat,
                 std::filesystem::path const& target, std::error_code& ec) {
    impl_->create_symlink(path, stat, target, ec);
  }

  std::optional<file_writer>
  create_file(std::filesystem::path const& path, file_stat const& stat,
              std::error_code& ec) {
    return impl_->create_file(path, stat, ec);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void create_entry(std::filesystem::path const& path,
                              file_stat const& stat, std::error_code& ec) = 0;
    virtual void
    create_symlink(std::filesystem::path const& path, file_stat const& stat,
                   std::filesystem::path const& target,
                   std::error_code& ec) = 0;
    virtual std::optional<file_writer>
    create_file(std::filesystem::path const& path, file_stat const& stat,
                std::error_code& ec) = 0;
  };

  explicit disk_writer(std::unique_ptr<impl>&& impl)
      : impl_{std::move(impl)} {}

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::utility::internal

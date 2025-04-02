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

#include <boost/iostreams/device/mapped_file.hpp>

#include <dwarfs/mmif.h>

namespace dwarfs {

class mmap : public mmif {
 public:
  explicit mmap(std::filesystem::path const& path);
  mmap(std::filesystem::path const& path, size_t size);

  void const* addr() const override;
  size_t size() const override;

  std::error_code lock(file_off_t offset, size_t size) override;
  std::error_code release(file_off_t offset, size_t size) override;
  std::error_code release_until(file_off_t offset) override;

  std::error_code advise(advice adv) override;
  std::error_code advise(advice adv, file_off_t offset, size_t size) override;

  std::filesystem::path const& path() const override;

 private:
  boost::iostreams::mapped_file mutable mf_;
  uint64_t const page_size_;
  std::filesystem::path const path_;
};
} // namespace dwarfs

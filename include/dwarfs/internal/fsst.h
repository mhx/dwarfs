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
#include <string_view>
#include <vector>

namespace dwarfs::internal {

class fsst_encoder {
 public:
  struct bulk_compression_result {
    std::string dictionary;
    std::string buffer;
    std::vector<std::string_view> compressed_data;
  };

  static std::optional<bulk_compression_result>
  compress(std::span<std::string_view const> data, bool force = false);
  static std::optional<bulk_compression_result>
  compress(std::span<std::string const> data, bool force = false);
};

class fsst_decoder {
 public:
  explicit fsst_decoder(std::string_view dictionary);

  std::string decompress(std::string_view data) const {
    return impl_->decompress(data);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::string decompress(std::string_view data) const = 0;
  };

 private:
  std::unique_ptr<impl const> impl_;
};

} // namespace dwarfs::internal

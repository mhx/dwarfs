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

#include <concepts>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/container/packed_int_vector.h>

namespace dwarfs::internal {

namespace detail {

template <typename T>
concept fsst_string_type =
    std::same_as<T, std::string_view> || std::same_as<T, std::string> ||
    std::same_as<T, std::u8string_view> || std::same_as<T, std::u8string>;

} // namespace detail

class fsst_encoder {
 public:
  using compressed_sizes_type =
      dwarfs::container::packed_int_vector<std::size_t>;

  struct bulk_compression_result {
    std::string dictionary;
    std::string buffer;
    compressed_sizes_type compressed_sizes;
  };

  static std::optional<bulk_compression_result>
  compress(std::span<std::string_view const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::string const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::u8string_view const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::u8string const> data, bool force = false);

  template <std::forward_iterator It>
    requires detail::fsst_string_type<std::iter_value_t<It>> &&
             std::sized_sentinel_for<It, It>
  static std::optional<bulk_compression_result>
  compress(It const begin, It const end, bool const force = false) {
    if (begin == end) {
      return std::nullopt;
    }

    std::size_t total_input_size = 0;
    std::vector<std::size_t> len_vec;
    std::vector<unsigned char const*> ptr_vec;

    auto const size = std::distance(begin, end);
    len_vec.reserve(size);
    ptr_vec.reserve(size);

    for (auto it = begin; it != end; ++it) {
      auto&& s = *it;
      ptr_vec.emplace_back(reinterpret_cast<unsigned char const*>(s.data()));
      len_vec.emplace_back(s.size());
      total_input_size += s.size();
    }

    return compress(ptr_vec, len_vec, total_input_size, force);
  }

  template <typename Container>
    requires detail::fsst_string_type<typename Container::value_type>
  static std::optional<bulk_compression_result>
  compress(Container const& data, bool force = false) {
    return compress(std::begin(data), std::end(data), force);
  }

 private:
  static std::optional<bulk_compression_result>
  compress(std::span<unsigned char const*> ptr_span,
           std::span<std::size_t const> len_span, std::size_t total_input_size,
           bool force);
};

class fsst_decoder {
 public:
  explicit fsst_decoder(std::string_view dictionary);

  void decompress_append_to(std::string& out, std::string_view data) const {
    impl_->decompress_append_to(out, data);
  }

  void decompress_append_to(std::u8string& out, std::u8string_view data) const {
    impl_->decompress_append_to_u8(out, data);
  }

  std::string decompress(std::string_view data) const {
    std::string r;
    decompress_append_to(r, data);
    return r;
  }

  std::u8string decompress(std::u8string_view data) const {
    std::u8string r;
    decompress_append_to(r, data);
    return r;
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    decompress_append_to(std::string& out, std::string_view data) const = 0;
    virtual void decompress_append_to_u8(std::u8string& out,
                                         std::u8string_view data) const = 0;
  };

 private:
  std::unique_ptr<impl const> impl_;
};

} // namespace dwarfs::internal

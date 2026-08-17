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
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
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

class fsst_incremental_compressor_impl;

} // namespace detail

/**
 * Type-erased, random-access view of a batch of strings.
 *
 * Lets the FSST code read input that isn't stored as contiguous pointer and
 * length arrays. All supported string types have single-byte characters, so
 * element lengths are byte lengths.
 *
 * This is a non-owning view. The container and the strings it holds must
 * outlive every call the view is passed to. The view itself does not need to
 * outlive such a call.
 */
class fsst_string_source {
 public:
  using length_function = std::size_t (*)(void const*, std::size_t);
  using data_function = unsigned char const* (*)(void const*, std::size_t);

  // Build a view over any container of strings with `size()` and `operator[]`.
  template <typename Container>
    requires detail::fsst_string_type<typename Container::value_type>
  explicit fsst_string_source(Container const& container)
      : context_{std::addressof(container)}
      , size_{container.size()}
      , length_{[](void const* ctx, std::size_t i) -> std::size_t {
        return static_cast<Container const*>(ctx)->operator[](i).size();
      }}
      , data_{[](void const* ctx, std::size_t i) -> unsigned char const* {
        return reinterpret_cast<unsigned char const*>(
            static_cast<Container const*>(ctx)->operator[](i).data());
      }} {}

  // Don't allow construction from a temporary container.
  template <typename Container>
    requires detail::fsst_string_type<typename Container::value_type> &&
                 (!std::ranges::borrowed_range<Container>)
  explicit fsst_string_source(Container&&) = delete;

  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size_ == 0; }

  [[nodiscard]] std::size_t length(std::size_t index) const {
    return length_(context_, index);
  }

  [[nodiscard]] unsigned char const* data(std::size_t index) const {
    return data_(context_, index);
  }

  // Total byte length of all strings.
  [[nodiscard]] std::size_t total_length() const;

 private:
  void const* context_;
  std::size_t size_;
  length_function length_;
  data_function data_;
};

class fsst_encoder {
 public:
  // The uint32_t is used for compatibility with the Thrift type.
  using index_type = dwarfs::container::packed_int_vector<std::uint32_t>;

  struct bulk_compression_result {
    std::string dictionary;
    std::string buffer;
    index_type positions;
  };

  static std::optional<bulk_compression_result>
  compress(std::span<std::string_view const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::string const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::u8string_view const> data, bool force = false);

  static std::optional<bulk_compression_result>
  compress(std::span<std::u8string const> data, bool force = false);

  template <typename Container>
    requires detail::fsst_string_type<typename Container::value_type>
  static std::optional<bulk_compression_result>
  compress(Container const& data, bool force = false) {
    return compress(fsst_string_source{data}, force);
  }

 private:
  static std::optional<bulk_compression_result>
  compress(fsst_string_source const& source, bool force);

  static std::optional<bulk_compression_result>
  compress(std::span<unsigned char const*> ptr_span,
           std::span<std::size_t const> len_span, std::size_t total_input_size,
           bool force);
};

/**
 * Incremental FSST bulk compressor.
 *
 * Compresses a collection of strings in batches. Used like this:
 *
 *   1. `create()` builds the symbol table. It has to see the whole input, and
 *      the input must stay alive for the duration of that call.
 *
 *   2. `add()` compresses one span of strings at a time. Each span only needs
 *      to be alive for its own call, so the caller may release it afterwards.
 *      The spans must be passed in order, and together must cover exactly the
 *      input that `create()` saw.
 *
 *   3. `finish()` yields the result.
 *
 * Unlike `fsst_encoder::compress()`, this does not decide for itself whether
 * compressing is worthwhile, as by the time that could be known, the source
 * may already be gone. Use `estimated_compressed_size()` with a representative
 * sample before starting, and don't begin releasing source data until the
 * decision has been made.
 */
template <detail::fsst_string_type StringType>
class fsst_incremental_compressor {
 public:
  using string_type = StringType;
  using result_type = fsst_encoder::bulk_compression_result;

  fsst_incremental_compressor(fsst_incremental_compressor&&) noexcept;
  fsst_incremental_compressor&
  operator=(fsst_incremental_compressor&&) noexcept;
  ~fsst_incremental_compressor();

  /**
   * Build a symbol table for `source`, which must cover the whole input.
   */
  static fsst_incremental_compressor create(fsst_string_source const& source);

  template <typename Container>
    requires std::same_as<typename Container::value_type, StringType>
  static fsst_incremental_compressor create(Container const& source) {
    return create(fsst_string_source{source});
  }

  /**
   * Compress `sample` into scratch space and return the resulting size,
   * without adding it to the result. Together with `dictionary_size()`, this
   * is enough to estimate whether compressing the whole input will pay off.
   */
  [[nodiscard]] std::size_t
  estimated_compressed_size(fsst_string_source const& sample) const;

  [[nodiscard]] std::size_t
  estimated_compressed_size(std::span<StringType const> sample) const {
    return estimated_compressed_size(fsst_string_source{sample});
  }

  void add(fsst_string_source const& strings);

  void add(std::span<StringType const> strings) {
    add(fsst_string_source{strings});
  }

  [[nodiscard]] std::size_t dictionary_size() const;
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] std::size_t input_size() const;
  [[nodiscard]] std::size_t compressed_size() const;

  [[nodiscard]] result_type finish() &&;

 private:
  explicit fsst_incremental_compressor(
      std::unique_ptr<detail::fsst_incremental_compressor_impl> impl);

  std::unique_ptr<detail::fsst_incremental_compressor_impl> impl_;
};

extern template class fsst_incremental_compressor<std::string>;
extern template class fsst_incremental_compressor<std::string_view>;
extern template class fsst_incremental_compressor<std::u8string>;
extern template class fsst_incremental_compressor<std::u8string_view>;

class fsst_decoder {
 public:
  static bool is_valid_dictionary(std::string_view dictionary);
  static bool is_valid_compressed_string(std::string_view str);

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

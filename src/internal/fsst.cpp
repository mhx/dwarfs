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

#include <cassert>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <dwarfs/internal/fsst.h>

#include <fmt/format.h>

#include <fsst.h>

namespace dwarfs::internal {

namespace {

std::optional<fsst_encoder::bulk_compression_result>
fsst_compress_(std::span<unsigned char const*> ptr_span,
               std::span<std::size_t const> len_span,
               std::size_t const total_input_size, bool force) {
  std::optional<fsst_encoder::bulk_compression_result> output;

  auto const size = ptr_span.size();
  assert(size == len_span.size());

  std::unique_ptr<::fsst_encoder_t, decltype(&::fsst_destroy)> enc{
      ::fsst_create(size, len_span.data(), ptr_span.data(), 0),
      &::fsst_destroy};

  std::string symtab;

  symtab.resize(sizeof(::fsst_decoder_t));

  auto const symtab_size =
      ::fsst_export(enc.get(), reinterpret_cast<unsigned char*>(symtab.data()));
  symtab.resize(symtab_size);

  std::vector<std::size_t> out_len_vec;
  std::vector<unsigned char*> out_ptr_vec;
  std::string buffer;

  out_len_vec.resize(size);
  out_ptr_vec.resize(size);

  if (symtab_size >= total_input_size && !force) {
    return output;
  }

  buffer.resize(total_input_size);

  for (;;) {
    auto const num_compressed = ::fsst_compress(
        enc.get(), size, len_span.data(), ptr_span.data(), buffer.size(),
        reinterpret_cast<unsigned char*>(buffer.data()), out_len_vec.data(),
        out_ptr_vec.data());

    if (num_compressed == size) {
      break;
    }

    if (!force) {
      return output;
    }

    buffer.resize(2 * buffer.size());
  }

  auto const compressed_size =
      (out_ptr_vec.back() - out_ptr_vec.front()) + out_len_vec.back();

  if (symtab_size + compressed_size >= total_input_size && !force) {
    return output;
  }

  assert(compressed_size >= 0);
  assert(reinterpret_cast<char*>(out_ptr_vec.front()) == buffer.data());
  assert(std::cmp_equal(compressed_size,
                        std::accumulate(out_len_vec.begin(), out_len_vec.end(),
                                        static_cast<size_t>(0))));

  // TODO: we can probably get rid of this entirely
  out_ptr_vec.clear();
  out_ptr_vec.shrink_to_fit();

  buffer.resize(static_cast<size_t>(compressed_size));

  output.emplace();

  output->dictionary = std::move(symtab);
  output->buffer = std::move(buffer);
  output->positions.reset(
      fsst_encoder::index_type::required_bits(compressed_size), size + 1);

  std::partial_sum(out_len_vec.begin(), out_len_vec.end(),
                   output->positions.begin() + 1);

  return output;
}

class fsst_decoder_ : public fsst_decoder::impl {
 public:
  explicit fsst_decoder_(std::string_view dictionary) {
    auto const read = ::fsst_import(
        &decoder_, reinterpret_cast<unsigned char const*>(dictionary.data()));
    if (read != dictionary.size()) {
      throw std::runtime_error(fmt::format(
          "read {0} symtab bytes, expected {1}", read, dictionary.size()));
    }
  }

  void
  decompress_append_to(std::string& out, std::string_view data) const override {
    decompress_append_to_impl(out, data);
  }

  void decompress_append_to_u8(std::u8string& out,
                               std::u8string_view data) const override {
    decompress_append_to_impl(out, data);
  }

 private:
  template <typename StringType, typename StringViewType>
    requires std::same_as<typename StringType::value_type,
                          typename StringViewType::value_type>
  void decompress_append_to_impl(StringType& out, StringViewType data) const {
    thread_local StringType buf;
    auto const size = data.size();
    buf.resize(8 * size);
    auto outlen = ::fsst_decompress(
        &decoder_, size, reinterpret_cast<unsigned char const*>(data.data()),
        buf.size(), reinterpret_cast<unsigned char*>(buf.data()));
    buf.resize(outlen);
    out.append(buf);
  }

  ::fsst_decoder_t decoder_;
};

} // namespace

auto fsst_encoder::compress(std::span<std::string_view const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress(data.begin(), data.end(), force);
}

auto fsst_encoder::compress(std::span<std::string const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress(data.begin(), data.end(), force);
}

auto fsst_encoder::compress(std::span<std::u8string_view const> data,
                            bool force)
    -> std::optional<bulk_compression_result> {
  return compress(data.begin(), data.end(), force);
}

auto fsst_encoder::compress(std::span<std::u8string const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress(data.begin(), data.end(), force);
}

auto fsst_encoder::compress(std::span<unsigned char const*> ptr_span,
                            std::span<size_t const> len_span,
                            std::size_t const total_input_size, bool force)
    -> std::optional<bulk_compression_result> {
  return fsst_compress_(ptr_span, len_span, total_input_size, force);
}

fsst_decoder::fsst_decoder(std::string_view dictionary)
    : impl_{std::make_unique<fsst_decoder_>(dictionary)} {}

} // namespace dwarfs::internal

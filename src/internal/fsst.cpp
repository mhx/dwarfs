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
#include <stdexcept>

#include <dwarfs/internal/fsst.h>

#include <fmt/format.h>

#include <fsst.h>

namespace dwarfs::internal {

namespace {

template <typename T>
std::optional<fsst_encoder::bulk_compression_result>
fsst_compress_(std::span<T const> input, bool force) {
  std::optional<fsst_encoder::bulk_compression_result> output;

  if (input.empty()) {
    return output;
  }

  auto const size = input.size();
  size_t total_input_size = 0;
  std::vector<size_t> len_vec;
  std::vector<unsigned char const*> ptr_vec;

  len_vec.reserve(size);
  ptr_vec.reserve(size);

  for (auto const& s : input) {
    ptr_vec.emplace_back(reinterpret_cast<unsigned char const*>(s.data()));
    len_vec.emplace_back(s.size());
    total_input_size += s.size();
  }

  std::unique_ptr<::fsst_encoder_t, decltype(&::fsst_destroy)> enc{
      ::fsst_create(size, len_vec.data(), ptr_vec.data(), 0), &::fsst_destroy};

  std::string symtab;

  symtab.resize(sizeof(::fsst_decoder_t));

  auto const symtab_size =
      ::fsst_export(enc.get(), reinterpret_cast<unsigned char*>(symtab.data()));
  symtab.resize(symtab_size);

  std::vector<size_t> out_len_vec;
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
        enc.get(), size, len_vec.data(), ptr_vec.data(), buffer.size(),
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

  size_t const compressed_size =
      (out_ptr_vec.back() - out_ptr_vec.front()) + out_len_vec.back();

  if (symtab_size + compressed_size >= total_input_size && !force) {
    return output;
  }

  assert(reinterpret_cast<char*>(out_ptr_vec.front()) == buffer.data());
  assert(compressed_size == std::accumulate(out_len_vec.begin(),
                                            out_len_vec.end(),
                                            static_cast<size_t>(0)));

  buffer.resize(compressed_size);

  output.emplace();

  output->dictionary = std::move(symtab);
  output->buffer = std::move(buffer);
  output->compressed_data.reserve(size);

  for (size_t i = 0; i < size; ++i) {
    output->compressed_data.emplace_back(
        reinterpret_cast<char*>(out_ptr_vec[i]), out_len_vec[i]);
  }

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

  std::string decompress(std::string_view data) const override {
    thread_local std::string out;
    auto const size = data.size();
    out.resize(8 * size);
    auto outlen = ::fsst_decompress(
        &decoder_, size, reinterpret_cast<unsigned char const*>(data.data()),
        out.size(), reinterpret_cast<unsigned char*>(out.data()));
    out.resize(outlen);
    return out;
  }

 private:
  ::fsst_decoder_t decoder_;
};

} // namespace

auto fsst_encoder::compress(std::span<std::string_view const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return fsst_compress_(data, force);
}

auto fsst_encoder::compress(std::span<std::string const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return fsst_compress_(data, force);
}

fsst_decoder::fsst_decoder(std::string_view dictionary)
    : impl_{std::make_unique<fsst_decoder_>(dictionary)} {}

} // namespace dwarfs::internal

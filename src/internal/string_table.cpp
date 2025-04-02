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

#include <algorithm>
#include <numeric>

#include <fmt/format.h>

#include <fsst.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>

#include <dwarfs/internal/string_table.h>

namespace dwarfs::internal {

class legacy_string_table : public string_table::impl {
 public:
  explicit legacy_string_table(string_table::LegacyTableView v)
      : v_{v} {}

  std::string lookup(size_t index) const override {
    return std::string(v_[index]);
  }

  std::vector<std::string> unpack() const override {
    throw std::runtime_error("cannot unpack legacy string table");
  }

  bool is_packed() const override { return false; }

  size_t unpacked_size() const override {
    return std::accumulate(v_.begin(), v_.end(), 0,
                           [](auto n, auto s) { return n + s.size(); });
  }

 private:
  string_table::LegacyTableView v_;
};

template <bool PackedData, bool PackedIndex>
class packed_string_table : public string_table::impl {
 public:
  packed_string_table(logger& lgr, [[maybe_unused]] std::string_view name,
                      string_table::PackedTableView v)
      : v_{v}
      , buffer_{v_.buffer().data()} {
    LOG_PROXY(debug_logger_policy, lgr);

    if constexpr (PackedData) {
      auto ti = LOG_TIMED_DEBUG;

      auto st = v_.symtab();
      DWARFS_CHECK(st, "symtab unexpectedly unset");
      dec_ = std::make_unique<fsst_decoder_t>();

      auto read = fsst_import(
          dec_.get(), reinterpret_cast<unsigned char const*>(st->data()));

      if (read != st->size()) {
        DWARFS_THROW(runtime_error,
                     fmt::format("read {0} symtab bytes, expected {1}", read,
                                 st->size()));
      }

      ti << "imported dictionary for " << name << " string table";
    }

    if constexpr (PackedIndex) {
      auto ti = LOG_TIMED_DEBUG;

      DWARFS_CHECK(v_.packed_index(), "index unexpectedly not packed");
      index_.resize(v_.index().size() + 1);
      std::partial_sum(v_.index().begin(), v_.index().end(),
                       index_.begin() + 1);

      ti << "unpacked index for " << name << " string table ("
         << sizeof(index_.front()) * index_.capacity() << " bytes)";
    }
  }

  std::string lookup(size_t index) const override {
    auto beg = buffer_;
    auto end = buffer_;

    if constexpr (PackedIndex) {
      beg += index_[index];
      end += index_[index + 1];
    } else {
      beg += v_.index()[index];
      end += v_.index()[index + 1];
    }

    if constexpr (PackedData) {
      thread_local std::string out;
      size_t size = end - beg;
      out.resize(8 * size);
      auto outlen = fsst_decompress(
          dec_.get(), size, reinterpret_cast<unsigned char const*>(beg),
          out.size(), reinterpret_cast<unsigned char*>(out.data()));
      out.resize(outlen);
      return out;
    }

    return {beg, end};
  }

  std::vector<std::string> unpack() const override {
    std::vector<std::string> v;
    auto size = PackedIndex ? index_.size() : v_.index().size();
    if (size > 0) {
      v.reserve(size - 1);
      for (size_t i = 0; i < size - 1; ++i) {
        v.emplace_back(lookup(i));
      }
    }
    return v;
  }

  bool is_packed() const override { return true; }

  size_t unpacked_size() const override {
    size_t unpacked = 0;
    auto size = PackedIndex ? index_.size() : v_.index().size();
    for (size_t i = 0; i < size - 1; ++i) {
      unpacked += lookup(i).size();
    }
    return unpacked;
  }

 private:
  string_table::PackedTableView v_;
  char const* const buffer_;
  std::vector<uint32_t> index_;
  std::unique_ptr<fsst_decoder_t> dec_;
};

string_table::string_table(LegacyTableView v)
    : impl_{std::make_unique<legacy_string_table>(v)} {}

namespace {

std::unique_ptr<string_table::impl>
build_string_table(logger& lgr, std::string_view name,
                   string_table::PackedTableView v) {
  if (v.symtab()) {
    if (v.packed_index()) {
      return std::make_unique<packed_string_table<true, true>>(lgr, name, v);
    }
    return std::make_unique<packed_string_table<true, false>>(lgr, name, v);
  }
  if (v.packed_index()) {
    return std::make_unique<packed_string_table<false, true>>(lgr, name, v);
  }
  return std::make_unique<packed_string_table<false, false>>(lgr, name, v);
}

} // namespace

string_table::string_table(logger& lgr, std::string_view name,
                           PackedTableView v)
    : impl_{build_string_table(lgr, name, v)} {}

template <typename T>
thrift::metadata::string_table
string_table::pack_generic(std::span<T const> input,
                           pack_options const& options) {
  auto size = input.size();
  bool pack_data = options.pack_data;
  size_t total_input_size = 0;
  std::string buffer;
  std::string symtab;
  std::vector<size_t> out_len_vec;
  std::vector<unsigned char*> out_ptr_vec;

  if (input.empty()) {
    pack_data = false;
  }

  if (pack_data) {
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
        ::fsst_create(size, len_vec.data(), ptr_vec.data(), 0),
        &::fsst_destroy};

    symtab.resize(sizeof(::fsst_decoder_t));

    auto symtab_size = ::fsst_export(
        enc.get(), reinterpret_cast<unsigned char*>(symtab.data()));
    symtab.resize(symtab_size);

    if (symtab.size() < total_input_size or options.force_pack_data) {
      out_len_vec.resize(size);
      out_ptr_vec.resize(size);

      buffer.resize(options.force_pack_data ? total_input_size
                                            : total_input_size - symtab.size());
      size_t num_compressed = 0;

      for (;;) {
        num_compressed = ::fsst_compress(
            enc.get(), size, len_vec.data(), ptr_vec.data(), buffer.size(),
            reinterpret_cast<unsigned char*>(buffer.data()), out_len_vec.data(),
            out_ptr_vec.data());

        if (num_compressed == size || !options.force_pack_data) {
          break;
        }

        buffer.resize(2 * buffer.size());
      }

      pack_data = num_compressed == size;
    } else {
      pack_data = false;
    }
  } else {
    for (auto const& s : input) {
      total_input_size += s.size();
    }
  }

  thrift::metadata::string_table output;

  if (pack_data) {
    // store compressed
    size_t compressed_size =
        (out_ptr_vec.back() - out_ptr_vec.front()) + out_len_vec.back();

    DWARFS_CHECK(reinterpret_cast<char*>(out_ptr_vec.front()) == buffer.data(),
                 "string table compression pointer mismatch");
    // TODO: only enable this in debug mode
    DWARFS_CHECK(compressed_size == std::accumulate(out_len_vec.begin(),
                                                    out_len_vec.end(),
                                                    static_cast<size_t>(0)),
                 "string table compression pointer mismatch");

    buffer.resize(compressed_size);
    output.buffer()->swap(buffer);
    output.symtab() = std::move(symtab);
    output.index()->resize(size);
    std::ranges::copy(out_len_vec, output.index()->begin());
  } else {
    // store uncompressed
    output.buffer()->reserve(total_input_size);
    output.index()->reserve(size);
    for (auto const& s : input) {
      output.buffer().value() += s;
      output.index()->emplace_back(s.size());
    }
  }

  output.packed_index() = options.pack_index;

  if (!options.pack_index) {
    output.index()->insert(output.index()->begin(), 0);
    std::partial_sum(output.index()->begin(), output.index()->end(),
                     output.index()->begin());
  }

  return output;
}

thrift::metadata::string_table
string_table::pack(std::span<std::string const> input,
                   pack_options const& options) {
  return pack_generic(input, options);
}

thrift::metadata::string_table
string_table::pack(std::span<std::string_view const> input,
                   pack_options const& options) {
  return pack_generic(input, options);
}

} // namespace dwarfs::internal

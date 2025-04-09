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

#include <array>
#include <cassert>
#include <optional>

#include <lzma.h>

#include <fmt/format.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>
#include <dwarfs/sorted_array_map.h>
#include <dwarfs/types.h>

#include "base.h"

namespace dwarfs {

namespace {

using namespace std::string_view_literals;

constexpr sorted_array_map lzma_error_desc{
    std::pair{LZMA_NO_CHECK, "input stream has no integrity check"},
    std::pair{LZMA_UNSUPPORTED_CHECK, "cannot calculate the integrity check"},
    std::pair{LZMA_GET_CHECK, "integrity check type is now available"},
    std::pair{LZMA_MEM_ERROR, "cannot allocate memory"},
    std::pair{LZMA_MEMLIMIT_ERROR, "memory usage limit was reached"},
    std::pair{LZMA_FORMAT_ERROR, "file format not recognized"},
    std::pair{LZMA_OPTIONS_ERROR, "invalid or unsupported options"},
    std::pair{LZMA_DATA_ERROR, "data is corrupt"},
    std::pair{LZMA_BUF_ERROR, "no progress is possible"},
    std::pair{LZMA_PROG_ERROR, "programming error"},
    // TODO: re-add when this has arrived in the mainstream...
    // {LZMA_SEEK_NEEDED, "request to change the input file position"},
};

constexpr sorted_array_map kBinaryModes{
    std::pair{"x86"sv, LZMA_FILTER_X86},
    std::pair{"powerpc"sv, LZMA_FILTER_POWERPC},
    std::pair{"ia64"sv, LZMA_FILTER_IA64},
    std::pair{"arm"sv, LZMA_FILTER_ARM},
    std::pair{"armthumb"sv, LZMA_FILTER_ARMTHUMB},
    std::pair{"sparc"sv, LZMA_FILTER_SPARC},
};

constexpr sorted_array_map kCompressionModes{
    std::pair{"fast"sv, LZMA_MODE_FAST},
    std::pair{"normal"sv, LZMA_MODE_NORMAL},
};

constexpr sorted_array_map kMatchFinders{
    std::pair{"hc3"sv, LZMA_MF_HC3}, std::pair{"hc4"sv, LZMA_MF_HC4},
    std::pair{"bt2"sv, LZMA_MF_BT2}, std::pair{"bt3"sv, LZMA_MF_BT3},
    std::pair{"bt4"sv, LZMA_MF_BT4},
};

template <typename T, size_t N>
T find_option(sorted_array_map<std::string_view, T, N> const& options,
              std::string_view name, std::string_view what) {
  if (auto value = options.get(name)) {
    return *value;
  }
  DWARFS_THROW(runtime_error, fmt::format("unknown {} '{}'", what, name));
}

template <typename T, size_t N>
std::string
option_names(sorted_array_map<std::string_view, T, N> const& options) {
  return options | ranges::views::keys | ranges::views::join(", "sv) |
         ranges::to<std::string>;
}

std::string lzma_error_string(lzma_ret err) {
  if (auto it = lzma_error_desc.find(err); it != lzma_error_desc.end()) {
    return it->second;
  }
  return fmt::format("unknown error {}", static_cast<int>(err));
}

class lzma_block_compressor final : public block_compressor::impl {
 public:
  explicit lzma_block_compressor(option_map& om);
  lzma_block_compressor(lzma_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lzma_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* metadata) const override;

  compression_type type() const override { return compression_type::LZMA; }

  std::string describe() const override { return description_; }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

 private:
  shared_byte_buffer
  compress(shared_byte_buffer const& data, lzma_filter const* filters) const;

  static uint32_t get_preset(unsigned level, bool extreme) {
    uint32_t preset = level;

    if (extreme) {
      preset |= LZMA_PRESET_EXTREME;
    }

    return preset;
  }

  static lzma_vli get_vli(std::optional<std::string_view> binary) {
    if (!binary) {
      return LZMA_VLI_UNKNOWN;
    }

    return find_option(kBinaryModes, *binary, "binary mode");
  }

  lzma_options_lzma opt_lzma_;
  lzma_vli binary_vli_;
  std::string description_;
};

lzma_block_compressor::lzma_block_compressor(option_map& om) {
  auto level = om.get<unsigned>("level", 9U);
  auto extreme = om.get<bool>("extreme", false);
  auto binary_mode = om.get_optional<std::string>("binary");
  auto dict_size = om.get_optional<unsigned>("dict_size");
  auto mode = om.get_optional<std::string>("mode");
  auto mf = om.get_optional<std::string>("mf");
  auto nice = om.get_optional<unsigned>("nice");
  auto depth = om.get_optional<unsigned>("depth");

  description_ = fmt::format(
      "lzma [level={}{}{}{}{}{}{}{}]", level,
      dict_size ? ", dict_size=" + std::to_string(*dict_size) : "",
      extreme ? ", extreme" : "", binary_mode ? ", binary=" + *binary_mode : "",
      mode ? ", mode=" + *mode : "", mf ? ", mf=" + *mf : "",
      nice ? ", nice=" + std::to_string(*nice) : "",
      depth ? ", depth=" + std::to_string(*depth) : "");

  binary_vli_ = get_vli(binary_mode);

  if (lzma_lzma_preset(&opt_lzma_, get_preset(level, extreme))) {
    DWARFS_THROW(runtime_error, "unsupported preset, possibly a bug");
  }

  if (dict_size) {
    opt_lzma_.dict_size = 1 << *dict_size;
  }

  if (mode) {
    opt_lzma_.mode = find_option(kCompressionModes, *mode, "compression mode");
  }

  if (mf) {
    opt_lzma_.mf = find_option(kMatchFinders, *mf, "match finder");
  }

  if (nice) {
    opt_lzma_.nice_len = *nice;
  }

  if (depth) {
    opt_lzma_.depth = *depth;
  }
}

shared_byte_buffer
lzma_block_compressor::compress(shared_byte_buffer const& data,
                                lzma_filter const* filters) const {
  lzma_stream s = LZMA_STREAM_INIT;

  if (auto ret = lzma_stream_encoder(&s, filters, LZMA_CHECK_CRC64);
      ret != LZMA_OK) {
    DWARFS_THROW(runtime_error, fmt::format("lzma_stream_encoder: {}",
                                            lzma_error_string(ret)));
  }

  lzma_action action = LZMA_FINISH;

  auto compressed = malloc_byte_buffer::create(); // TODO: make configurable
  compressed.resize(data.size() - 1);

  s.next_in = data.data();
  s.avail_in = data.size();
  s.next_out = compressed.data();
  s.avail_out = compressed.size();

  lzma_ret ret = lzma_code(&s, action);

  compressed.resize(compressed.size() - s.avail_out);

  lzma_end(&s);

  if (ret == 0) {
    throw bad_compression_ratio_error();
  }

  if (ret == LZMA_STREAM_END) {
    compressed.shrink_to_fit();
  } else {
    DWARFS_THROW(runtime_error, fmt::format("LZMA compression failed: {}",
                                            lzma_error_string(ret)));
  }

  return compressed.share();
}

shared_byte_buffer
lzma_block_compressor::compress(shared_byte_buffer const& data,
                                std::string const* /*metadata*/) const {
  auto lzma_opts = opt_lzma_;
  std::array<lzma_filter, 3> filters{{{binary_vli_, nullptr},
                                      {LZMA_FILTER_LZMA2, &lzma_opts},
                                      {LZMA_VLI_UNKNOWN, nullptr}}};

  auto best = compress(data, &filters[1]);

  if (filters[0].id != LZMA_VLI_UNKNOWN) {
    auto compressed = compress(data, filters.data());

    if (compressed.size() < best.size()) {
      best.swap(compressed);
    }
  }

  return best;
}

class lzma_block_decompressor final : public block_decompressor_base {
 public:
  lzma_block_decompressor(std::span<uint8_t const> data)
      : stream_(LZMA_STREAM_INIT)
      , uncompressed_size_(get_uncompressed_size(data.data(), data.size())) {
    stream_.next_in = data.data();
    stream_.avail_in = data.size();
    if (auto ret = lzma_stream_decoder(&stream_, UINT64_MAX, LZMA_CONCATENATED);
        ret != LZMA_OK) {
      DWARFS_THROW(runtime_error, fmt::format("lzma_stream_decoder: {}",
                                              lzma_error_string(ret)));
    }
  }

  ~lzma_block_decompressor() override { lzma_end(&stream_); }

  compression_type type() const override { return compression_type::LZMA; }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    lzma_action action = LZMA_RUN;

    if (decompressed_.size() + frame_size > uncompressed_size_) {
      frame_size = uncompressed_size_ - decompressed_.size();
      action = LZMA_FINISH;
    }

    assert(frame_size > 0);

    size_t offset = decompressed_.size();
    decompressed_.resize(offset + frame_size);

    stream_.next_out = decompressed_.data() + offset;
    stream_.avail_out = frame_size;

    lzma_ret ret = lzma_code(&stream_, action);

    if (ret == LZMA_STREAM_END) {
      lzma_end(&stream_);
    }

    if (ret != (action == LZMA_RUN ? LZMA_OK : LZMA_STREAM_END) ||
        stream_.avail_out != 0) {
      decompressed_.clear();
      error_ =
          fmt::format("LZMA decompression failed: {}", lzma_error_string(ret));
      DWARFS_THROW(runtime_error, error_);
    }

    return ret == LZMA_STREAM_END;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static size_t get_uncompressed_size(uint8_t const* data, size_t size);

  lzma_stream stream_;
  size_t const uncompressed_size_;
  std::string error_;
};

size_t lzma_block_decompressor::get_uncompressed_size(uint8_t const* data,
                                                      size_t size) {
  if (size < 2 * LZMA_STREAM_HEADER_SIZE) {
    DWARFS_THROW(runtime_error, "lzma compressed block is too small");
  }

  lzma_stream s = LZMA_STREAM_INIT;
  file_off_t pos = size - LZMA_STREAM_HEADER_SIZE;
  uint32_t const* ptr = reinterpret_cast<uint32_t const*>(data + size) - 1;

  while (*ptr == 0) {
    pos -= 4;
    --ptr;

    if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
      DWARFS_THROW(runtime_error, "data error (stream padding)");
    }
  }

  lzma_stream_flags footer_flags;

  if (auto ret = lzma_stream_footer_decode(&footer_flags, data + pos);
      ret != LZMA_OK) {
    DWARFS_THROW(runtime_error, fmt::format("lzma_stream_footer_decode: {}",
                                            lzma_error_string(ret)));
  }

  lzma_vli index_size = footer_flags.backward_size;
  if (static_cast<lzma_vli>(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
    DWARFS_THROW(runtime_error, "data error (index size)");
  }

  pos -= index_size;
  lzma_index* index = nullptr;

  if (auto ret = lzma_index_decoder(&s, &index, UINT64_MAX); ret != LZMA_OK) {
    DWARFS_THROW(runtime_error,
                 fmt::format("lzma_index_decoder: {}", lzma_error_string(ret)));
  }

  s.avail_in = index_size;
  s.next_in = data + pos;

  lzma_ret ret = lzma_code(&s, LZMA_RUN);
  if (ret != LZMA_STREAM_END || s.avail_in != 0) {
    DWARFS_THROW(runtime_error,
                 fmt::format("lzma_code(): {} (avail_in={})",
                             lzma_error_string(ret), s.avail_in));
  }

  pos -= LZMA_STREAM_HEADER_SIZE;
  if (static_cast<lzma_vli>(pos) < lzma_index_total_size(index)) {
    DWARFS_THROW(runtime_error, "data error (index total size)");
  }

  size_t usize = lzma_index_uncompressed_size(index);

  // TODO: wrap this in some RAII container, as error handling is horrible...
  lzma_end(&s);
  lzma_index_end(index, nullptr);

  return usize;
}

template <typename Base>
class lzma_compression_info : public Base {
 public:
  static constexpr compression_type type{compression_type::LZMA};

  std::string_view name() const override { return "lzma"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("LZMA compression (liblzma {})", ::lzma_version_string())};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("liblzma-{}", ::lzma_version_string())};
  }
};

class lzma_compressor_factory final
    : public lzma_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<lzma_block_compressor>(om);
  }

 private:
  std::vector<std::string> const options_{
      "level=[0..9]",
      "dict_size=[12..30]",
      "extreme",
      "binary={" + option_names(kBinaryModes) + "}",
      "mode={" + option_names(kCompressionModes) + "}",
      "mf={" + option_names(kMatchFinders) + "}",
      "nice=[0..273]",
      "depth=[0..4294967295]",
  };
};

class lzma_decompressor_factory final
    : public lzma_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<lzma_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(lzma_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(lzma_decompressor_factory)

} // namespace dwarfs

/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <lzma.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"
#include "dwarfs/types.h"

namespace dwarfs {

namespace {

std::unordered_map<lzma_ret, char const*> const lzma_error_desc{
    {LZMA_NO_CHECK, "input stream has no integrity check"},
    {LZMA_UNSUPPORTED_CHECK, "cannot calculate the integrity check"},
    {LZMA_GET_CHECK, "integrity check type is now available"},
    {LZMA_MEM_ERROR, "cannot allocate memory"},
    {LZMA_MEMLIMIT_ERROR, "memory usage limit was reached"},
    {LZMA_FORMAT_ERROR, "file format not recognized"},
    {LZMA_OPTIONS_ERROR, "invalid or unsupported options"},
    {LZMA_DATA_ERROR, "data is corrupt"},
    {LZMA_BUF_ERROR, "no progress is possible"},
    {LZMA_PROG_ERROR, "programming error"},
    // TODO: re-add when this has arrived in the mainstream...
    // {LZMA_SEEK_NEEDED, "request to change the input file position"},
};

std::string lzma_error_string(lzma_ret err) {
  if (auto it = lzma_error_desc.find(err); it != lzma_error_desc.end()) {
    return it->second;
  }
  return fmt::format("unknown error {}", static_cast<int>(err));
}

class lzma_block_compressor final : public block_compressor::impl {
 public:
  lzma_block_compressor(unsigned level, bool extreme,
                        const std::string& binary_mode, unsigned dict_size);
  lzma_block_compressor(const lzma_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lzma_block_compressor>(*this);
  }

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data,
                                std::string const* metadata) const override;
  std::vector<uint8_t> compress(std::vector<uint8_t>&& data,
                                std::string const* metadata) const override {
    return compress(data, metadata);
  }

  compression_type type() const override { return compression_type::LZMA; }

  std::string describe() const override { return description_; }

  std::string metadata_requirements() const override { return std::string(); }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return compression_constraints();
  }

 private:
  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data, const lzma_filter* filters) const;

  static uint32_t get_preset(unsigned level, bool extreme) {
    uint32_t preset = level;

    if (extreme) {
      preset |= LZMA_PRESET_EXTREME;
    }

    return preset;
  }

  static lzma_vli get_vli(const std::string& binary) {
    if (binary.empty()) {
      return LZMA_VLI_UNKNOWN;
    }

    std::unordered_map<std::string, lzma_vli> vm{
        {"x86", LZMA_FILTER_X86},           {"powerpc", LZMA_FILTER_POWERPC},
        {"ia64", LZMA_FILTER_IA64},         {"arm", LZMA_FILTER_ARM},
        {"armthumb", LZMA_FILTER_ARMTHUMB}, {"sparc", LZMA_FILTER_SPARC},
    };

    auto i = vm.find(binary);

    if (i == vm.end()) {
      DWARFS_THROW(runtime_error, "unsupported binary mode");
    }

    return i->second;
  }

  lzma_options_lzma opt_lzma_;
  std::array<lzma_filter, 3> filters_;
  std::string description_;
};

lzma_block_compressor::lzma_block_compressor(unsigned level, bool extreme,
                                             const std::string& binary_mode,
                                             unsigned dict_size)
    : description_{
          fmt::format("lzma [level={}, dict_size={}{}{}]", level, dict_size,
                      extreme ? ", extreme" : "",
                      binary_mode.empty() ? "" : ", binary=" + binary_mode)} {
  if (lzma_lzma_preset(&opt_lzma_, get_preset(level, extreme))) {
    DWARFS_THROW(runtime_error, "unsupported preset, possibly a bug");
  }

  if (dict_size > 0) {
    opt_lzma_.dict_size = 1 << dict_size;
  }

  filters_[0].id = get_vli(binary_mode);
  filters_[0].options = NULL;
  filters_[1].id = LZMA_FILTER_LZMA2;
  filters_[1].options = &opt_lzma_;
  filters_[2].id = LZMA_VLI_UNKNOWN;
  filters_[2].options = NULL;
}

std::vector<uint8_t>
lzma_block_compressor::compress(const std::vector<uint8_t>& data,
                                const lzma_filter* filters) const {
  lzma_stream s = LZMA_STREAM_INIT;

  if (lzma_stream_encoder(&s, filters, LZMA_CHECK_CRC64)) {
    DWARFS_THROW(runtime_error, "lzma_stream_encoder");
  }

  lzma_action action = LZMA_FINISH;

  std::vector<uint8_t> compressed(data.size() - 1);

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

  return compressed;
}

std::vector<uint8_t>
lzma_block_compressor::compress(const std::vector<uint8_t>& data,
                                std::string const* /*metadata*/) const {
  std::vector<uint8_t> best = compress(data, &filters_[1]);

  if (filters_[0].id != LZMA_VLI_UNKNOWN) {
    std::vector<uint8_t> compressed = compress(data, &filters_[0]);

    if (compressed.size() < best.size()) {
      best.swap(compressed);
    }
  }

  return best;
}

class lzma_block_decompressor final : public block_decompressor::impl {
 public:
  lzma_block_decompressor(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& target)
      : stream_(LZMA_STREAM_INIT)
      , decompressed_(target)
      , uncompressed_size_(get_uncompressed_size(data, size)) {
    stream_.next_in = data;
    stream_.avail_in = size;
    if (lzma_stream_decoder(&stream_, UINT64_MAX, LZMA_CONCATENATED) !=
        LZMA_OK) {
      DWARFS_THROW(runtime_error, "lzma_stream_decoder");
    }
    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("could not reserve {} bytes for decompressed block",
                      uncompressed_size_));
    }
  }

  ~lzma_block_decompressor() override { lzma_end(&stream_); }

  compression_type type() const override { return compression_type::LZMA; }

  std::optional<std::string> metadata() const override { return std::nullopt; }

  bool decompress_frame(size_t frame_size) override {
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
  static size_t get_uncompressed_size(const uint8_t* data, size_t size);

  lzma_stream stream_;
  std::vector<uint8_t>& decompressed_;
  const size_t uncompressed_size_;
  std::string error_;
};

size_t lzma_block_decompressor::get_uncompressed_size(const uint8_t* data,
                                                      size_t size) {
  if (size < 2 * LZMA_STREAM_HEADER_SIZE) {
    DWARFS_THROW(runtime_error, "lzma compressed block is too small");
  }

  lzma_stream s = LZMA_STREAM_INIT;
  file_off_t pos = size - LZMA_STREAM_HEADER_SIZE;
  const uint32_t* ptr = reinterpret_cast<const uint32_t*>(data + size) - 1;

  while (*ptr == 0) {
    pos -= 4;
    --ptr;

    if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
      DWARFS_THROW(runtime_error, "data error (stream padding)");
    }
  }

  lzma_stream_flags footer_flags;

  if (lzma_stream_footer_decode(&footer_flags, data + pos) != LZMA_OK) {
    DWARFS_THROW(runtime_error, "lzma_stream_footer_decode()");
  }

  lzma_vli index_size = footer_flags.backward_size;
  if (static_cast<lzma_vli>(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
    DWARFS_THROW(runtime_error, "data error (index size)");
  }

  pos -= index_size;
  lzma_index* index = NULL;

  if (lzma_index_decoder(&s, &index, UINT64_MAX) != LZMA_OK) {
    DWARFS_THROW(runtime_error, "lzma_index_decoder()");
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
  lzma_index_end(index, NULL);

  return usize;
}

class lzma_compression_factory : public compression_factory {
 public:
  std::string_view name() const override { return "lzma"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("LZMA compression (liblzma {})", ::lzma_version_string())};
    return s_desc;
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<lzma_block_compressor>(
        om.get<unsigned>("level", 9u), om.get<bool>("extreme", false),
        om.get<std::string>("binary"), om.get<unsigned>("dict_size", 0u));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<lzma_block_decompressor>(data.data(), data.size(),
                                                     target);
  }

 private:
  std::vector<std::string> const options_{
      "level=[0..9]",
      "dict_size=[12..30]",
      "extreme",
      "binary={x86,powerpc,ia64,arm,armthumb,sparc}",
  };
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::LZMA, lzma_compression_factory)

} // namespace dwarfs

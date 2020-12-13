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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <unordered_map>

#include <sys/types.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include <folly/Conv.h>

#include <fmt/format.h>

#ifdef DWARFS_HAVE_LIBLZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

#ifdef DWARFS_HAVE_LIBZSTD
#include <zstd.h>
#endif

#ifdef DWARFS_HAVE_LIBLZMA
#include <lzma.h>
#endif

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"

namespace dwarfs {

namespace {

class option_map {
 public:
  option_map(const std::string& spec) {
    std::vector<std::string> arg;
    boost::split(arg, spec, boost::is_any_of(":"));

    choice_ = arg[0];

    for (size_t i = 1; i < arg.size(); ++i) {
      std::vector<std::string> kv;
      boost::split(kv, arg[i], boost::is_any_of("="));

      if (kv.size() > 2) {
        DWARFS_THROW(runtime_error, "error parsing option " + kv[0] +
                                        " for choice " + choice_);
      }

      opt_[kv[0]] = kv.size() > 1 ? kv[1] : std::string("1");
    }
  }

  const std::string& choice() const { return choice_; }

  template <typename T>
  T get(const std::string& key, const T& default_value = T()) {
    auto i = opt_.find(key);

    if (i != opt_.end()) {
      std::string val = i->second;
      opt_.erase(i);
      return folly::to<T>(val);
    }

    return default_value;
  }

  void report() {
    if (!opt_.empty()) {
      std::vector<std::string> invalid;
      std::transform(
          opt_.begin(), opt_.end(), std::back_inserter(invalid),
          [](const std::pair<std::string, std::string>& p) { return p.first; });
      DWARFS_THROW(runtime_error, "invalid option(s) for choice " + choice_ +
                                      ": " + boost::join(invalid, ", "));
    }
  }

 private:
  std::unordered_map<std::string, std::string> opt_;
  std::string choice_;
};
} // namespace

#ifdef DWARFS_HAVE_LIBLZMA
class lzma_block_compressor : public block_compressor::impl {
 public:
  lzma_block_compressor(unsigned level, bool extreme,
                        const std::string& binary_mode, unsigned dict_size,
                        size_t block_size);
  lzma_block_compressor(const lzma_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lzma_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override;
  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  void append(const uint8_t* data, size_t size, bool last) override;

  std::vector<uint8_t> move_data() override { return std::move(data_); }

  compression_type type() const override { return compression_type::LZMA; }

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
  lzma_stream stream_;
  std::vector<uint8_t> data_;
};
#endif

class null_block_compressor : public block_compressor::impl {
 public:
  null_block_compressor() = default;
  null_block_compressor(const null_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<null_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override {
    return data;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return std::move(data);
  }

  void append(const uint8_t* data, size_t size, bool) override {
    data_.insert(data_.end(), data, data + size);
  }

  std::vector<uint8_t> move_data() override { return std::move(data_); }

  compression_type type() const override { return compression_type::NONE; }

 private:
  std::vector<uint8_t> data_;
};

#ifdef DWARFS_HAVE_LIBLZMA
lzma_block_compressor::lzma_block_compressor(unsigned level, bool extreme,
                                             const std::string& binary_mode,
                                             unsigned dict_size,
                                             size_t block_size)
    : stream_(LZMA_STREAM_INIT)
    , data_(block_size > 0 ? lzma_stream_buffer_bound(block_size) : 0) {
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

  if (block_size > 0) {
    auto* filters = &filters_[filters_[0].id == LZMA_VLI_UNKNOWN];

    if (lzma_stream_encoder(&stream_, filters, LZMA_CHECK_CRC64)) {
      DWARFS_THROW(runtime_error, "lzma_stream_encoder");
    }

    stream_.next_out = data_.data();
    stream_.avail_out = data_.size();
  }
}

void lzma_block_compressor::append(const uint8_t* data, size_t size,
                                   bool last) {
  const lzma_action action = last ? LZMA_FINISH : LZMA_RUN;

  stream_.next_in = data;
  stream_.avail_in = size;

  lzma_ret ret = lzma_code(&stream_, action);

  switch (ret) {
  case LZMA_STREAM_END:
    if (!last) {
      DWARFS_THROW(runtime_error, "LZMA unexpected stream end");
    }
    break;

  case LZMA_MEM_ERROR:
    DWARFS_THROW(runtime_error, "LZMA_MEM_ERROR");

  case LZMA_DATA_ERROR:
    DWARFS_THROW(runtime_error, "LZMA_DATA_ERROR");

  default:
    DWARFS_THROW(runtime_error, "unknown LZMA error");
  }

  if (last) {
    data_.resize(data_.size() - stream_.avail_out);
    data_.shrink_to_fit();
    lzma_end(&stream_);
  }
}

std::vector<uint8_t>
lzma_block_compressor::compress(const std::vector<uint8_t>& data,
                                const lzma_filter* filters) const {
  lzma_stream s = LZMA_STREAM_INIT;

  if (lzma_stream_encoder(&s, filters, LZMA_CHECK_CRC64)) {
    DWARFS_THROW(runtime_error, "lzma_stream_encoder");
  }

  lzma_action action = LZMA_FINISH;

  std::vector<uint8_t> compressed(lzma_stream_buffer_bound(data.size()));

  s.next_in = data.data();
  s.avail_in = data.size();
  s.next_out = compressed.data();
  s.avail_out = compressed.size();

  lzma_ret ret = lzma_code(&s, action);

  compressed.resize(compressed.size() - s.avail_out);

  lzma_end(&s);

  switch (ret) {
  case LZMA_STREAM_END:
    compressed.shrink_to_fit();
    break;

  case LZMA_MEM_ERROR:
    DWARFS_THROW(runtime_error, "LZMA_MEM_ERROR");

  case LZMA_DATA_ERROR:
    DWARFS_THROW(runtime_error, "LZMA_DATA_ERROR");

  default:
    DWARFS_THROW(runtime_error, "unknown LZMA error");
  }

  return compressed;
}

std::vector<uint8_t>
lzma_block_compressor::compress(const std::vector<uint8_t>& data) const {
  std::vector<uint8_t> best = compress(data, &filters_[1]);

  if (filters_[0].id != LZMA_VLI_UNKNOWN) {
    std::vector<uint8_t> compressed = compress(data, &filters_[0]);

    if (compressed.size() < best.size()) {
      best.swap(compressed);
    }
  }

  return best;
}
#endif

#ifdef DWARFS_HAVE_LIBLZ4
struct lz4_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int /*level*/) {
    return folly::to<size_t>(LZ4_compress_default(
        reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
        folly::to<int>(size), folly::to<int>(destsize)));
  }
};

struct lz4hc_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int level) {
    return folly::to<size_t>(LZ4_compress_HC(
        reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
        folly::to<int>(size), folly::to<int>(destsize), level));
  }
};

template <typename Policy>
class lz4_block_compressor : public block_compressor::impl {
 public:
  lz4_block_compressor(int level = 0)
      : level_(level) {}
  lz4_block_compressor(const lz4_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lz4_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override {
    std::vector<uint8_t> compressed(
        sizeof(uint32_t) + LZ4_compressBound(folly::to<int>(data.size())));
    uint32_t* psize = reinterpret_cast<uint32_t*>(&compressed[0]);
    *psize = data.size();
    auto csize =
        Policy::compress(&data[0], &compressed[sizeof(uint32_t)], data.size(),
                         compressed.size() - sizeof(uint32_t), level_);
    if (csize == 0) {
      DWARFS_THROW(runtime_error, "error during compression");
    }
    compressed.resize(sizeof(uint32_t) + csize);
    return compressed;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  void append(const uint8_t* data, size_t size, bool) override {
    data_.insert(data_.end(), data, data + size);
  }

  std::vector<uint8_t> move_data() override {
    return compress(std::move(data_));
  }

  compression_type type() const override { return compression_type::LZ4; }

 private:
  std::vector<uint8_t> data_;
  const int level_;
};
#endif

#ifdef DWARFS_HAVE_LIBZSTD
class zstd_block_compressor : public block_compressor::impl {
 public:
  zstd_block_compressor(int level)
      : ctx_(ZSTD_createCCtx())
      , level_(level) {}

  zstd_block_compressor(const zstd_block_compressor& rhs)
      : ctx_(ZSTD_createCCtx())
      , level_(rhs.level_) {}

  ~zstd_block_compressor() { ZSTD_freeCCtx(ctx_); }

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zstd_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override;

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  void append(const uint8_t* data, size_t size, bool) override {
    data_.insert(data_.end(), data, data + size);
  }

  std::vector<uint8_t> move_data() override {
    return compress(std::move(data_));
  }

  compression_type type() const override { return compression_type::ZSTD; }

 private:
  ZSTD_CCtx* ctx_;
  std::vector<uint8_t> data_;
  const int level_;
};

std::vector<uint8_t>
zstd_block_compressor::compress(const std::vector<uint8_t>& data) const {
  std::vector<uint8_t> compressed(ZSTD_compressBound(data.size()));
  auto size = ZSTD_compress(compressed.data(), compressed.size(), data.data(),
                            data.size(), level_);
  compressed.resize(size);
  compressed.shrink_to_fit();
  return compressed;
}
#endif

block_compressor::block_compressor(const std::string& spec, size_t block_size) {
  option_map om(spec);

  if (om.choice() == "null") {
    impl_ = std::make_unique<null_block_compressor>();
#ifdef DWARFS_HAVE_LIBLZMA
  } else if (om.choice() == "lzma") {
    impl_ = std::make_unique<lzma_block_compressor>(
        om.get<unsigned>("level", 9u), om.get<bool>("extreme", false),
        om.get<std::string>("binary"), om.get<unsigned>("dict_size", 0u),
        block_size);
#endif
#ifdef DWARFS_HAVE_LIBLZ4
  } else if (om.choice() == "lz4") {
    impl_ = std::make_unique<lz4_block_compressor<lz4_compression_policy>>();
  } else if (om.choice() == "lz4hc") {
    impl_ = std::make_unique<lz4_block_compressor<lz4hc_compression_policy>>(
        om.get<int>("level", 9));
#endif
#ifdef DWARFS_HAVE_LIBZSTD
  } else if (om.choice() == "zstd") {
    impl_ = std::make_unique<zstd_block_compressor>(
        om.get<int>("level", ZSTD_maxCLevel()));
#endif
  } else {
    DWARFS_THROW(runtime_error, "unknown compression: " + om.choice());
  }

  om.report();
}

class null_block_decompressor : public block_decompressor::impl {
 public:
  null_block_decompressor(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& target)
      : decompressed_(target)
      , data_(data)
      , uncompressed_size_(size) {
    decompressed_.reserve(uncompressed_size_);
  }

  compression_type type() const override { return compression_type::NONE; }

  bool decompress_frame(size_t frame_size) override {
    if (decompressed_.size() + frame_size > uncompressed_size_) {
      frame_size = uncompressed_size_ - decompressed_.size();
    }

    assert(frame_size > 0);

    size_t offset = decompressed_.size();
    decompressed_.resize(offset + frame_size);

    std::copy(data_ + offset, data_ + offset + frame_size,
              &decompressed_[offset]);

    return decompressed_.size() == uncompressed_size_;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  std::vector<uint8_t>& decompressed_;
  const uint8_t* const data_;
  const size_t uncompressed_size_;
};

#ifdef DWARFS_HAVE_LIBLZMA
class lzma_block_decompressor : public block_decompressor::impl {
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
    decompressed_.reserve(uncompressed_size_);
  }

  ~lzma_block_decompressor() { lzma_end(&stream_); }

  compression_type type() const override { return compression_type::LZMA; }

  bool decompress_frame(size_t frame_size) override {
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
      DWARFS_THROW(runtime_error,
                   fmt::format("error while decompressing LZMA stream "
                               "(action={0}, ret={1}, stream_.avail_out={2})",
                               action, ret, stream_.avail_out));
    }

    return ret == LZMA_STREAM_END;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static size_t get_uncompressed_size(const uint8_t* data, size_t size);

  lzma_stream stream_;
  std::vector<uint8_t>& decompressed_;
  const size_t uncompressed_size_;
};
#endif

#ifdef DWARFS_HAVE_LIBLZ4
class lz4_block_decompressor : public block_decompressor::impl {
 public:
  lz4_block_decompressor(const uint8_t* data, size_t size,
                         std::vector<uint8_t>& target)
      : decompressed_(target)
      , data_(data + sizeof(uint32_t))
      , input_size_(size - sizeof(uint32_t))
      , uncompressed_size_(get_uncompressed_size(data)) {
    decompressed_.reserve(uncompressed_size_);
  }

  compression_type type() const override { return compression_type::LZ4; }

  bool decompress_frame(size_t) override {
    decompressed_.resize(uncompressed_size_);
    LZ4_decompress_safe(reinterpret_cast<const char*>(data_),
                        reinterpret_cast<char*>(&decompressed_[0]),
                        static_cast<int>(input_size_),
                        static_cast<int>(uncompressed_size_));
    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static size_t get_uncompressed_size(const uint8_t* data) {
    uint32_t size;
    ::memcpy(&size, data, sizeof(size));
    return size;
  }

  std::vector<uint8_t>& decompressed_;
  const uint8_t* const data_;
  const size_t input_size_;
  const size_t uncompressed_size_;
};
#endif

#ifdef DWARFS_HAVE_LIBZSTD
class zstd_block_decompressor : public block_decompressor::impl {
 public:
  zstd_block_decompressor(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& target)
      : decompressed_(target)
      , data_(data)
      , size_(size)
      , uncompressed_size_(ZSTD_getDecompressedSize(data, size)) {
    decompressed_.reserve(uncompressed_size_);
  }

  compression_type type() const override { return compression_type::ZSTD; }

  bool decompress_frame(size_t /*frame_size*/) override {
    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);
    auto rv = ZSTD_decompress(decompressed_.data(), decompressed_.size(), data_,
                              size_);

    if (ZSTD_isError(rv)) {
      decompressed_.clear();
      error_ = fmt::format("ZSTD: {}", ZSTD_getErrorName(rv));
      DWARFS_THROW(runtime_error, error_);
    }

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  std::vector<uint8_t>& decompressed_;
  const uint8_t* const data_;
  const size_t size_;
  const size_t uncompressed_size_;
  std::string error_;
};
#endif

#ifdef DWARFS_HAVE_LIBLZMA
size_t lzma_block_decompressor::get_uncompressed_size(const uint8_t* data,
                                                      size_t size) {
  if (size < 2 * LZMA_STREAM_HEADER_SIZE) {
    DWARFS_THROW(runtime_error, "lzma compressed block is too small");
  }

  lzma_stream s = LZMA_STREAM_INIT;
  off_t pos = size - LZMA_STREAM_HEADER_SIZE;
  const uint32_t* ptr = reinterpret_cast<const uint32_t*>(data + size) - 1;
  lzma_vli stream_padding = 0;

  while (*ptr == 0) {
    stream_padding += 4;
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
                 fmt::format("lzma_code(): {} (avail_in={})", ret, s.avail_in));
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
#endif

block_decompressor::block_decompressor(compression_type type,
                                       const uint8_t* data, size_t size,
                                       std::vector<uint8_t>& target) {
  switch (type) {
  case compression_type::NONE:
    impl_ = std::make_unique<null_block_decompressor>(data, size, target);
    break;

#ifdef DWARFS_HAVE_LIBLZMA
  case compression_type::LZMA:
    impl_ = std::make_unique<lzma_block_decompressor>(data, size, target);
    break;
#endif

#ifdef DWARFS_HAVE_LIBLZ4
  case compression_type::LZ4:
  case compression_type::LZ4HC:
    impl_ = std::make_unique<lz4_block_decompressor>(data, size, target);
    break;
#endif

#ifdef DWARFS_HAVE_LIBZSTD
  case compression_type::ZSTD:
    impl_ = std::make_unique<zstd_block_decompressor>(data, size, target);
    break;
#endif

  default:
    DWARFS_THROW(runtime_error,
                 "unsupported compression type: " + get_compression_name(type));
  }
}
} // namespace dwarfs

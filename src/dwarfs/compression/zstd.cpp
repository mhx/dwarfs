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

#include <mutex>

#include <zstd.h>

#include <fmt/format.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"

#if ZSTD_VERSION_MAJOR > 1 ||                                                  \
    (ZSTD_VERSION_MAJOR == 1 && ZSTD_VERSION_MINOR >= 4)
#define ZSTD_MIN_LEVEL ZSTD_minCLevel()
#else
#define ZSTD_MIN_LEVEL 1
#endif

namespace dwarfs {

namespace {

class zstd_block_compressor final : public block_compressor::impl {
 public:
  explicit zstd_block_compressor(int level)
      : ctxmgr_(get_context_manager())
      , level_(level) {}

  zstd_block_compressor(const zstd_block_compressor& rhs)
      : level_(rhs.level_) {}

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zstd_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override;

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  compression_type type() const override { return compression_type::ZSTD; }

 private:
  class scoped_context;

  class context_manager {
   public:
    context_manager() = default;

    ~context_manager() {
      for (auto ctx : ctx_) {
        ZSTD_freeCCtx(ctx);
      }
    }

   private:
    friend class scoped_context;

    ZSTD_CCtx* acquire() {
      std::lock_guard lock(mx_);
      if (ctx_.empty()) {
        return ZSTD_createCCtx();
      }
      auto ctx = ctx_.back();
      ctx_.pop_back();
      return ctx;
    }

    void release(ZSTD_CCtx* ctx) {
      std::lock_guard lock(mx_);
      ctx_.push_back(ctx);
    }

    std::mutex mx_;
    std::vector<ZSTD_CCtx*> ctx_;
  };

  class scoped_context {
   public:
    explicit scoped_context(context_manager& mgr)
        : mgr_{&mgr}
        , ctx_{mgr_->acquire()} {}
    ~scoped_context() { mgr_->release(ctx_); }

    scoped_context(scoped_context const&) = delete;
    scoped_context(scoped_context&&) = default;
    scoped_context& operator=(scoped_context const&) = delete;
    scoped_context& operator=(scoped_context&&) = default;

    ZSTD_CCtx* get() const { return ctx_; }

   private:
    context_manager* mgr_;
    ZSTD_CCtx* ctx_;
  };

  static std::shared_ptr<context_manager> get_context_manager() {
    std::lock_guard lock(s_mx);
    if (auto mgr = s_ctxmgr.lock()) {
      return mgr;
    }
    auto mgr = std::make_shared<context_manager>();
    s_ctxmgr = mgr;
    return mgr;
  }

  static std::mutex s_mx;
  static std::weak_ptr<context_manager> s_ctxmgr;

  std::shared_ptr<context_manager> ctxmgr_;
  const int level_;
};

std::mutex zstd_block_compressor::s_mx;
std::weak_ptr<zstd_block_compressor::context_manager>
    zstd_block_compressor::s_ctxmgr;

std::vector<uint8_t>
zstd_block_compressor::compress(const std::vector<uint8_t>& data) const {
  std::vector<uint8_t> compressed(ZSTD_compressBound(data.size()));
  scoped_context ctx(*ctxmgr_);
  auto size = ZSTD_compressCCtx(ctx.get(), compressed.data(), compressed.size(),
                                data.data(), data.size(), level_);
  if (ZSTD_isError(size)) {
    DWARFS_THROW(runtime_error,
                 fmt::format("ZSTD: {}", ZSTD_getErrorName(size)));
  }
  if (size >= data.size()) {
    throw bad_compression_ratio_error();
  }
  compressed.resize(size);
  compressed.shrink_to_fit();
  return compressed;
}

class zstd_block_decompressor final : public block_decompressor::impl {
 public:
  zstd_block_decompressor(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& target)
      : decompressed_(target)
      , data_(data)
      , size_(size)
      , uncompressed_size_(ZSTD_getFrameContentSize(data, size)) {
    switch (uncompressed_size_) {
    case ZSTD_CONTENTSIZE_UNKNOWN:
      DWARFS_THROW(runtime_error, "ZSTD content size unknown");
      break;

    case ZSTD_CONTENTSIZE_ERROR:
      DWARFS_THROW(runtime_error, "ZSTD content size error");
      break;

    default:
      break;
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
  const unsigned long long uncompressed_size_;
  std::string error_;
};

class zstd_compression_factory : public compression_factory {
 public:
  zstd_compression_factory()
      : options_{
            fmt::format("level=[{}..{}]", ZSTD_MIN_LEVEL, ZSTD_maxCLevel())} {}

  std::string_view name() const override { return "zstd"; }

  std::string_view description() const override { return "ZSTD compression"; }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<zstd_block_compressor>(
        om.get<int>("level", ZSTD_maxCLevel()));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<zstd_block_decompressor>(data.data(), data.size(),
                                                     target);
  }

 private:
  std::vector<std::string> const options_;
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::ZSTD, zstd_compression_factory)

} // namespace dwarfs

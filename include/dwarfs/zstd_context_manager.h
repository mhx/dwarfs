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

#pragma once

#include <mutex>
#include <vector>

#include <zstd.h>

namespace dwarfs {

class zstd_context_manager;

class zstd_scoped_context {
  friend class zstd_context_manager;

 public:
  ~zstd_scoped_context();

  zstd_scoped_context(zstd_scoped_context&&) = default;
  zstd_scoped_context& operator=(zstd_scoped_context&&) = default;

  ZSTD_CCtx* get() const { return ctx_; }

 private:
  explicit zstd_scoped_context(zstd_context_manager* mgr);

  zstd_context_manager* mgr_;
  ZSTD_CCtx* ctx_;
};

class zstd_context_manager {
  friend class zstd_scoped_context;

 public:
  zstd_context_manager() = default;

  ~zstd_context_manager() {
    for (auto ctx : ctx_) {
      ZSTD_freeCCtx(ctx);
    }
  }

  zstd_scoped_context make_context() { return zstd_scoped_context(this); }

 private:
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

inline zstd_scoped_context::zstd_scoped_context(zstd_context_manager* mgr)
    : mgr_{mgr}
    , ctx_{mgr_->acquire()} {}

inline zstd_scoped_context::~zstd_scoped_context() { mgr_->release(ctx_); }

} // namespace dwarfs

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

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

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if defined(__SANITIZE_THREAD__)
#define DWARFS_SANITIZE_THREAD 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define DWARFS_SANITIZE_THREAD 1
#endif
#endif

#if defined(__has_builtin)
#if __has_builtin(__builtin_cpu_supports) && __has_attribute(target)
#define DWARFS_USE_CPU_FEATURES 1
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define DWARFS_FORCE_INLINE inline __attribute__((__always_inline__))
#elif defined(_MSC_VER)
#define DWARFS_FORCE_INLINE __forceinline
#else
#define DWARFS_FORCE_INLINE inline
#endif

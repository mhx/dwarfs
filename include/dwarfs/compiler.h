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

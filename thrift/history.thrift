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

package "dwarfs.dev/thrift/history"

include "thrift/annotation/cpp.thrift"

@cpp.Type{name = "uint8_t"}
typedef byte UInt8
@cpp.Type{name = "uint16_t"}
typedef i16 UInt16
@cpp.Type{name = "uint32_t"}
typedef i32 UInt32
@cpp.Type{name = "uint64_t"}
typedef i64 UInt64

struct dwarfs_version {
   1: UInt16 major
   2: UInt16 minor
   3: UInt16 patch
   4: bool is_release
   5: optional string git_rev
   6: optional string git_branch
   7: optional string git_desc
}

struct history_entry {
   1: dwarfs_version version
   2: string system_id
   3: string compiler_id
   4: optional list<string> arguments
   5: optional UInt64 timestamp
   6: optional set<string> library_versions
}

struct history {
   1: list<history_entry> entries
}

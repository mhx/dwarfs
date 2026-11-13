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

#include <string_view>

namespace dwarfs::tool::manpage {

// An immutable stream of UTF-8 text and inline formatting commands.
//
// The generator writes exactly one of the following control sequences
// per logical layout line (before width-dependent wrapping):
//
//   0x10..0x19             common indentation / no-wrap layout (see decoder)
//   0x1f <first> <next> <no_wrap>  arbitrary layout (all byte values)
//
// A line continues until the next line command or the end of the stream.
// Every line starts with unstyled text; within a line, these commands begin
// a new text fragment (which can affect wrapping at style boundaries):
//
//   0x01                   plain
//   0x02                   bold
//   0x03                   italic
//   0x04                   heading
//   0x05                   code
//   0x06                   preformatted block
//   0x07 <mask>            combination of style bits (BOLD=1, ITALIC=2,
//                          HEAD=4, CODE=8, BLOCK=16)
//
// No other bytes are interpreted as commands. Literal occurrences of the
// reserved control bytes 0x01..0x07 and 0x10..0x1f are rejected at generation.
using document = std::string_view;

} // namespace dwarfs::tool::manpage

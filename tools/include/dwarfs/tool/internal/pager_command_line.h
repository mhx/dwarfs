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

#ifdef _WIN32

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwarfs::tool::internal {

/**
 * Append \p arg to \p cmd, quoted per the CommandLineToArgvW rules, so that
 * the child re-parses exactly what we intended.
 *
 * See "Parsing C++ Command-Line Arguments" and Daniel Colascione's "Everyone
 * quotes command line arguments the wrong way".
 */
void append_quoted(std::wstring& cmd, std::wstring const& arg);

/**
 * Split a command line into arguments. Exact inverse of append_quoted():
 * CommandLineToArgvW implements the very rules we quote for.
 *
 * \note The caller must reject empty or blank input. For an empty string
 *       CommandLineToArgvW returns the path of the current executable rather
 *       than an empty vector.
 *
 * \returns nullopt if the command line could not be parsed.
 */
std::optional<std::vector<std::string>> split_command_line(std::string_view sv);

} // namespace dwarfs::tool::internal

#endif

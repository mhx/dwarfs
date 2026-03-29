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

#include <memory>
#include <string>

namespace dwarfs {

class scoped_output_capture {
 public:
  enum class capture : unsigned {
    stdout_only = 0x1,
    stderr_only = 0x2,
    both = stdout_only | stderr_only,
  };

  explicit scoped_output_capture(capture which = capture::stdout_only);
  ~scoped_output_capture();

  scoped_output_capture(scoped_output_capture const&) = delete;
  scoped_output_capture& operator=(scoped_output_capture const&) = delete;

  scoped_output_capture(scoped_output_capture&&) noexcept;
  scoped_output_capture& operator=(scoped_output_capture&&) noexcept;

  void stop();

  std::string const& captured_stdout();
  std::string const& captured_stderr();
  std::string const& captured();

 private:
  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

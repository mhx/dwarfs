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

#include <filesystem>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace dwarfs {

class glob_matcher;
class library_dependencies;
class logger;
class os_access;

namespace reader {

class filesystem_v2_lite;

}

namespace utility {

struct filesystem_extractor_options {
  size_t max_queued_bytes{static_cast<size_t>(512) << 20};
  bool continue_on_error{false};
  std::function<void(std::string_view, uint64_t, uint64_t)> progress;
};

class filesystem_extractor {
 public:
  filesystem_extractor(logger& lgr, os_access const& os);

  static void add_library_dependencies(library_dependencies& deps);

  void
  open_archive(std::filesystem::path const& output, std::string const& format,
               std::string const& format_options = "") {
    impl_->open_archive(output, format, format_options);
  }

  void open_stream(std::ostream& os, std::string const& format,
                   std::string const& format_options = "") {
    impl_->open_stream(os, format, format_options);
  }

  void open_disk(std::filesystem::path const& output) {
    impl_->open_disk(output);
  }

  void close() { impl_->close(); }

  bool extract(reader::filesystem_v2_lite const& fs,
               filesystem_extractor_options const& opts =
                   filesystem_extractor_options()) {
    return impl_->extract(fs, nullptr, opts);
  }

  bool
  extract(reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
          filesystem_extractor_options const& opts =
              filesystem_extractor_options()) {
    return impl_->extract(fs, matcher, opts);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    open_archive(std::filesystem::path const& output, std::string const& format,
                 std::string const& format_options = "") = 0;
    virtual void open_stream(std::ostream& os, std::string const& format,
                             std::string const& format_options) = 0;
    virtual void open_disk(std::filesystem::path const& output) = 0;
    virtual void close() = 0;
    virtual bool
    extract(reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
            filesystem_extractor_options const& opts) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace utility

} // namespace dwarfs

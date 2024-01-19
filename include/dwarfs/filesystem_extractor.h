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

#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include <folly/Function.h>

namespace dwarfs {

class filesystem_v2;
class logger;
class os_access;

struct filesystem_extractor_options {
  size_t max_queued_bytes{4096};
  bool continue_on_error{false};
  folly::Function<void(std::string_view, uint64_t, uint64_t) const> progress;
};

class filesystem_extractor {
 public:
  filesystem_extractor(logger& lgr, os_access const& os);

  void open_archive(std::string const& output, std::string const& format) {
    return impl_->open_archive(output, format);
  }

  void open_stream(std::ostream& os, std::string const& format) {
    return impl_->open_stream(os, format);
  }

  void open_disk(std::string const& output) { return impl_->open_disk(output); }

  void close() { return impl_->close(); }

  bool
  extract(filesystem_v2 const& fs, filesystem_extractor_options const& opts =
                                       filesystem_extractor_options()) {
    return impl_->extract(fs, opts);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    open_archive(std::string const& output, std::string const& format) = 0;
    virtual void open_stream(std::ostream& os, std::string const& format) = 0;
    virtual void open_disk(std::string const& output) = 0;
    virtual void close() = 0;
    virtual bool extract(filesystem_v2 const& fs,
                         filesystem_extractor_options const& opts) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <dwarfs/file_stat.h>
#include <dwarfs/writer/input_entry_reader.h>

namespace dwarfs {

class file_access;
class library_dependencies;
class logger;
class os_access;
class thread_pool;

namespace writer {

struct scanner_options;

class entry_filter;
class filesystem_writer;
class writer_progress;
class segmenter_factory;

// Whether a directory contributes its contents to the image root, or
// appears as a named entry below the image root.
enum class scan_source_mode { contents, entry };

// A physical input root. When input_entries is set, only the listed entries
// are included, otherwise the directory is traversed recursively.
struct scan_source {
  std::filesystem::path path;
  std::unique_ptr<input_entry_reader> input_entries{};
  scan_source_mode mode{scan_source_mode::contents};
};

struct scan_root_attributes {
  std::optional<file_stat::mode_type> mode{};
  std::optional<file_stat::uid_type> uid{};
  std::optional<file_stat::gid_type> gid{};
  std::optional<file_stat::time_type> time{};
};

struct scan_request {
  std::vector<scan_source> sources;
  std::shared_ptr<file_access const> fa{};
  std::function<void(library_dependencies&)> extra_deps{};
  std::optional<file_stat> root_metadata{};
  scan_root_attributes root_attributes{};
};

class scanner {
 public:
  scanner(logger& lgr, thread_pool& pool, segmenter_factory& sf,
          os_access const& os, scanner_options const& options);

  void add_filter(std::unique_ptr<entry_filter>&& filter) {
    impl_->add_filter(std::move(filter));
  }

  void scan(filesystem_writer& fsw, scan_request const& request,
            writer_progress& prog) {
    impl_->scan(fsw, request, prog);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_filter(std::unique_ptr<entry_filter>&& filter) = 0;

    virtual void scan(filesystem_writer& fsw, scan_request const& request,
                      writer_progress& prog) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer
} // namespace dwarfs

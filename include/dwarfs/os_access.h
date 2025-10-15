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

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/memory_mapping.h>
#include <dwarfs/types.h>

namespace dwarfs {

class dir_reader {
 public:
  virtual ~dir_reader() = default;

  virtual bool read(std::filesystem::path& name) = 0;
};

// TODO: refactor this so we avoid all the smart pointers everywhere
class os_access {
 public:
  virtual ~os_access() = default;

  virtual std::unique_ptr<dir_reader>
  opendir(std::filesystem::path const& path) const = 0;
  virtual file_stat symlink_info(std::filesystem::path const& path) const = 0;
  virtual std::filesystem::path
  read_symlink(std::filesystem::path const& path) const = 0;
  virtual file_view open_file(std::filesystem::path const& path) const = 0;
  virtual readonly_memory_mapping map_empty_readonly(size_t size) const = 0;
  virtual memory_mapping map_empty(size_t size) const = 0;
  virtual int access(std::filesystem::path const& path, int mode) const = 0;
  virtual std::filesystem::path
  canonical(std::filesystem::path const& path) const = 0;
  virtual std::filesystem::path current_path() const = 0;
  virtual std::optional<std::string> getenv(std::string_view name) const = 0;
  virtual void
  thread_set_affinity(std::thread::id tid, std::span<int const> cpus,
                      std::error_code& ec) const = 0;
  virtual std::chrono::nanoseconds
  thread_get_cpu_time(std::thread::id tid, std::error_code& ec) const = 0;
  virtual std::filesystem::path
  find_executable(std::filesystem::path const& name) const = 0;
  virtual std::chrono::nanoseconds native_file_time_resolution() const = 0;
};
} // namespace dwarfs

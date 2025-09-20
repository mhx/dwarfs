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

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <dwarfs/os_access.h>

namespace dwarfs {

class os_access_generic : public os_access {
 public:
  std::unique_ptr<dir_reader>
  opendir(std::filesystem::path const& path) const override;
  file_stat symlink_info(std::filesystem::path const& path) const override;
  std::filesystem::path
  read_symlink(std::filesystem::path const& path) const override;
  file_view map_file(std::filesystem::path const& path) const override;
  int access(std::filesystem::path const& path, int mode) const override;
  std::filesystem::path
  canonical(std::filesystem::path const& path) const override;
  std::filesystem::path current_path() const override;
  std::optional<std::string> getenv(std::string_view name) const override;
  void thread_set_affinity(std::thread::id tid, std::span<int const> cpus,
                           std::error_code& ec) const override;
  std::chrono::nanoseconds
  thread_get_cpu_time(std::thread::id tid, std::error_code& ec) const override;
  std::filesystem::path
  find_executable(std::filesystem::path const& name) const override;
};
} // namespace dwarfs

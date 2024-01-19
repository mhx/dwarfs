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

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "dwarfs/os_access.h"

namespace dwarfs {

class mmif;

class os_access_generic : public os_access {
 public:
  std::unique_ptr<dir_reader>
  opendir(std::filesystem::path const& path) const override;
  file_stat symlink_info(std::filesystem::path const& path) const override;
  std::filesystem::path
  read_symlink(std::filesystem::path const& path) const override;
  std::unique_ptr<mmif>
  map_file(std::filesystem::path const& path) const override;
  std::unique_ptr<mmif>
  map_file(std::filesystem::path const& path, size_t size) const override;
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

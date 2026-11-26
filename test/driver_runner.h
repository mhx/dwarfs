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

#include "subprocess.h"

namespace dwarfs::test {

// Owns a FUSE test mount, not merely the launcher process. The default mode
// deliberately exercises daemonization on POSIX; Windows stays in foreground.
class driver_runner {
 public:
  struct foreground_t {};
  static constexpr foreground_t foreground{};
  struct automount_t {};
  static constexpr automount_t automount{};

  template <typename... Args>
  driver_runner(std::filesystem::path const& driver, bool tool_arg,
                std::filesystem::path const& image,
                std::filesystem::path const& mountpoint, Args&&... args)
      : driver_runner(mode::normal, driver, tool_arg, image, mountpoint,
                      subprocess::arguments(std::forward<Args>(args)...)) {}

  template <typename... Args>
  driver_runner(foreground_t, std::filesystem::path const& driver,
                bool tool_arg, std::filesystem::path const& image,
                std::filesystem::path const& mountpoint, Args&&... args)
      : driver_runner(mode::foreground, driver, tool_arg, image, mountpoint,
                      subprocess::arguments(std::forward<Args>(args)...)) {}

  template <typename... Args>
  driver_runner(automount_t, std::filesystem::path const& driver, bool tool_arg,
                std::filesystem::path const& image,
                std::filesystem::path const& mountpoint, Args&&... args)
      : driver_runner(mode::automount, driver, tool_arg, image, mountpoint,
                      subprocess::arguments(std::forward<Args>(args)...)) {}

  ~driver_runner();
  driver_runner(driver_runner const&) = delete;
  driver_runner& operator=(driver_runner const&) = delete;

  bool wait_until_ready(std::filesystem::path const& path);
  bool unmount();
  std::string const& cmdline() const;
  std::string const& err() const;

 private:
  enum class mode { normal, foreground, automount };

  driver_runner(mode how, std::filesystem::path const& driver, bool tool_arg,
                std::filesystem::path const& image,
                std::filesystem::path const& mountpoint,
                subprocess::argument_list const& args);

  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::test

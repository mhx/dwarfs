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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace dwarfs::writer {

namespace internal {

class progress;

} // namespace internal

class writer_progress {
 public:
  using update_function_type = std::function<void(writer_progress&, bool)>;

  writer_progress();
  explicit writer_progress(update_function_type func);
  writer_progress(update_function_type func,
                  std::chrono::microseconds interval);

  writer_progress(writer_progress const&) = delete;
  writer_progress& operator=(writer_progress const&) = delete;
  writer_progress(writer_progress&&) = delete;
  writer_progress& operator=(writer_progress&&) = delete;

  ~writer_progress() noexcept;

  size_t errors() const;

  internal::progress& get_internal() const { return *prog_; }

 private:
  std::unique_ptr<internal::progress> prog_;
  mutable std::mutex running_mx_;
  bool running_{false};
  std::condition_variable cond_;
  std::thread thread_;
};

} // namespace dwarfs::writer

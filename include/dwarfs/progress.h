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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <thread>

#include <folly/Function.h>

namespace dwarfs {

class file_interface;

class progress {
 public:
  progress(folly::Function<void(const progress&, bool)>&& func);
  ~progress() noexcept;

  template <typename T>
  void sync(T&& func) {
    std::unique_lock<std::mutex> lock(mx_);
    func();
  }

  std::atomic<file_interface const*> current{nullptr};
  std::atomic<size_t> files_found{0};
  std::atomic<size_t> files_scanned{0};
  std::atomic<size_t> dirs_found{0};
  std::atomic<size_t> dirs_scanned{0};
  std::atomic<size_t> links_found{0};
  std::atomic<size_t> links_scanned{0};
  std::atomic<size_t> duplicate_files{0};
  std::atomic<size_t> block_count{0};
  std::atomic<size_t> chunk_count{0};
  std::atomic<size_t> inodes_written{0};
  std::atomic<size_t> blocks_written{0};
  std::atomic<size_t> errors{0};
  std::atomic<uint64_t> original_size{0};
  std::atomic<uint64_t> saved_by_deduplication{0};
  std::atomic<uint64_t> saved_by_segmentation{0};
  std::atomic<uint64_t> filesystem_size{0};
  std::atomic<uint64_t> compressed_size{0};

 private:
  std::atomic<bool> running_;
  std::mutex mx_;
  std::condition_variable cond_;
  std::thread thread_;
};
} // namespace dwarfs

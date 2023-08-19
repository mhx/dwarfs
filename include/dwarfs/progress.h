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
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <folly/Function.h>

namespace dwarfs {

class object;

class progress {
 public:
  class context {
   public:
    explicit context(std::string const& name)
        : name_{name} {}

    std::string const& name() const { return name_; }

   private:
    std::string const name_;
  };

  using status_function_type =
      folly::Function<std::string(progress const&, size_t) const>;

  progress(folly::Function<void(const progress&, bool)>&& func,
           unsigned interval_ms);
  ~progress() noexcept;

  template <typename T>
  void sync(T&& func) {
    std::unique_lock lock(mx_);
    func();
  }

  std::shared_ptr<context> create_context(std::string const& name) const;
  std::vector<std::shared_ptr<context const>> get_active_contexts() const;

  void set_status_function(status_function_type status_fun);

  std::string status(size_t max_len) const;

  std::atomic<object const*> current{nullptr};
  std::atomic<uint64_t> total_bytes_read{0};
  std::atomic<size_t> current_size{0};
  std::atomic<size_t> current_offset{0};
  std::atomic<size_t> files_found{0};
  std::atomic<size_t> files_scanned{0};
  std::atomic<size_t> dirs_found{0};
  std::atomic<size_t> dirs_scanned{0};
  std::atomic<size_t> symlinks_found{0};
  std::atomic<size_t> symlinks_scanned{0};
  std::atomic<size_t> specials_found{0};
  std::atomic<size_t> duplicate_files{0};
  std::atomic<size_t> hardlinks{0};
  std::atomic<size_t> block_count{0};
  std::atomic<size_t> chunk_count{0};
  std::atomic<size_t> inodes_scanned{0};
  std::atomic<size_t> inodes_written{0};
  std::atomic<size_t> fragments_found{0};
  std::atomic<size_t> fragments_written{0};
  std::atomic<size_t> blocks_written{0};
  std::atomic<size_t> errors{0};
  std::atomic<size_t> nilsimsa_depth{0};
  std::atomic<size_t> blockify_queue{0};
  std::atomic<size_t> compress_queue{0};
  std::atomic<uint64_t> original_size{0};
  std::atomic<uint64_t> hardlink_size{0};
  std::atomic<uint64_t> symlink_size{0};
  std::atomic<uint64_t> saved_by_deduplication{0};
  std::atomic<uint64_t> saved_by_segmentation{0};
  std::atomic<uint64_t> filesystem_size{0};
  std::atomic<uint64_t> compressed_size{0};
  std::atomic<size_t> similarity_scans{0};
  std::atomic<uint64_t> similarity_bytes{0};
  std::atomic<size_t> hash_scans{0};
  std::atomic<uint64_t> hash_bytes{0};

 private:
  std::atomic<bool> running_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  status_function_type status_fun_;
  std::vector<std::weak_ptr<context const>> mutable contexts_;
  std::thread thread_;
};
} // namespace dwarfs

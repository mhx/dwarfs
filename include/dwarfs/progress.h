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
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <folly/Function.h>

#include "dwarfs/speedometer.h"
#include "dwarfs/terminal.h"

namespace dwarfs {

class object;

class progress {
 public:
  class context {
   public:
    struct status {
      termcolor color;
      std::string context;
      std::string status_string;
      std::optional<std::string> path;
      std::optional<size_t> bytes_processed;
      std::optional<size_t> bytes_total;
    };

    virtual ~context() = default;

    virtual status get_status() const = 0;
    virtual int get_priority() const { return 0; }

    speedometer<uint64_t> speed{std::chrono::seconds(5)};
  };

  using status_function_type =
      folly::Function<std::string(progress const&, size_t) const>;

  progress(folly::Function<void(progress&, bool)>&& func, unsigned interval_ms);
  ~progress() noexcept;

  void set_status_function(status_function_type status_fun);

  std::string status(size_t max_len);

  template <typename T, typename... Args>
  std::shared_ptr<T> create_context(Args&&... args) const {
    auto ctx = std::make_shared<T>(std::forward<Args>(args)...);
    add_context(ctx);
    return ctx;
  }

  std::vector<std::shared_ptr<context>> get_active_contexts() const;

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
  std::atomic<uint64_t> original_size{0};
  std::atomic<uint64_t> hardlink_size{0};
  std::atomic<uint64_t> symlink_size{0};
  std::atomic<uint64_t> saved_by_deduplication{0};
  std::atomic<uint64_t> saved_by_segmentation{0};
  std::atomic<uint64_t> filesystem_size{0};
  std::atomic<uint64_t> compressed_size{0};

  struct scan_progress {
    std::atomic<size_t> scans{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> usec{0};
    std::atomic<uint64_t> chunk_size{UINT64_C(16) << 20};
    std::atomic<uint64_t> bytes_per_sec{0};
  };

  class scan_updater {
   public:
    scan_updater(scan_progress& sp, size_t bytes)
        : sp_{sp}
        , bytes_{bytes}
        , start_{std::chrono::steady_clock::now()} {}

    ~scan_updater() {
      auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - start_)
                      .count();
      ++sp_.scans;
      sp_.bytes += bytes_;
      sp_.usec += usec;
    }

   private:
    scan_progress& sp_;
    size_t const bytes_;
    std::chrono::steady_clock::time_point const start_;
  };

  scan_progress similarity;
  scan_progress categorize;
  scan_progress hash;

 private:
  void add_context(std::shared_ptr<context> const& ctx) const;

  mutable std::mutex running_mx_;
  bool running_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  std::shared_ptr<status_function_type> status_fun_;
  std::vector<std::weak_ptr<context>> mutable contexts_;
  std::thread thread_;
};

} // namespace dwarfs

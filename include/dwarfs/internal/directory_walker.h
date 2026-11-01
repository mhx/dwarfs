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

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <dwarfs/os_access.h>

#include <dwarfs/internal/move_only_function.h>

namespace dwarfs::internal {

struct directory_walker_options {
  std::size_t num_workers{1};
  // Maximum number of queued entries.
  std::size_t max_pending_entries{4096};
  std::size_t batch_size{64};
  // Maximum number of directory reads queued or executing simultaneously,
  // defaults to (2 * num_workers).
  std::optional<std::size_t> max_inflight_directories{};
  bool collect_metadata{true};
};

using walk_entry = directory_entry;

/**
 * Walks a directory tree in parallel, invoking a visitor for each entry.
 *
 * \tparam Context    Type of the context object passed to the visitor and
 *                    completion callbacks.
 */
template <typename Context>
class directory_walker {
 public:
  using visitor_type = move_only_function<std::optional<Context>(
      walk_entry const&, Context const&)>;
  using completion_type = move_only_function<void(
      std::filesystem::path const&, Context const&, std::error_code)>;

  explicit directory_walker(os_access const& os,
                            directory_walker_options options = {})
      : os_{os}
      , options_{options} {
    if (!options_.num_workers || !options_.max_pending_entries ||
        !options_.batch_size) {
      throw std::invalid_argument("directory_walker: zero queue/worker limit");
    }
  }

  /**
   * Walk a directory tree.
   *
   * The visitor is invoked for each entry in the tree. If the visitor returns a
   * child context, the walker will descend into that entry. The completion
   * callback is invoked for each directory after all its children have been
   * processed, or if the directory could not be read, in which case the error
   * code is set.
   *
   * Each parent callback completes before any of its children are delivered.
   */
  void run(std::filesystem::path const& root, Context root_context,
           visitor_type visitor, completion_type on_complete = {}) const {
    if (options_.num_workers == 1) {
      struct task {
        std::filesystem::path path;
        Context context;
      };

      std::vector<task> stack;
      stack.push_back({root, std::move(root_context)});

      while (!stack.empty()) {
        std::error_code error;
        std::unique_ptr<dir_reader> reader;
        auto const current = std::move(stack.back());
        stack.pop_back();

        try {
          reader = os_.opendir(current.path);
        } catch (std::system_error const& e) {
          error = e.code();
        }

        if (reader) {
          std::vector<task> children;

          for (;;) {
            auto result = reader->read(options_.collect_metadata
                                           ? directory_metadata::full
                                           : directory_metadata::none);

            if (!result) {
              error = result.error();
              break;
            }

            if (!*result) {
              break;
            }

            auto ent = std::move(**result);

            fetch_missing_metadata(ent);

            if (auto child = visitor(ent, current.context)) {
              children.push_back({ent.path, std::move(*child)});
            }
          }

          if (on_complete) {
            on_complete(current.path, current.context, error);
          }

          stack.insert(stack.end(), std::make_move_iterator(children.rbegin()),
                       std::make_move_iterator(children.rend()));
        } else if (on_complete) {
          on_complete(current.path, current.context, error);
        }
      }
      return;
    }

    struct task {
      std::filesystem::path path;
      Context context;
    };

    struct event {
      walk_entry entry;
      std::shared_ptr<task const> parent;
      bool completed{false};
      std::error_code completion_error;
    };

    std::mutex mx;
    std::condition_variable jobs_cv, results_cv, space_cv;
    std::deque<std::shared_ptr<task const>> jobs;
    std::deque<task> deferred;
    std::deque<std::vector<event>> results;
    std::size_t queued_events{0};
    std::size_t inflight{0};
    bool stopping{false};
    std::exception_ptr worker_error;
    auto const max_inflight =
        options_.max_inflight_directories.value_or(2 * options_.num_workers);
    auto const batch_limit =
        std::min(options_.batch_size, options_.max_pending_entries);

    auto dispatch = [&] {
      while (inflight < max_inflight && !deferred.empty()) {
        jobs.push_back(
            std::make_shared<task const>(std::move(deferred.front())));
        deferred.pop_front();
        ++inflight;
        jobs_cv.notify_one();
      }
    };

    auto publish = [&](std::vector<event>& batch) {
      if (batch.empty()) {
        return;
      }

      std::unique_lock lock(mx);

      space_cv.wait(lock, [&] {
        return stopping ||
               queued_events + batch.size() <= options_.max_pending_entries;
      });

      if (!stopping) {
        queued_events += batch.size();
        results.push_back(std::move(batch));
        results_cv.notify_one();
      }

      batch = {};
    };

    auto worker = [&] {
      try {
        for (;;) {
          std::shared_ptr<task const> current;
          {
            std::unique_lock lock(mx);
            jobs_cv.wait(lock, [&] { return stopping || !jobs.empty(); });
            if (stopping) {
              return;
            }
            current = std::move(jobs.front());
            jobs.pop_front();
          }

          std::error_code directory_error;
          std::unique_ptr<dir_reader> reader;
          std::vector<event> batch;

          batch.reserve(batch_limit);

          try {
            reader = os_.opendir(current->path);
          } catch (std::system_error const& e) {
            directory_error = e.code();
          }

          if (reader) {
            for (;;) {
              auto result = reader->read(options_.collect_metadata
                                             ? directory_metadata::full
                                             : directory_metadata::none);
              if (!result) {
                directory_error = result.error();
                break;
              }
              if (!*result) {
                break;
              }
              auto ent = std::move(**result);
              fetch_missing_metadata(ent);
              batch.push_back({std::move(ent), current, false, {}});
              if (batch.size() == batch_limit) {
                publish(batch);
              }
            }
          }

          batch.push_back({{}, current, true, directory_error});
          publish(batch);
        }
      } catch (...) {
        {
          std::lock_guard lock(mx);
          if (!worker_error) {
            worker_error = std::current_exception();
          }
          stopping = true;
        }

        jobs_cv.notify_all();
        space_cv.notify_all();
        results_cv.notify_all();
      }
    };

    std::vector<std::thread> workers;

    auto stop_and_join = [&] {
      {
        std::lock_guard lock(mx);
        stopping = true;
      }

      jobs_cv.notify_all();
      space_cv.notify_all();
      results_cv.notify_all();

      for (auto& thread : workers) {
        if (thread.joinable()) {
          thread.join();
        }
      }
    };

    try {
      deferred.push_back({root, std::move(root_context)});

      {
        std::lock_guard lock(mx);
        dispatch();
      }

      workers.reserve(options_.num_workers);

      for (std::size_t i = 0; i < options_.num_workers; ++i) {
        workers.emplace_back(worker);
      }

      while (inflight || !deferred.empty()) {
        std::vector<event> batch;

        {
          std::unique_lock lock(mx);
          results_cv.wait(lock,
                          [&] { return worker_error || !results.empty(); });

          if (worker_error) {
            std::rethrow_exception(worker_error);
          }

          batch = std::move(results.front());
          results.pop_front();
          queued_events -= batch.size();
          space_cv.notify_all();
        }

        for (auto& ev : batch) {
          if (ev.completed) {
            if (on_complete) {
              on_complete(ev.parent->path, ev.parent->context,
                          ev.completion_error);
            }
            --inflight;
          } else if (auto child = visitor(ev.entry, ev.parent->context)) {
            deferred.push_back({ev.entry.path, std::move(*child)});
          }
        }

        {
          std::lock_guard lock(mx);
          dispatch();
        }
      }
    } catch (...) {
      stop_and_join();
      throw;
    }

    stop_and_join();
  }

 private:
  void fetch_missing_metadata(walk_entry& ent) const {
    if (!options_.collect_metadata) {
      ent.metadata.reset();
    } else if (!ent.metadata) {
      try {
        auto stat = os_.symlink_info(ent.path);
        if (auto ec = stat.error()) {
          ent.metadata.emplace(std::unexpected(ec));
        } else {
          ent.metadata.emplace(std::move(stat));
        }
      } catch (std::system_error const& e) {
        ent.metadata.emplace(std::unexpected(e.code()));
      }
    }
  }

  os_access const& os_;
  directory_walker_options options_;
};

} // namespace dwarfs::internal

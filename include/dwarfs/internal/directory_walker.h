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

#include <any>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

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
 * The visitor is serialized on the calling thread. Its returned context is
 * associated with a child directory before any entries of that directory are
 * delivered. A context must be copy-constructible (std::any requirement).
 */
class directory_walker {
 public:
  explicit directory_walker(os_access const& os,
                            directory_walker_options options = {});

  /**
   * Walk a directory tree.
   *
   * The visitor is invoked for each entry in the tree. If the visitor returns
   * a child context, the walker will descend into that entry. The completion
   * callback is invoked after the directory is enumerated, or on a directory
   * read error. This does not imply that all descendants have been processed.
   *
   * Each parent callback completes before any of its children are delivered.
   * The call blocks until all workers have stopped.
   */
  template <typename Context, typename Visitor>
  void run(std::filesystem::path const& root, Context&& root_context,
           Visitor&& visitor) const {
    run(root, std::forward<Context>(root_context),
        std::forward<Visitor>(visitor), nullptr);
  }

  template <typename Context, typename Visitor, typename Completion>
  void run(std::filesystem::path const& root, Context&& root_context,
           Visitor&& visitor, Completion&& on_complete) const {
    using context_type = std::decay_t<Context>;
    static_assert(std::is_copy_constructible_v<context_type>,
                  "directory_walker context must be copy-constructible");

    auto visit = [&](walk_entry const& entry, std::any const& parent) {
      auto child = std::invoke(std::forward<Visitor>(visitor), entry,
                               std::any_cast<context_type const&>(parent));
      std::optional<std::any> result;
      if (child) {
        result.emplace(std::in_place_type<context_type>, std::move(*child));
      }
      return result;
    };

    completion_type complete;

    if constexpr (!std::is_same_v<std::remove_cvref_t<Completion>,
                                  std::nullptr_t>) {
      complete = [&](std::filesystem::path const& path, std::any const& context,
                     std::error_code error) {
        std::invoke(std::forward<Completion>(on_complete), path,
                    std::any_cast<context_type const&>(context), error);
      };
    }

    run_impl(root,
             std::any(std::in_place_type<context_type>,
                      std::forward<Context>(root_context)),
             std::move(visit), std::move(complete));
  }

 private:
  using visitor_type = move_only_function<std::optional<std::any>(
      walk_entry const&, std::any const&)>;
  using completion_type = move_only_function<void(
      std::filesystem::path const&, std::any const&, std::error_code)>;

  void run_impl(std::filesystem::path const& root, std::any root_context,
                visitor_type visitor, completion_type on_complete) const;
  void fetch_missing_metadata(walk_entry& ent) const;

  os_access const& os_;
  directory_walker_options options_;
};

} // namespace dwarfs::internal

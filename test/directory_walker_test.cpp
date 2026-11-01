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

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <dwarfs/internal/directory_walker.h>

#include "test_helpers.h"

using namespace dwarfs;
namespace fs = std::filesystem;

namespace {

void populate(test::os_access_mock& os) {
  os.add_dir("/");
  os.add_dir("root");
  os.add_dir("root/a");
  os.add_dir("root/b");
  os.add_dir("root/a/deeper");
  os.add_file("root/a/deeper/file", "hello");
  os.add_file("root/a/file", "abc");
  os.add_file("root/b/file", "xyz");
  os.add_file("root/top", "top");
}

enum class injected_failure { metadata, enumeration, unexpected };

class failing_reader final : public dir_reader {
 public:
  failing_reader(std::unique_ptr<dir_reader> wrapped, injected_failure failure)
      : wrapped_{std::move(wrapped)}
      , failure_{failure} {}

  bool read(fs::path& name) override { return wrapped_->read(name); }

  directory_read_result read(directory_metadata mode) override {
    if (failure_ == injected_failure::unexpected) {
      throw std::logic_error("injected unexpected exception");
    }
    if (failure_ == injected_failure::enumeration && calls_++ == 1) {
      return std::unexpected(
          std::make_error_code(std::errc::permission_denied));
    }
    auto result = wrapped_->read(mode);
    if (result && *result && failure_ == injected_failure::metadata &&
        (*result)->path == "root/a") {
      (*result)->metadata.emplace(std::unexpected(
          std::make_error_code(std::errc::no_such_file_or_directory)));
    }
    return result;
  }

 private:
  std::unique_ptr<dir_reader> wrapped_;
  injected_failure failure_;
  size_t calls_{0};
};

class failing_os_access final : public test::os_access_mock {
 public:
  explicit failing_os_access(injected_failure failure)
      : failure_{failure} {}

  std::unique_ptr<dir_reader> opendir(fs::path const& path) const override {
    auto reader = test::os_access_mock::opendir(path);
    if (path == "root") {
      return std::make_unique<failing_reader>(std::move(reader), failure_);
    }
    return reader;
  }

 private:
  injected_failure failure_;
};

} // namespace

TEST(directory_walker_test, parent_context_and_metadata) {
  for (size_t workers : {1, 2, 4}) {
    test::os_access_mock os;
    populate(os);
    internal::directory_walker walker(os, {.num_workers = workers,
                                           .max_pending_entries = 2,
                                           .batch_size = 2,
                                           .max_inflight_directories = 2});

    std::set<fs::path> visited, completed;

    walker.run(
        "root", fs::path("root"),
        [&](internal::walk_entry const& ent,
            fs::path const& parent) -> std::optional<fs::path> {
          EXPECT_EQ(ent.path.parent_path(), parent);
          EXPECT_TRUE(ent.metadata.has_value());
          if (!ent.metadata) {
            return std::nullopt;
          }
          EXPECT_TRUE(ent.metadata->has_value());
          EXPECT_TRUE(parent == "root" || visited.contains(parent));
          EXPECT_TRUE(visited.insert(ent.path).second);
          if (ent.metadata && ent.metadata->has_value() &&
              ent.metadata->value().is_directory()) {
            return ent.path;
          }
          return std::nullopt;
        },
        [&](fs::path const& dir, fs::path const& context,
            std::error_code error) {
          EXPECT_FALSE(error);
          EXPECT_EQ(dir, context);
          EXPECT_TRUE(completed.insert(dir).second);
        });

    EXPECT_EQ(7, visited.size());
    EXPECT_EQ(4, completed.size());
  }
}

TEST(directory_walker_test, prune_subtrees) {
  test::os_access_mock os;
  populate(os);

  internal::directory_walker walker(os, {.num_workers = 4,
                                         .max_pending_entries = 1,
                                         .batch_size = 1,
                                         .max_inflight_directories = 1});
  std::set<fs::path> seen;

  walker.run(
      "root", 0,
      [&](internal::walk_entry const& e, int depth) -> std::optional<int> {
        EXPECT_LE(depth, 1);
        seen.insert(e.path);
        if (e.path == "root/a") {
          return std::nullopt;
        }
        if (e.metadata && e.metadata->has_value() &&
            e.metadata->value().is_directory()) {
          return depth + 1;
        }
        return std::nullopt;
      });

  EXPECT_TRUE(seen.contains("root/a"));
  EXPECT_FALSE(seen.contains("root/a/file"));
  EXPECT_FALSE(seen.contains("root/a/deeper"));
  EXPECT_TRUE(seen.contains("root/b/file"));
}

TEST(directory_walker_test, reject_zero_limits) {
  test::os_access_mock os;
  EXPECT_THROW((internal::directory_walker(os, {.num_workers = 0})),
               std::invalid_argument);
  EXPECT_THROW((internal::directory_walker(
                   os, {.num_workers = 2, .max_pending_entries = 0})),
               std::invalid_argument);
}

TEST(directory_walker_test, visitor_exception_joins_workers) {
  test::os_access_mock os;
  populate(os);
  internal::directory_walker walker(
      os, {.num_workers = 3, .max_pending_entries = 2});
  EXPECT_THROW(
      walker.run("root", 0,
                 [&](internal::walk_entry const&, int) -> std::optional<int> {
                   throw std::runtime_error("stop");
                 }),
      std::runtime_error);
}

TEST(directory_walker_test, optional_metadata) {
  test::os_access_mock os;
  populate(os);
  internal::directory_walker walker(
      os, {.num_workers = 2, .collect_metadata = false});
  size_t count = 0;

  walker.run("root", 0,
             [&](internal::walk_entry const& e, int) -> std::optional<int> {
               ++count;
               EXPECT_FALSE(e.metadata);
               if (e.path == "root/a" || e.path == "root/b") {
                 return 1;
               }
               return std::nullopt;
             });

  EXPECT_EQ(6, count);
}

TEST(directory_walker_test, metadata_failure_preserves_entry_and_continues) {
  for (size_t workers : {1, 3}) {
    failing_os_access os(injected_failure::metadata);
    populate(os);
    internal::directory_walker walker(
        os,
        {.num_workers = workers, .max_pending_entries = 1, .batch_size = 1});
    std::set<fs::path> seen;
    bool saw_metadata_error = false;

    walker.run(
        "root", fs::path("root"),
        [&](internal::walk_entry const& e,
            fs::path const&) -> std::optional<fs::path> {
          seen.insert(e.path);
          EXPECT_TRUE(e.metadata.has_value());
          if (!e.metadata) {
            return std::nullopt;
          }
          if (e.path == "root/a") {
            EXPECT_FALSE(e.metadata->has_value());
            EXPECT_EQ(
                std::make_error_code(std::errc::no_such_file_or_directory),
                e.metadata->error());
            saw_metadata_error = true;
            return std::nullopt;
          }
          EXPECT_TRUE(e.metadata->has_value());
          if (e.metadata->has_value() && e.metadata->value().is_directory()) {
            return e.path;
          }
          return std::nullopt;
        });

    EXPECT_TRUE(saw_metadata_error);
    EXPECT_TRUE(seen.contains("root/top"));
    EXPECT_TRUE(seen.contains("root/b/file"));
    EXPECT_FALSE(seen.contains("root/a/file"));
  }
}

TEST(directory_walker_test, enumeration_failure_delivered_on_completion) {
  for (size_t workers : {1, 3}) {
    failing_os_access os(injected_failure::enumeration);
    populate(os);
    internal::directory_walker walker(os, {.num_workers = workers});
    size_t seen = 0;
    bool got_error = false;

    walker.run(
        "root", 0,
        [&](internal::walk_entry const&, int) -> std::optional<int> {
          ++seen;
          return std::nullopt;
        },
        [&](fs::path const& path, int, std::error_code ec) {
          EXPECT_EQ("root", path);
          EXPECT_EQ(std::make_error_code(std::errc::permission_denied), ec);
          got_error = true;
        });

    EXPECT_EQ(1, seen);
    EXPECT_TRUE(got_error);
  }
}

TEST(directory_walker_test, unexpected_worker_exception_propagates) {
  for (size_t workers : {1, 3}) {
    failing_os_access os(injected_failure::unexpected);
    populate(os);
    internal::directory_walker walker(
        os, {.num_workers = workers, .max_pending_entries = 1});
    EXPECT_THROW(
        walker.run("root", 0,
                   [](internal::walk_entry const&, int) -> std::optional<int> {
                     return std::nullopt;
                   }),
        std::logic_error);
  }
}

TEST(directory_walker_test, same_walker_different_context_types) {
  test::os_access_mock os;
  populate(os);
  internal::directory_walker walker(os, {.num_workers = 3});
  std::size_t count_int = 0;
  std::size_t count_path = 0;

  walker.run(
      "root", 0,
      [&](internal::walk_entry const& e, int depth) -> std::optional<int> {
        ++count_int;
        if (e.metadata && e.metadata->has_value() &&
            e.metadata->value().is_directory()) {
          return depth + 1;
        }
        return std::nullopt;
      });

  walker.run("root", fs::path("root"),
             [&](internal::walk_entry const& e,
                 fs::path const& parent) -> std::optional<fs::path> {
               ++count_path;
               EXPECT_EQ(parent, e.path.parent_path());
               if (e.metadata && e.metadata->has_value() &&
                   e.metadata->value().is_directory()) {
                 return e.path;
               }
               return std::nullopt;
             });

  EXPECT_EQ(7, count_int);
  EXPECT_EQ(count_int, count_path);
}

TEST(directory_walker_test, move_only_callbacks) {
  test::os_access_mock os;
  populate(os);
  internal::directory_walker walker(os, {.num_workers = 3});
  int visited = 0;
  int completed = 0;

  walker.run(
      "root", 0,
      [owned = std::make_unique<int>(42), &visited](
          internal::walk_entry const& e, int depth) -> std::optional<int> {
        EXPECT_EQ(42, *owned);
        ++visited;
        if (e.metadata && e.metadata->has_value() &&
            e.metadata->value().is_directory()) {
          return depth + 1;
        }
        return std::nullopt;
      },
      [owned = std::make_unique<int>(17), &completed](fs::path const&, int,
                                                      std::error_code ec) {
        EXPECT_EQ(17, *owned);
        EXPECT_FALSE(ec);
        ++completed;
      });

  EXPECT_EQ(7, visited);
  EXPECT_EQ(4, completed);
}

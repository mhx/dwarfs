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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/reader/internal/lru_cache.h>

namespace {

using dwarfs::reader::internal::lru_cache;
using unique_str_ptr = std::unique_ptr<std::string>;

} // namespace

// Test for integral keys and regular types (e.g., std::string)
TEST(lru_cache_test, insert_and_retrieve_with_integral_key) {
  lru_cache<int, std::string> cache(3);

  cache.set(1, "one");
  cache.set(2, "two");
  cache.set(3, "three");

  // Retrieve and verify
  ASSERT_EQ(cache.find(1)->second, "one");
  ASSERT_EQ(cache.find(2)->second, "two");
  ASSERT_EQ(cache.find(3)->second, "three");
}

TEST(lru_cache_test, insert_eviction_with_integral_key) {
  lru_cache<int, std::string> cache(3);

  cache.set(1, "one");
  cache.set(2, "two");
  cache.set(3, "three");

  // Evict least recently used (key 1)
  cache.set(4, "four");

  // Verify eviction
  ASSERT_EQ(cache.find(1), cache.end());
  ASSERT_EQ(cache.find(2)->second, "two");
  ASSERT_EQ(cache.find(3)->second, "three");
  ASSERT_EQ(cache.find(4)->second, "four");
}

TEST(lru_cache_test, find_with_promotion) {
  lru_cache<int, std::string> cache(3);

  cache.set(1, "one");
  cache.set(2, "two");
  cache.set(3, "three");

  // Access item to promote
  cache.find(2);

  // Add a new item, evicting the least recently used (key 1)
  cache.set(4, "four");

  // Verify promotion and eviction
  ASSERT_EQ(cache.find(2)->second, "two");
  ASSERT_EQ(cache.find(1), cache.end());
  ASSERT_EQ(cache.find(3)->second, "three");
  ASSERT_EQ(cache.find(4)->second, "four");
}

TEST(lru_cache_test, prune_hook) {
  lru_cache<int, std::string> cache(3);
  std::vector<std::pair<int, std::string>> evicted_items;

  // Set a prune hook to capture evicted keys
  cache.set_prune_hook([&evicted_items](int key, std::string&& value) {
    evicted_items.emplace_back(key, std::move(value));
  });

  cache.set(1, "one");
  cache.set(2, "two");
  cache.set(3, "three");
  cache.set(4, "four");

  // Verify that the least recently used key is evicted
  ASSERT_EQ(evicted_items.size(), 1);
  EXPECT_EQ(evicted_items[0].first, 1);
  EXPECT_EQ(evicted_items[0].second, "one");
}

TEST(lru_cache_test, unique_ptr_key_type) {
  lru_cache<int, unique_str_ptr> cache(3);

  cache.set(1, std::make_unique<std::string>("one"));
  cache.set(2, std::make_unique<std::string>("two"));
  cache.set(3, std::make_unique<std::string>("three"));

  // Retrieve and verify unique_ptr values
  auto val1 = std::move(cache.find(1)->second);
  auto val2 = std::move(cache.find(2)->second);
  auto val3 = std::move(cache.find(3)->second);

  ASSERT_EQ(*val1, "one");
  ASSERT_EQ(*val2, "two");
  ASSERT_EQ(*val3, "three");

  // Add a new item, evicting the least recently used (key 1)
  cache.set(4, std::make_unique<std::string>("four"));

  // Verify eviction of key 1
  ASSERT_EQ(cache.find(1), cache.end());
}

TEST(lru_cache_test, unique_ptr_eviction) {
  lru_cache<int, unique_str_ptr> cache(3);
  std::vector<std::pair<int, unique_str_ptr>> evicted_items;

  // Set a prune hook to capture evicted values
  cache.set_prune_hook([&evicted_items](int key, unique_str_ptr&& value) {
    evicted_items.emplace_back(key, std::move(value));
  });

  cache.set(1, std::make_unique<std::string>("one"));
  cache.set(2, std::make_unique<std::string>("two"));
  cache.set(3, std::make_unique<std::string>("three"));
  cache.set(4, std::make_unique<std::string>("four"));

  // Verify that the least recently used key (key 1) was evicted
  ASSERT_EQ(evicted_items.size(), 1);
  EXPECT_EQ(evicted_items[0].first, 1);
  EXPECT_EQ(*evicted_items[0].second, "one");
}

TEST(lru_cache_test, clear_cache) {
  lru_cache<int, std::string> cache(3);

  cache.set(1, "one");
  cache.set(2, "two");
  cache.set(3, "three");

  // Clear the cache
  cache.clear();

  // Verify that the cache is empty
  ASSERT_TRUE(cache.empty());
  ASSERT_EQ(cache.size(), 0);
}

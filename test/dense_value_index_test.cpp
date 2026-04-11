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

#include <cctype>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/dense_value_index.h>
#include <dwarfs/internal/flat_dense_value_index.h>

using namespace dwarfs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;

namespace {

template <template <typename> typename Policy>
struct policy_wrapper {
  template <typename T>
  using policy = Policy<T>;
};

struct throwing_index_set_control {
  static inline bool fail_next_insert = false;
};

template <typename Hash, typename Equal>
class throwing_index_set {
 public:
  using underlying_type = std::unordered_set<std::size_t, Hash, Equal>;
  using value_type = typename underlying_type::value_type;
  using iterator = typename underlying_type::iterator;
  using const_iterator = typename underlying_type::const_iterator;

  throwing_index_set(std::size_t bucket_count, Hash hash, Equal equal)
      : set_(bucket_count, std::move(hash), std::move(equal)) {}

  std::pair<iterator, bool> insert(value_type value) {
    if (throwing_index_set_control::fail_next_insert) {
      throwing_index_set_control::fail_next_insert = false;
      throw std::runtime_error("injected insert failure");
    }
    return set_.insert(value);
  }

  void clear() { set_.clear(); }

  void reserve(std::size_t n) { set_.reserve(n); }

  iterator end() noexcept { return set_.end(); }
  const_iterator end() const noexcept { return set_.end(); }

  template <typename K>
  iterator find(K const& key) {
    return set_.find(key);
  }

  template <typename K>
  const_iterator find(K const& key) const {
    return set_.find(key);
  }

 private:
  underlying_type set_;
};

template <typename T>
struct throwing_insert_policy : std_dense_value_index_policy<T> {
  template <typename Hash, typename Equal>
  using index_type = throwing_index_set<Hash, Equal>;
};

struct throwing_value {
  static inline bool fail_construction = false;

  int value;

  explicit throwing_value(int v)
      : value(v) {
    if (fail_construction) {
      throw std::runtime_error("injected construction failure");
    }
  }
};

struct throwing_value_hash {
  using is_transparent = void;

  std::size_t operator()(throwing_value const& v) const noexcept {
    return std::hash<int>{}(v.value);
  }

  std::size_t operator()(int v) const noexcept { return std::hash<int>{}(v); }
};

struct throwing_value_equal {
  using is_transparent = void;

  bool operator()(throwing_value const& lhs,
                  throwing_value const& rhs) const noexcept {
    return lhs.value == rhs.value;
  }

  bool operator()(throwing_value const& lhs, int rhs) const noexcept {
    return lhs.value == rhs;
  }

  bool operator()(int lhs, throwing_value const& rhs) const noexcept {
    return lhs == rhs.value;
  }
};

template <typename T>
struct throwing_value_policy {
  static_assert(std::same_as<T, throwing_value>);

  using store_type = std::vector<T>;
  using hash_type = throwing_value_hash;
  using equal_type = throwing_value_equal;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

struct ascii_case_hash {
  using is_transparent = void;

  std::size_t operator()(std::string_view sv) const noexcept {
    std::size_t h = 1469598103934665603ULL;
    for (unsigned char c : sv) {
      auto const lower = static_cast<unsigned char>(std::tolower(c));
      h ^= lower;
      h *= 1099511628211ULL;
    }
    return h;
  }

  std::size_t operator()(std::string const& s) const noexcept {
    return (*this)(std::string_view{s});
  }
};

struct ascii_case_equal {
  using is_transparent = void;

  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
      auto const a = static_cast<unsigned char>(lhs[i]);
      auto const b = static_cast<unsigned char>(rhs[i]);
      if (std::tolower(a) != std::tolower(b)) {
        return false;
      }
    }

    return true;
  }

  bool
  operator()(std::string const& lhs, std::string const& rhs) const noexcept {
    return (*this)(std::string_view{lhs}, std::string_view{rhs});
  }

  bool operator()(std::string const& lhs, std::string_view rhs) const noexcept {
    return (*this)(std::string_view{lhs}, rhs);
  }

  bool operator()(std::string_view lhs, std::string const& rhs) const noexcept {
    return (*this)(lhs, std::string_view{rhs});
  }
};

template <typename T>
struct ascii_case_string_policy {
  static_assert(std::same_as<T, std::string>);

  using store_type = std::vector<T>;
  using hash_type = ascii_case_hash;
  using equal_type = ascii_case_equal;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

template <class T>
class append_only_store {
 public:
  using value_type = T;

  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

  template <class... Args>
  decltype(auto) emplace_back(Args&&... args) {
    return values_.emplace_back(std::forward<Args>(args)...);
  }

  [[nodiscard]] T& operator[](std::size_t index) noexcept {
    return values_[index];
  }
  [[nodiscard]] T const& operator[](std::size_t index) const noexcept {
    return values_[index];
  }

  [[nodiscard]] T& at(std::size_t index) { return values_.at(index); }
  [[nodiscard]] T const& at(std::size_t index) const {
    return values_.at(index);
  }

  [[nodiscard]] std::vector<T> const& values() const noexcept {
    return values_;
  }

 private:
  std::vector<T> values_;
};

template <class T>
struct append_only_store_policy {
  using store_type = append_only_store<T>;
  using hash_type = default_value_hash<T>;
  using equal_type = std::equal_to<>;
  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

struct counted_string {
  std::string value;

  counted_string() = default;
  counted_string(char const* s)
      : value(s) {}
  counted_string(std::string s)
      : value(std::move(s)) {}

  friend bool
  operator==(counted_string const&, counted_string const&) = default;
};

} // namespace

template <>
struct std::hash<counted_string> {
  std::size_t operator()(counted_string const& v) const noexcept {
    return std::hash<std::string_view>{}(v.value);
  }
};

using tested_policy_wrappers =
    ::testing::Types<policy_wrapper<std_dense_value_index_policy>,
                     policy_wrapper<internal::flat_dense_value_index_policy>>;

template <typename PolicyWrapper>
class dense_value_index_string_policy_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<std::string, PolicyWrapper::template policy>;

  std::vector<std::string> store;
  index_type index{store};

  [[nodiscard]] typename index_type::insert_result insert_alpha() {
    return index.emplace("alpha");
  }

  [[nodiscard]] typename index_type::insert_result insert_beta() {
    return index.emplace("beta");
  }

  void expect_alpha_beta() const {
    EXPECT_EQ(index.size(), 2);
    EXPECT_THAT(store, ElementsAre("alpha", "beta"));
    EXPECT_THAT(index[0], Eq("alpha"));
    EXPECT_THAT(index[1], Eq("beta"));
  }
};

TYPED_TEST_SUITE(dense_value_index_string_policy_test, tested_policy_wrappers);

TYPED_TEST(dense_value_index_string_policy_test, starts_empty) {
  EXPECT_TRUE(this->index.empty());
  EXPECT_EQ(this->index.size(), 0);
  EXPECT_THAT(this->store, ElementsAre());
}

TYPED_TEST(dense_value_index_string_policy_test,
           assigns_dense_indices_to_new_values) {
  auto const first = this->insert_alpha();
  auto const second = this->insert_beta();

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);

  this->expect_alpha_beta();
}

TYPED_TEST(dense_value_index_string_policy_test,
           duplicate_insert_returns_existing_index_without_duplication) {
  auto const first = this->insert_alpha();
  auto const duplicate = this->index.emplace(std::string{"alpha"});

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(this->index.size(), 1);
  EXPECT_THAT(this->store, ElementsAre("alpha"));
}

TYPED_TEST(dense_value_index_string_policy_test,
           supports_heterogeneous_lookup_for_string_values) {
  this->index.add("alpha");
  this->index.add("beta");

  std::string const alpha = "alpha";
  std::string_view const beta = "beta";
  std::string_view const missing = "missing";

  EXPECT_TRUE(this->index.contains(alpha));
  EXPECT_TRUE(this->index.contains(beta));
  EXPECT_FALSE(this->index.contains(missing));

  EXPECT_THAT(this->index.index_of(alpha), Optional(Eq(0)));
  EXPECT_THAT(this->index.index_of(beta), Optional(Eq(1)));
  EXPECT_THAT(this->index.index_of(missing), Eq(std::nullopt));
}

TYPED_TEST(dense_value_index_string_policy_test, reserve_preserves_behavior) {
  this->index.reserve(64);

  EXPECT_EQ(this->index.size(), 0);

  auto const a = this->index.add("alpha");
  auto const b = this->index.add("beta");
  auto const a2 = this->index.add("alpha");

  EXPECT_THAT(a, Eq(0));
  EXPECT_THAT(b, Eq(1));
  EXPECT_THAT(a2, Eq(0));

  this->expect_alpha_beta();
}

TYPED_TEST(dense_value_index_string_policy_test,
           add_is_a_thin_wrapper_over_emplace) {
  auto const a = this->index.add("alpha");
  auto const b = this->index.add("beta");
  auto const a2 = this->index.add("alpha");

  EXPECT_THAT(a, Eq(0));
  EXPECT_THAT(b, Eq(1));
  EXPECT_THAT(a2, Eq(0));

  this->expect_alpha_beta();
}

TYPED_TEST(dense_value_index_string_policy_test,
           values_returns_the_external_store_contents) {
  this->index.add("alpha");
  this->index.add("beta");

  EXPECT_THAT(this->index.values(), ElementsAre("alpha", "beta"));
  EXPECT_EQ(&this->index.values(),
            &static_cast<std::vector<std::string> const&>(this->store));
}

TYPED_TEST(dense_value_index_string_policy_test,
           can_rebuild_index_from_existing_unique_store) {
  std::vector<std::string> rebuilt_store = {"alpha", "beta", "gamma"};

  basic_dense_value_index<std::string, TypeParam::template policy>
      rebuilt_index(rebuilt_store);

  EXPECT_EQ(rebuilt_index.size(), 3);
  EXPECT_THAT(rebuilt_index[0], Eq("alpha"));
  EXPECT_THAT(rebuilt_index[1], Eq("beta"));
  EXPECT_THAT(rebuilt_index[2], Eq("gamma"));
  EXPECT_THAT(rebuilt_index.index_of(std::string_view{"alpha"}),
              Optional(Eq(0)));
  EXPECT_THAT(rebuilt_index.index_of(std::string_view{"beta"}),
              Optional(Eq(1)));
  EXPECT_THAT(rebuilt_index.index_of(std::string_view{"gamma"}),
              Optional(Eq(2)));
}

TYPED_TEST(dense_value_index_string_policy_test,
           rejects_duplicate_values_in_existing_store) {
  std::vector<std::string> rebuilt_store = {"alpha", "beta", "alpha"};

  EXPECT_THROW(
      (basic_dense_value_index<std::string, TypeParam::template policy>(
          rebuilt_store)),
      std::invalid_argument);
}

template <typename PolicyWrapper>
class dense_value_index_size_t_policy_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<std::size_t, PolicyWrapper::template policy>;

  std::vector<std::size_t> store;
  index_type index{store};
};

TYPED_TEST_SUITE(dense_value_index_size_t_policy_test, tested_policy_wrappers);

TYPED_TEST(dense_value_index_size_t_policy_test,
           works_when_value_type_is_size_t) {
  auto const first = this->index.emplace(42);
  auto const second = this->index.emplace(7);
  auto const duplicate = this->index.emplace(42);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(this->index.size(), 2);
  EXPECT_THAT(this->store, ElementsAre(42, 7));
  EXPECT_THAT(this->index[0], Eq(42));
  EXPECT_THAT(this->index[1], Eq(7));
  EXPECT_THAT(this->index.index_of(42), Optional(Eq(0)));
  EXPECT_THAT(this->index.index_of(7), Optional(Eq(1)));
  EXPECT_THAT(this->index.index_of(99), Eq(std::nullopt));
}

class case_insensitive_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<std::string, ascii_case_string_policy>;

  std::vector<std::string> store;
  index_type index{store};
};

TEST_F(case_insensitive_index_test, supports_custom_case_insensitive_policy) {
  auto const first = index.emplace("Alpha");
  auto const second = index.emplace("BETA");
  auto const duplicate = index.emplace("alpha");

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(index.size(), 2);
  EXPECT_THAT(store, ElementsAre("Alpha", "BETA"));
  EXPECT_TRUE(index.contains(std::string_view{"ALPHA"}));
  EXPECT_TRUE(index.contains(std::string_view{"beta"}));
  EXPECT_THAT(index.index_of(std::string_view{"aLpHa"}), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(std::string_view{"BeTa"}), Optional(Eq(1)));
}

class throwing_value_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<throwing_value, throwing_value_policy>;

  void SetUp() override { throwing_value::fail_construction = false; }

  std::vector<throwing_value> store;
  index_type index{store};
};

TEST_F(throwing_value_index_test,
       emplace_has_strong_guarantee_when_value_construction_throws) {
  EXPECT_THAT(index.add(1), Eq(0));
  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(index.index_of(1), Optional(Eq(0)));

  throwing_value::fail_construction = true;
  EXPECT_THROW(index.emplace(2), std::runtime_error);
  throwing_value::fail_construction = false;

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size(), 1);
  EXPECT_THAT(store[0].value, Eq(1));
  EXPECT_THAT(index.index_of(1), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(2), Eq(std::nullopt));
}

class throwing_insert_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<std::string, throwing_insert_policy>;

  void SetUp() override {
    throwing_index_set_control::fail_next_insert = false;
  }

  std::vector<std::string> store;
  index_type index{store};
};

TEST_F(throwing_insert_index_test,
       emplace_has_strong_guarantee_when_index_insertion_throws) {
  EXPECT_THAT(index.add("alpha"), Eq(0));
  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre("alpha"));

  throwing_index_set_control::fail_next_insert = true;
  EXPECT_THROW(index.emplace("beta"), std::runtime_error);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre("alpha"));
  EXPECT_THAT(index.index_of(std::string_view{"alpha"}), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Eq(std::nullopt));
}

TYPED_TEST(dense_value_index_string_policy_test,
           at_returns_value_for_valid_index) {
  this->index.add("alpha");
  this->index.add("beta");

  EXPECT_THAT(this->index.at(0), Eq("alpha"));
  EXPECT_THAT(this->index.at(1), Eq("beta"));
}

TYPED_TEST(dense_value_index_string_policy_test,
           at_throws_for_out_of_range_index) {
  this->index.add("alpha");

  EXPECT_THROW(static_cast<void>(this->index.at(1)), std::out_of_range);
}

class append_only_string_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_value_index<std::string, append_only_store_policy>;

  append_only_store<std::string> store;
  index_type index{store};
};

TEST_F(append_only_string_index_test,
       inserts_new_values_using_append_only_store) {
  auto const first = index.emplace("alpha");
  auto const second = index.emplace("beta");

  EXPECT_EQ(first.index, 0);
  EXPECT_TRUE(first.inserted);
  EXPECT_EQ(second.index, 1);
  EXPECT_TRUE(second.inserted);

  EXPECT_EQ(index.size(), 2);
  EXPECT_THAT(store.values(), ElementsAre("alpha", "beta"));
  EXPECT_THAT(index[0], Eq("alpha"));
  EXPECT_THAT(index[1], Eq("beta"));
}

TEST_F(append_only_string_index_test,
       duplicate_insert_returns_existing_index_without_appending) {
  auto const first = index.emplace("alpha");
  auto const duplicate = index.emplace(std::string{"alpha"});

  EXPECT_EQ(first.index, 0);
  EXPECT_TRUE(first.inserted);
  EXPECT_EQ(duplicate.index, 0);
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store.values(), ElementsAre("alpha"));
}

TEST_F(append_only_string_index_test,
       supports_heterogeneous_lookup_on_append_only_store_path) {
  index.add("alpha");
  index.add("beta");

  EXPECT_TRUE(index.contains(std::string_view{"alpha"}));
  EXPECT_TRUE(index.contains(std::string_view{"beta"}));
  EXPECT_FALSE(index.contains(std::string_view{"gamma"}));

  EXPECT_THAT(index.index_of(std::string_view{"alpha"}), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Optional(Eq(1)));
  EXPECT_THAT(index.index_of(std::string_view{"gamma"}), Eq(std::nullopt));
}

TEST_F(append_only_string_index_test, at_works_with_append_only_store) {
  index.add("alpha");
  index.add("beta");

  EXPECT_THAT(index.at(0), Eq("alpha"));
  EXPECT_THAT(index.at(1), Eq("beta"));
  EXPECT_THROW(static_cast<void>(index.at(2)), std::out_of_range);
}

TEST(dense_value_index_append_only_test,
     duplicate_value_is_still_stored_only_once) {
  append_only_store<counted_string> store;
  basic_dense_value_index<counted_string, append_only_store_policy> index(
      store);

  auto const first = index.emplace("alpha");
  auto const duplicate = index.emplace("alpha");

  EXPECT_EQ(first.index, 0);
  EXPECT_TRUE(first.inserted);
  EXPECT_EQ(duplicate.index, 0);
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size(), 1);
  EXPECT_THAT(store[0].value, Eq("alpha"));
}

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

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <dwarfs/detail/string_like_hash.h>

namespace dwarfs {

template <typename T>
struct default_value_hash : std::hash<T> {};

template <typename Char, typename Traits, typename Alloc>
struct default_value_hash<std::basic_string<Char, Traits, Alloc>>
    : detail::basic_string_like_hash<std::basic_string<Char, Traits, Alloc>> {};

template <typename T>
struct dense_value_index_policy_base {
  using store_type = std::vector<T>;
  using hash_type = default_value_hash<T>;
  using equal_type = std::equal_to<>;
};

template <typename T>
struct std_dense_value_index_policy : dense_value_index_policy_base<T> {
  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

template <typename T, template <typename> typename Policy>
class basic_dense_value_index {
 public:
  using policy_type = Policy<T>;
  using value_type = T;
  using size_type = std::size_t;
  using store_type = typename policy_type::store_type;
  using hash_type = typename policy_type::hash_type;
  using equal_type = typename policy_type::equal_type;

  struct insert_result {
    size_type index;
    bool inserted;
  };

 private:
  template <class U>
  struct probe_key {
    U const* value;
  };

  template <class U>
  static constexpr bool is_compatible_probe =
      std::invocable<hash_type const&, U const&> &&
      std::predicate<equal_type const&, value_type const&, U const&> &&
      std::predicate<equal_type const&, U const&, value_type const&>;

  class indirect_hash {
   public:
    using is_transparent = void;

    indirect_hash(hash_type hash, store_type const* store)
        : hash_{std::move(hash)}
        , store_{store} {}

    std::size_t operator()(size_type index) const
        noexcept(noexcept(std::invoke(hash_, (*store_)[index]))) {
      return std::invoke(hash_, (*store_)[index]);
    }

    template <class U>
      requires(is_compatible_probe<U>)
    std::size_t operator()(probe_key<U> probe) const
        noexcept(noexcept(std::invoke(hash_, *probe.value))) {
      return std::invoke(hash_, *probe.value);
    }

   private:
    [[no_unique_address]] hash_type hash_;
    store_type const* store_;
  };

  class indirect_equal {
   public:
    using is_transparent = void;

    indirect_equal(equal_type equal, store_type const* store)
        : equal_{std::move(equal)}
        , store_{store} {}

    bool operator()(size_type lhs, size_type rhs) const
        noexcept(noexcept(std::invoke(equal_, (*store_)[lhs],
                                      (*store_)[rhs]))) {
      return lhs == rhs || std::invoke(equal_, (*store_)[lhs], (*store_)[rhs]);
    }

    template <class U>
      requires(is_compatible_probe<U>)
    bool operator()(size_type lhs, probe_key<U> rhs) const
        noexcept(noexcept(std::invoke(equal_, (*store_)[lhs], *rhs.value))) {
      return std::invoke(equal_, (*store_)[lhs], *rhs.value);
    }

    template <class U>
      requires(is_compatible_probe<U>)
    bool operator()(probe_key<U> lhs, size_type rhs) const
        noexcept(noexcept(std::invoke(equal_, *lhs.value, (*store_)[rhs]))) {
      return std::invoke(equal_, *lhs.value, (*store_)[rhs]);
    }

   private:
    [[no_unique_address]] equal_type equal_;
    store_type const* store_;
  };

  using index_set_type =
      typename policy_type::template index_type<indirect_hash, indirect_equal>;

  class pop_back_guard {
   public:
    explicit pop_back_guard(store_type* store) noexcept
        : store_{store} {}

    pop_back_guard(pop_back_guard const&) = delete;
    pop_back_guard& operator=(pop_back_guard const&) = delete;

    ~pop_back_guard() {
      if (active_) {
        store_->pop_back();
      }
    }

    void release() noexcept { active_ = false; }

   private:
    store_type* store_;
    bool active_ = true;
  };

 public:
  explicit basic_dense_value_index(store_type& store,
                                   equal_type equal = equal_type{},
                                   hash_type hash = hash_type{})
      : store_{&store}
      , index_(0, indirect_hash(std::move(hash), std::addressof(store)),
               indirect_equal(std::move(equal), std::addressof(store))) {
    rebuild_index();
  }

  [[nodiscard]] size_type size() const noexcept { return store_->size(); }

  [[nodiscard]] bool empty() const noexcept { return store_->empty(); }

  void reserve(size_type n) {
    if constexpr (requires(store_type& s) { s.reserve(n); }) {
      store_->reserve(n);
    }
    index_.reserve(n);
  }

  [[nodiscard]] value_type const& operator[](size_type index) const noexcept {
    return (*store_)[index];
  }

  [[nodiscard]] value_type const& at(size_type index) const {
    return (*store_).at(index);
  }

  [[nodiscard]] store_type const& values() const noexcept { return *store_; }

  template <class U>
    requires(is_compatible_probe<U>)
  [[nodiscard]] std::optional<size_type> index_of(U const& value) const {
    auto const it = index_.find(probe_key<U>{std::addressof(value)});
    if (it == index_.end()) {
      return std::nullopt;
    }
    return *it;
  }

  template <class U>
    requires(is_compatible_probe<U>)
  [[nodiscard]] bool contains(U const& value) const {
    return index_.find(probe_key<U>{std::addressof(value)}) != index_.end();
  }

  template <class... Args>
  insert_result emplace(Args&&... args) {
    auto const new_index = store_->size();

    if constexpr (requires(store_type& s) { s.pop_back(); }) {
      store_->emplace_back(std::forward<Args>(args)...);
      pop_back_guard rollback(store_);

      auto const [it, inserted] = index_.insert(new_index);
      if (inserted) {
        rollback.release();
        return {.index = new_index, .inserted = true};
      }

      return {.index = *it, .inserted = false};
    } else {
      // Fallback for append-only stores without pop_back().
      //
      // Duplicate detection is performed before mutating the store.
      // If store_->emplace_back() succeeds and index_.insert() throws,
      // the strong exception guarantee cannot be preserved. However,
      // that is likely only going to happen if the index runs out of
      // memory. Exceptions thrown by the value's constructor or move
      // constructor as well as by appending to the store will still
      // leave the index in a valid state.

      auto tmp = value_type(std::forward<Args>(args)...);

      if (auto const ix = index_of(tmp)) {
        return {.index = *ix, .inserted = false};
      }

      store_->emplace_back(std::move(tmp));
      [[maybe_unused]] auto const [it, inserted] = index_.insert(new_index);

      assert(inserted);
      assert(*it == new_index);

      return {.index = new_index, .inserted = true};
    }
  }

  template <class... Args>
  size_type add(Args&&... args) {
    return emplace(std::forward<Args>(args)...).index;
  }

 private:
  void rebuild_index() {
    index_.clear();
    index_.reserve(store_->size());

    for (size_type i = 0; i < store_->size(); ++i) {
      if (!index_.insert(i).second) {
        throw std::invalid_argument("basic_dense_value_index requires the "
                                    "initial store to contain unique values");
      }
    }
  }

  store_type* store_{nullptr};
  index_set_type index_;
};

template <typename T>
using dense_value_index =
    basic_dense_value_index<T, std_dense_value_index_policy>;

} // namespace dwarfs

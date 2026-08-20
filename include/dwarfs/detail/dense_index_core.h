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

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace dwarfs::detail {

struct identity_key_projection {
  template <typename U>
  [[nodiscard]] constexpr U&& operator()(U&& value) const noexcept {
    return std::forward<U>(value);
  }
};

struct pair_first_projection {
  template <typename U>
  [[nodiscard]] constexpr decltype(auto) operator()(U&& value) const noexcept {
    return (std::forward<U>(value).first);
  }
};

struct pair_second_projection {
  template <typename U>
  [[nodiscard]] constexpr decltype(auto) operator()(U&& value) const noexcept {
    return (std::forward<U>(value).second);
  }
};

// Minimal requirements for a store that a dense index can be built over.
template <typename Store>
concept dense_index_store = requires(Store const& store, std::size_t index) {
  typename Store::reference;
  typename Store::const_reference;
  { store.size() } -> std::convertible_to<std::size_t>;
  { store[index] } -> std::same_as<typename Store::const_reference>;
};

template <typename Projection, typename ElementReference>
using projected_reference_t = std::conditional_t<
    std::is_reference_v<ElementReference>,
    std::invoke_result_t<Projection const&, ElementReference>,
    std::remove_cvref_t<
        std::invoke_result_t<Projection const&, ElementReference>>>;

template <typename T, typename KeyProjection,
          template <typename> typename Policy>
class dense_index_core {
  static_assert(std::is_empty_v<KeyProjection>);
  static_assert(std::is_default_constructible_v<KeyProjection>);

 public:
  using policy_type = Policy<T>;
  using value_type = T;
  using key_projection_type = KeyProjection;
  using size_type = std::size_t;
  using store_type = typename policy_type::store_type;
  using hash_type = typename policy_type::hash_type;
  using equal_type = typename policy_type::equal_type;
  using value_reference = typename store_type::reference;
  using const_value_reference = typename store_type::const_reference;

  static_assert(dense_index_store<store_type>,
                "the store must provide reference and const_reference member "
                "types, size(), and a const subscript operator returning "
                "const_reference");

  using key_type =
      std::remove_cvref_t<std::invoke_result_t<KeyProjection const&, T const&>>;

  template <typename Projection>
  using projected_reference =
      projected_reference_t<Projection, value_reference>;

  template <typename Projection>
  using const_projected_reference =
      projected_reference_t<Projection, const_value_reference>;

  using const_key_reference = const_projected_reference<KeyProjection>;

  struct insert_result {
    size_type index;
    bool inserted;
  };

 private:
  [[nodiscard]] static constexpr decltype(auto)
  project_key(auto&& value) noexcept {
    return KeyProjection{}(std::forward<decltype(value)>(value));
  }

  template <typename U>
  struct probe_key {
    U const* value;
  };

  static constexpr bool store_has_pop_back =
      requires(store_type& s) { s.pop_back(); };

 public:
  template <typename U>
  static constexpr bool is_compatible_probe =
      std::invocable<hash_type const&, U const&> &&
      std::predicate<equal_type const&, key_type const&, U const&> &&
      std::predicate<equal_type const&, U const&, key_type const&>;

 private:
  class indirect_hash {
   public:
    using is_transparent = void;

    // phmap::parallel_flat_hash_set default-constructs its inner sets
    // before assigning the real ones...
    indirect_hash()
      requires(std::default_initializable<hash_type>)
    = default;

    indirect_hash(hash_type hash, store_type const* store)
        : hash_{std::move(hash)}
        , store_{store} {}

    std::size_t operator()(size_type index) const
        noexcept(noexcept(std::invoke(hash_, project_key((*store_)[index])))) {
      assert(store_);
      return std::invoke(hash_, project_key((*store_)[index]));
    }

    template <typename U>
      requires(is_compatible_probe<U>)
    std::size_t operator()(probe_key<U> probe) const
        noexcept(noexcept(std::invoke(hash_, *probe.value))) {
      return std::invoke(hash_, *probe.value);
    }

   private:
    [[no_unique_address]] hash_type hash_{};
    store_type const* store_{nullptr};
  };

  class indirect_equal {
   public:
    using is_transparent = void;

    // phmap::parallel_flat_hash_set default-constructs its inner sets
    // before assigning the real ones...
    indirect_equal()
      requires(std::default_initializable<equal_type>)
    = default;

    indirect_equal(equal_type equal, store_type const* store)
        : equal_{std::move(equal)}
        , store_{store} {}

    bool operator()(size_type lhs, size_type rhs) const
        noexcept(noexcept(std::invoke(equal_, project_key((*store_)[lhs]),
                                      project_key((*store_)[rhs])))) {
      assert(store_);
      return lhs == rhs || std::invoke(equal_, project_key((*store_)[lhs]),
                                       project_key((*store_)[rhs]));
    }

    template <typename U>
      requires(is_compatible_probe<U>)
    bool operator()(size_type lhs, probe_key<U> rhs) const
        noexcept(noexcept(std::invoke(equal_, project_key((*store_)[lhs]),
                                      *rhs.value))) {
      assert(store_);
      return std::invoke(equal_, project_key((*store_)[lhs]), *rhs.value);
    }

    template <typename U>
      requires(is_compatible_probe<U>)
    bool operator()(probe_key<U> lhs, size_type rhs) const
        noexcept(noexcept(std::invoke(equal_, *lhs.value,
                                      project_key((*store_)[rhs])))) {
      assert(store_);
      return std::invoke(equal_, *lhs.value, project_key((*store_)[rhs]));
    }

   private:
    [[no_unique_address]] equal_type equal_{};
    store_type const* store_{nullptr};
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

  // Append-only stores cannot roll back
  class no_rollback_guard {
   public:
    explicit no_rollback_guard(store_type*) noexcept {}

    no_rollback_guard(no_rollback_guard const&) = delete;
    no_rollback_guard& operator=(no_rollback_guard const&) = delete;

    void release() noexcept {}
  };

  using rollback_guard =
      std::conditional_t<store_has_pop_back, pop_back_guard, no_rollback_guard>;

 public:
  dense_index_core(store_type& store, equal_type equal, hash_type hash,
                   std::string_view context)
      : store_{&store}
      , index_(0, indirect_hash(std::move(hash), std::addressof(store)),
               indirect_equal(std::move(equal), std::addressof(store))) {
    rebuild_index(context);
  }

  [[nodiscard]] size_type size() const noexcept { return store_->size(); }

  [[nodiscard]] bool empty() const noexcept { return store_->empty(); }

  [[nodiscard]] size_type index_size_in_bytes() const noexcept {
    return index_.capacity() * sizeof(typename index_set_type::value_type);
  }

  void reserve(size_type n) {
    if constexpr (requires(store_type& s) { s.reserve(n); }) {
      store_->reserve(n);
    }
    index_.reserve(n);
  }

  [[nodiscard]] store_type const& values() const noexcept { return *store_; }

  [[nodiscard]] const_value_reference value(size_type index) const noexcept {
    return std::as_const(*store_)[index];
  }

  [[nodiscard]] const_value_reference value_at(size_type index) const {
    return std::as_const(*store_).at(index);
  }

  template <typename U>
    requires(is_compatible_probe<U>)
  [[nodiscard]] std::optional<size_type> index_of(U const& value) const {
    auto const it = index_.find(probe_key<U>{std::addressof(value)});
    if (it == index_.end()) {
      return std::nullopt;
    }
    return *it;
  }

  template <typename U>
    requires(is_compatible_probe<U>)
  [[nodiscard]] bool contains(U const& value) const {
    return index_.find(probe_key<U>{std::addressof(value)}) != index_.end();
  }

  template <typename... Args>
  insert_result emplace_value(Args&&... args) {
    auto const new_index = store_->size();

    if constexpr (store_has_pop_back) {
      store_->emplace_back(std::forward<Args>(args)...);
      rollback_guard rollback(store_);

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

      if (auto const ix = index_of(project_key(std::as_const(tmp)))) {
        return {.index = *ix, .inserted = false};
      }

      store_->emplace_back(std::move(tmp));
      [[maybe_unused]] auto const [it, inserted] = index_.insert(new_index);

      assert(inserted);
      assert(*it == new_index);

      return {.index = new_index, .inserted = true};
    }
  }

  template <typename U, typename... Args>
    requires(is_compatible_probe<U>)
  insert_result emplace_value_if_absent(U const& key, Args&&... args) {
    if (auto const it = index_.find(probe_key<U>{std::addressof(key)});
        it != index_.end()) {
      return {.index = *it, .inserted = false};
    }

    return append_value(std::forward<Args>(args)...);
  }

  template <typename... Args>
  insert_result append_value(Args&&... args) {
    auto const new_index = store_->size();

    store_->emplace_back(std::forward<Args>(args)...);
    rollback_guard rollback(store_);

    [[maybe_unused]] auto const [it, inserted] = index_.insert(new_index);
    assert(inserted);
    assert(*it == new_index);

    rollback.release();

    return {.index = new_index, .inserted = true};
  }

 protected:
  [[nodiscard]] value_reference mutable_value(size_type index) noexcept {
    return (*store_)[index];
  }

  [[nodiscard]] value_reference mutable_value_at(size_type index) {
    return store_->at(index);
  }

 private:
  void rebuild_index(std::string_view context) {
    index_.clear();
    index_.reserve(store_->size());

    for (size_type i = 0; i < store_->size(); ++i) {
      if (!index_.insert(i).second) {
        throw std::invalid_argument(
            std::string{context} +
            " requires the initial store to contain unique values");
      }
    }
  }

  store_type* store_{nullptr};
  index_set_type index_;
};

} // namespace dwarfs::detail

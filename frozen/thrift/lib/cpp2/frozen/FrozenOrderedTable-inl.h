/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <type_traits>

#include <dwarfs/thrift_lite/container.h>

namespace apache::thrift::frozen::detail {

template <typename Container>
concept OrderedAssocContainer = requires { typename Container::key_compare; } &&
    std::same_as<typename Container::key_compare,
                 std::less<typename Container::key_type>>;

static_assert(
    OrderedAssocContainer<std::set<int>>,
    "std::set should satisfy OrderedAssocContainer");

static_assert(
    !OrderedAssocContainer<std::unordered_set<int>>,
    "std::unordered_set should not satisfy OrderedAssocContainer");

/**
 * Layout specialization for unique ordered range types which support
 * binary-search based lookup.
 */
template <class T, class Item, class KeyExtractor, class Key = T>
struct SortedTableLayout : public ArrayLayout<T, Item> {
 private:
  static bool containerIsSorted(const T& cont) {
    return std::is_sorted(
        cont.begin(), cont.end(), [](const Item& a, const Item& b) {
          return KeyExtractor::getKey(a) < KeyExtractor::getKey(b);
        });
  }

 public:
  using Base = ArrayLayout<T, Item>;
  using LayoutSelf = SortedTableLayout;

  void thaw(ViewPosition self, T& out) const {
    auto v = view(self);
    out.clear();
    ::dwarfs::thrift_lite::try_reserve(out, v.size());
    for (auto it = v.begin(); it != v.end(); ++it) {
      out.insert(out.end(), it.thaw());
    }
    if constexpr (OrderedAssocContainer<T>) {
      assert(containerIsSorted(out));
    }
  }

  // provide indirect sort table if the collection isn't pre-sorted
  static void maybeIndex(const T& coll, std::vector<const Item*>& index) {
    index.clear();
    if constexpr (OrderedAssocContainer<T>) {
      assert(containerIsSorted(coll));
      return;
    } else {
      if (containerIsSorted(coll)) {
        return;
      }
      index.reserve(coll.size());
      for (decltype(auto) item : coll) {
        index.push_back(KeyExtractor::getPointer(item));
      }
      std::sort(index.begin(), index.end(), [](const Item* pa, const Item* pb) {
        return KeyExtractor::getKey(*pa) < KeyExtractor::getKey(*pb);
      });
    }
  }

  static void ensureDistinctKeys(
      const typename KeyExtractor::KeyType& key1,
      const typename KeyExtractor::KeyType& key2) {
    if (!(key1 < key2)) {
      throw std::domain_error("Input collection is not distinct");
    }
  }

  FieldPosition layoutItems(
      LayoutRoot& root,
      const T& coll,
      LayoutPosition /* self */,
      FieldPosition pos,
      LayoutPosition write,
      FieldPosition writeStep) final {
    std::vector<const Item*> index;
    maybeIndex(coll, index);

    FieldPosition noField; // not really used
    const typename KeyExtractor::KeyType* lastKey = nullptr;
    if (index.empty()) {
      // either the collection was already sorted or it's empty
      for (decltype(auto) item : coll) {
        root.layoutField(write, noField, this->itemField, item);
        write = write(writeStep);
        const typename KeyExtractor::KeyType* itemKey =
            &KeyExtractor::getKey(item);
        if (lastKey) {
          ensureDistinctKeys(*lastKey, *itemKey);
        }
        lastKey = itemKey;
      }
    } else {
      if constexpr (!OrderedAssocContainer<T>) {
        // collection was non-empty and non-sorted, needs indirection table.
        for (auto ptr : index) {
          root.layoutField(write, noField, this->itemField, *ptr);
          write = write(writeStep);
          const typename KeyExtractor::KeyType* itemKey =
              &KeyExtractor::getKey(*ptr);
          if (lastKey) {
            ensureDistinctKeys(*lastKey, *itemKey);
          }
          lastKey = itemKey;
        }
      } else {
        TL_PANIC("can't happen!");
      }
    }

    return pos;
  }

  void freezeItems(
      FreezeRoot& root,
      const T& coll,
      FreezePosition /* self */,
      FreezePosition write,
      FieldPosition writeStep) const final {
    std::vector<const Item*> index;
    maybeIndex(coll, index);

    FieldPosition noField; // not really used
    if (index.empty()) {
      // either the collection was already sorted or it's empty
      for (decltype(auto) item : coll) {
        root.freezeField(write, this->itemField, item);
        write = write(writeStep);
      }
    } else {
      // collection was non-empty and non-sorted, needs indirection table.
      for (auto ptr : index) {
        root.freezeField(write, this->itemField, *ptr);
        write = write(writeStep);
      }
    }
  }

  class View : public Base::View {
    using KeyView = typename Layout<Key>::View;
    using ItemView = typename Layout<Item>::View;

   public:
    View() = default;
    View(const LayoutSelf* layout, ViewPosition position)
        : Base::View(layout, position) {}

    using iterator = typename Base::View::iterator;

    void operator[](size_t) = delete;

    /// Returns an iterator pointing to the first element that compares not less
    /// to the value `key`. This allows finding a frozen element in the ordered
    /// table without freezing the key.
    template <typename K>
    iterator lower_bound(const K& key) const
      requires(!std::is_convertible_v<K, KeyView>)
    {
      return lower_bound_impl(key);
    }

    iterator lower_bound(const KeyView& key) const {
      return lower_bound_impl(key);
    }

    /// Returns an iterator pointing to the first element that compares greater
    /// to the value `key`. This allows finding a frozen element in the ordered
    /// table without freezing the key.
    template <typename K>
    iterator upper_bound(const K& key) const
      requires(!std::is_convertible_v<K, KeyView>)
    {
      return upper_bound_impl(key);
    }

    iterator upper_bound(const KeyView& key) const {
      return upper_bound_impl(key);
    }

    /// Returns a range containing all elements that compares equal to the value
    /// `key`. This allows finding a frozen element in the ordered table without
    /// freezing the key.
    template <typename K>
    std::pair<iterator, iterator> equal_range(const K& key) const
      requires(!std::is_convertible_v<K, KeyView>)
    {
      return equal_range_impl(key);
    }

    std::pair<iterator, iterator> equal_range(const KeyView& key) const {
      return equal_range_impl(key);
    }

    /// Finds an element with key that compares equivalent to the value `key`.
    /// This allows finding a frozen element in the ordered table without
    /// freezing the key.
    template <typename K>
    iterator find(const K& key) const
      requires(!std::is_convertible_v<K, KeyView>)
    {
      return find_impl(key);
    }

    iterator find(const KeyView& key) const { return find_impl(key); }

    /// Returns the number of elements with key that compares equivalent to the
    /// value `key`. This allows finding a frozen element in the ordered table
    /// without freezing the key.
    template <typename K>
    size_t count(const K& key) const
      requires(!std::is_convertible_v<K, KeyView>)
    {
      return count_impl(key);
    }

    size_t count(const KeyView& key) const { return count_impl(key); }

    T thaw() const {
      T ret;
      static_cast<const SortedTableLayout*>(this->layout_)
          ->thaw(this->position_, ret);
      return ret;
    }

   private:
    template <typename K>
    iterator lower_bound_impl(const K& key) const {
      return std::lower_bound(
          this->begin(), this->end(), key, [](ItemView a, const K& b) {
            return KeyExtractor::getViewKey(a) < b;
          });
    }

    template <typename K>
    iterator upper_bound_impl(const K& key) const {
      return std::upper_bound(
          this->begin(), this->end(), key, [](const K& a, ItemView b) {
            return a < KeyExtractor::getViewKey(b);
          });
    }

    template <typename K>
    std::pair<iterator, iterator> equal_range_impl(const K& key) const {
      auto found = lower_bound(key);
      if (found != this->end() && KeyExtractor::getViewKey(*found) == key) {
        auto next = found;
        return std::make_pair(found, ++next);
      }
      return std::make_pair(found, found);
    }

    template <typename K>
    iterator find_impl(const K& key) const {
      auto found = lower_bound(key);
      if (found != this->end() && KeyExtractor::getViewKey(*found) != key) {
        found = this->end();
      }
      return found;
    }

    template <typename K>
    size_t count_impl(const K& key) const {
      return find(key) == this->end() ? 0 : 1;
    }
  };

  View view(ViewPosition self) const { return View(this, self); }
};

} // namespace apache::thrift::frozen::detail

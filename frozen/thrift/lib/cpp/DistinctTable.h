/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#pragma once

#include <cassert>
#include <unordered_set>
#include <vector>

namespace apache::thrift {

template <class T>
struct BaseDistinctTablePolicy {
  template <class Hash, class Equal>
  using Index = std::unordered_set<size_t, Hash, Equal>;

  using Hash = std::hash<T>;
  using Equal = std::equal_to<T>;
  using Store = std::vector<T>;
};

/**
 * Accumulates only distinct values in an externally owned store.
 */
template <class T, template <class> class Policy = BaseDistinctTablePolicy>
class DistinctTable {
 public:
  using Store = typename Policy<T>::Store;
  using Hash = typename Policy<T>::Hash;
  using Equal = typename Policy<T>::Equal;

  explicit DistinctTable(
      Store* store, Equal equal = Equal(), Hash hash = Hash())
      : store_(store),
        indexes_(0, HashIndirect(hash, store), EqualIndirect(equal, store)) {
    assert(store->empty());
  }

  /**
   * Construct a T given the arguments, return the index at which such a value
   * can now be found, either newly or previously constructed.
   */
  template <class... Args>
  size_t add(Args&&... args) {
    auto index = store_->size();
    store_->push_back(std::forward<Args>(args)...);
    auto insertion = indexes_.insert(index);
    if (insertion.second) {
      return index;
    } else {
      store_->pop_back();
      return *insertion.first;
    }
  }

 private:
  class HashIndirect : public Hash {
   public:
    HashIndirect(Hash _hash, Store* store)
        : Hash(std::move(_hash)), store_(store) {}

    size_t operator()(size_t i) const { return Hash::operator()((*store_)[i]); }

   private:
    Store* store_;
  };

  class EqualIndirect : public Equal {
   public:
    EqualIndirect(Equal equal, Store* store)
        : Equal(std::move(equal)), store_(store) {}

    bool operator()(size_t a, size_t b) const {
      return a == b || Equal::operator()((*store_)[a], (*store_)[b]);
    }

   private:
    Store* store_;
  };

  using Index = typename Policy<T>::template Index<HashIndirect, EqualIndirect>;

  Store* store_;
  Index indexes_;
};

} // namespace apache::thrift

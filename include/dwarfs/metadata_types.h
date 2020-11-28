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

#include <cstdint>
#include <string_view>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/range/irange.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include "dwarfs/gen-cpp2/metadata_layouts.h"

namespace dwarfs {

template <typename T>
class metadata_;

class entry_view
    : public ::apache::thrift::frozen::View<thrift::metadata::entry> {
  using EntryView = ::apache::thrift::frozen::View<thrift::metadata::entry>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

 public:
  std::string_view name() const;
  uint16_t mode() const;
  uint16_t getuid() const;
  uint16_t getgid() const;

 private:
  entry_view(EntryView ev, Meta const* meta)
      : EntryView(ev)
      , meta_(meta) {}

  Meta const* meta_;
};

class directory_view {
  using EntryView = ::apache::thrift::frozen::View<thrift::metadata::entry>;
  using DirView = ::apache::thrift::frozen::View<thrift::metadata::directory>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

 public:
  uint32_t inode() const { return entry_.inode(); }
  uint32_t parent_inode() const;
  uint32_t first_entry() const;
  uint32_t entry_count() const;

  boost::integer_range<uint32_t> entry_range() const;

 private:
  directory_view(EntryView ev, Meta const* meta)
      : entry_(ev)
      , meta_(meta) {}

  DirView getdir() const;
  DirView getdir(uint32_t ino) const;
  uint32_t entry_count(DirView self) const;

  EntryView entry_;
  Meta const* meta_;
};

using chunk_view = ::apache::thrift::frozen::View<thrift::metadata::chunk>;

class chunk_range {
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

 public:
  class iterator
      : public boost::iterator_facade<iterator, chunk_view const,
                                      boost::random_access_traversal_tag> {
   public:
    iterator() = default;

    iterator(iterator const& other)
        : meta_(other.meta_)
        , it_(other.it_) {}

   private:
    friend class boost::iterator_core_access;
    friend class chunk_range;

    iterator(Meta const* meta, uint32_t it)
        : meta_(meta)
        , it_(it) {}

    bool equal(iterator const& other) const {
      return meta_ == other.meta_ && it_ == other.it_;
    }

    void increment() { ++it_; }

    void decrement() { --it_; }

    void advance(difference_type n) { it_ += n; }

    difference_type distance_to(iterator const& other) const {
      return static_cast<difference_type>(other.it_) -
             static_cast<difference_type>(it_);
    }

    chunk_view const& dereference() const {
      view_ = meta_->chunks()[it_];
      return view_;
    }

    Meta const* meta_;
    uint32_t it_{0};
    mutable chunk_view view_;
  };

  iterator begin() const { return iterator(meta_, begin_); }

  iterator end() const { return iterator(meta_, end_); }

  size_t size() const { return end_ - begin_; }

  bool empty() const { return end_ == begin_; }

 private:
  chunk_range(Meta const* meta, uint32_t begin, uint32_t end)
      : meta_(meta)
      , begin_(begin)
      , end_(end) {}

  Meta const* meta_;
  uint32_t begin_{0};
  uint32_t end_{0};
};

} // namespace dwarfs

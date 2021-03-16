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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/range/irange.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include "dwarfs/gen-cpp2/metadata_layouts.h"

namespace dwarfs {

template <typename T>
class metadata_;

class dir_entry_view;

class inode_view
    : public ::apache::thrift::frozen::View<thrift::metadata::inode_data> {
  using InodeView =
      ::apache::thrift::frozen::View<thrift::metadata::inode_data>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

  friend class dir_entry_view;

 public:
  uint16_t mode() const;
  uint16_t getuid() const;
  uint16_t getgid() const;
  uint32_t inode_num() const { return inode_num_; }

 private:
  inode_view(InodeView iv, uint32_t inode_num_, Meta const* meta)
      : InodeView{iv}
      , inode_num_{inode_num_}
      , meta_{meta} {}

  uint32_t inode_num_;
  Meta const* meta_;
};

/**
 * THIS *MUST* BE CONSTRUCTIBLE FROM ONLY AN INODE NUMBER (NOT EVEN AN
 * INODE_VIEW)
 */
class directory_view
    : public ::apache::thrift::frozen::View<thrift::metadata::directory> {
  using DirView = ::apache::thrift::frozen::View<thrift::metadata::directory>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

  friend class dir_entry_view;

 public:
  // TODO: not sure if these are needed
  uint32_t inode() const { return inode_; }
  bool is_root() const { return inode_ == 0; }

  uint32_t entry_count() const;

  boost::integer_range<uint32_t> entry_range() const;

  std::optional<directory_view> parent() const;

  uint32_t parent_inode() const;

 private:
  directory_view(uint32_t inode, Meta const* meta);

  DirView getdir(uint32_t ino) const;
  static DirView getdir(uint32_t ino, Meta const* meta);

  uint32_t inode_;
  Meta const* meta_;
};

class dir_entry_view {
  using InodeView =
      ::apache::thrift::frozen::View<thrift::metadata::inode_data>;
  using DirEntryView =
      ::apache::thrift::frozen::View<thrift::metadata::dir_entry>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

 public:
  std::string_view name() const;
  inode_view inode() const;

  bool is_root() const;

  // TODO: remove?
  // std::optional<directory_view> directory() const;
  std::optional<dir_entry_view> parent() const;

  std::string path() const;
  void append_path_to(std::string& s) const;

  uint32_t self_index() const { return self_index_; }

 private:
  dir_entry_view(DirEntryView v, uint32_t self_index, uint32_t parent_index,
                 Meta const* meta)
      : v_{v}
      , self_index_{self_index} // TODO: check if we really need this
      , parent_index_{parent_index}
      , meta_{meta} {}

  dir_entry_view(InodeView v, uint32_t self_index, uint32_t parent_index,
                 Meta const* meta)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , meta_{meta} {}

  static dir_entry_view
  from_dir_entry_index(uint32_t self_index, uint32_t parent_index,
                       Meta const* meta);
  static dir_entry_view
  from_dir_entry_index(uint32_t self_index, Meta const* meta);

  // TODO: this works, but it's strange; a limited version of dir_entry_view
  //       should work without a parent for these use cases
  static std::string_view name(uint32_t index, Meta const* meta);
  static inode_view inode(uint32_t index, Meta const* meta);

  std::variant<DirEntryView, InodeView> v_;
  uint32_t self_index_, parent_index_;
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

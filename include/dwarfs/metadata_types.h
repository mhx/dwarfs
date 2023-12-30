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
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/range/irange.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include "dwarfs/file_stat.h"
#include "dwarfs/file_type.h"
#include "dwarfs/string_table.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"

namespace dwarfs {

template <typename T>
class metadata_;

class dir_entry_view;
class logger;

enum class readlink_mode {
  raw,
  preferred,
  unix,
};

class global_metadata {
 public:
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  global_metadata(logger& lgr, Meta const& meta);

  static void check_consistency(logger& lgr, Meta const& meta);
  void check_consistency(logger& lgr) const;

  Meta const& meta() const { return meta_; }

  uint32_t first_dir_entry(uint32_t ino) const;
  uint32_t parent_dir_entry(uint32_t ino) const;

  string_table const& names() const { return names_; }

  std::vector<thrift::metadata::directory> const& directories() const {
    return directories_storage_;
  }

 private:
  Meta const& meta_;
  std::vector<thrift::metadata::directory> const directories_storage_;
  thrift::metadata::directory const* const directories_;
  string_table const names_;
};

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
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;

  mode_type mode() const;
  std::string mode_string() const;
  std::string perm_string() const;
  posix_file_type::value type() const {
    return posix_file_type::from_mode(mode());
  }
  bool is_regular_file() const { return type() == posix_file_type::regular; }
  bool is_directory() const { return type() == posix_file_type::directory; }
  bool is_symlink() const { return type() == posix_file_type::symlink; }
  uid_type getuid() const;
  gid_type getgid() const;
  uint32_t inode_num() const { return inode_num_; }

 private:
  inode_view(InodeView iv, uint32_t inode_num_, Meta const& meta)
      : InodeView{iv}
      , inode_num_{inode_num_}
      , meta_{&meta} {}

  uint32_t inode_num_;
  Meta const* meta_;
};

class directory_view {
  using DirView = ::apache::thrift::frozen::View<thrift::metadata::directory>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class metadata_;

  friend class dir_entry_view;

 public:
  uint32_t inode() const { return inode_; }
  uint32_t parent_inode() const;

  uint32_t first_entry() const { return first_entry(inode_); }
  uint32_t parent_entry() const { return parent_entry(inode_); }
  uint32_t entry_count() const;
  boost::integer_range<uint32_t> entry_range() const;

 private:
  directory_view(uint32_t inode, global_metadata const& g)
      : inode_{inode}
      , g_{&g} {}

  uint32_t first_entry(uint32_t ino) const;
  uint32_t parent_entry(uint32_t ino) const;

  uint32_t inode_;
  global_metadata const* g_;
};

class dir_entry_view {
  using InodeView =
      ::apache::thrift::frozen::View<thrift::metadata::inode_data>;
  using DirEntryView =
      ::apache::thrift::frozen::View<thrift::metadata::dir_entry>;

  template <typename T>
  friend class metadata_;

 public:
  std::string name() const;
  inode_view inode() const;

  bool is_root() const;

  std::optional<dir_entry_view> parent() const;

  std::string path() const;
  std::string unix_path() const;
  std::filesystem::path fs_path() const;
  std::wstring wpath() const;

  void append_to(std::filesystem::path& p) const;

  uint32_t self_index() const { return self_index_; }

 private:
  dir_entry_view(DirEntryView v, uint32_t self_index, uint32_t parent_index,
                 global_metadata const& g)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g} {}

  dir_entry_view(InodeView v, uint32_t self_index, uint32_t parent_index,
                 global_metadata const& g)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g} {}

  static dir_entry_view
  from_dir_entry_index(uint32_t self_index, uint32_t parent_index,
                       global_metadata const& g);
  static dir_entry_view
  from_dir_entry_index(uint32_t self_index, global_metadata const& g);

  // TODO: this works, but it's strange; a limited version of dir_entry_view
  //       should work without a parent for these use cases
  static std::string name(uint32_t index, global_metadata const& g);
  static inode_view inode(uint32_t index, global_metadata const& g);

  std::variant<DirEntryView, InodeView> v_;
  uint32_t self_index_, parent_index_;
  global_metadata const* g_;
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
        : meta_{meta}
        , it_{it} {}

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

  chunk_view operator[](uint32_t index) const { return meta_->chunks()[index]; }

 private:
  chunk_range(Meta const& meta, uint32_t begin, uint32_t end)
      : meta_(&meta)
      , begin_(begin)
      , end_(end) {}

  Meta const* meta_;
  uint32_t begin_{0};
  uint32_t end_{0};
};

} // namespace dwarfs

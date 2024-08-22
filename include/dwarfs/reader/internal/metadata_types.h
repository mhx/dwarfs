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
#include <string>
#include <variant>

#include <boost/iterator/iterator_facade.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_type.h>

#include <dwarfs/internal/string_table.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>

namespace dwarfs {

class logger;

namespace reader::internal {

template <typename T>
class metadata_;

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
  uint32_t self_dir_entry(uint32_t ino) const;

  dwarfs::internal::string_table const& names() const { return names_; }

  std::vector<thrift::metadata::directory> const& directories() const {
    return directories_;
  }

 private:
  Meta const& meta_;
  std::vector<thrift::metadata::directory> const directories_;
  std::vector<uint32_t> const dir_self_index_;
  dwarfs::internal::string_table const names_;
};

class inode_view_impl
    : public ::apache::thrift::frozen::View<thrift::metadata::inode_data> {
  using InodeView =
      ::apache::thrift::frozen::View<thrift::metadata::inode_data>;
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

 public:
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;

  inode_view_impl(InodeView iv, uint32_t inode_num_, Meta const& meta)
      : InodeView{iv}
      , inode_num_{inode_num_}
      , meta_{&meta} {}

  mode_type mode() const;
  std::string mode_string() const;
  std::string perm_string() const;
  posix_file_type::value type() const {
    return posix_file_type::from_mode(mode());
  }
  uid_type getuid() const;
  gid_type getgid() const;
  uint32_t inode_num() const { return inode_num_; }
  bool is_directory() const { return type() == posix_file_type::directory; }

 private:
  uint32_t inode_num_;
  Meta const* meta_;
};

class dir_entry_view_impl {
 public:
  using InodeView =
      ::apache::thrift::frozen::View<thrift::metadata::inode_data>;
  using DirEntryView =
      ::apache::thrift::frozen::View<thrift::metadata::dir_entry>;

  dir_entry_view_impl(DirEntryView v, uint32_t self_index,
                      uint32_t parent_index, global_metadata const& g)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g} {}

  dir_entry_view_impl(InodeView v, uint32_t self_index, uint32_t parent_index,
                      global_metadata const& g)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g} {}

  static std::shared_ptr<dir_entry_view_impl>
  from_dir_entry_index_shared(uint32_t self_index, uint32_t parent_index,
                              global_metadata const& g);
  static std::shared_ptr<dir_entry_view_impl>
  from_dir_entry_index_shared(uint32_t self_index, global_metadata const& g);

  static dir_entry_view_impl
  from_dir_entry_index(uint32_t self_index, uint32_t parent_index,
                       global_metadata const& g);
  static dir_entry_view_impl
  from_dir_entry_index(uint32_t self_index, global_metadata const& g);

  // TODO: this works, but it's strange; a limited version of
  // dir_entry_view_impl
  //       should work without a parent for these use cases
  static std::string name(uint32_t index, global_metadata const& g);
  static std::shared_ptr<inode_view_impl>
  inode_shared(uint32_t index, global_metadata const& g);

  std::string name() const;
  std::shared_ptr<inode_view_impl> inode_shared() const;
  inode_view_impl inode() const;

  bool is_root() const;

  std::shared_ptr<dir_entry_view_impl> parent() const;

  std::string path() const;
  std::string unix_path() const;
  std::filesystem::path fs_path() const;
  std::wstring wpath() const;

  void append_to(std::filesystem::path& p) const;

  uint32_t self_index() const { return self_index_; }

 private:
  template <template <typename...> class Ctor>
  auto make_inode() const;

  template <template <typename...> class Ctor>
  static auto make_dir_entry_view(uint32_t self_index, uint32_t parent_index,
                                  global_metadata const& g);

  template <template <typename...> class Ctor>
  static auto
  make_dir_entry_view(uint32_t self_index, global_metadata const& g);

  std::variant<DirEntryView, InodeView> v_;
  uint32_t self_index_, parent_index_;
  global_metadata const* g_;
};

using chunk_view = ::apache::thrift::frozen::View<thrift::metadata::chunk>;

class chunk_range {
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  template <typename T>
  friend class internal::metadata_;

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

    // TODO: this is nasty; can we do this without boost::iterator_facade?
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
  chunk_range() = default;

  chunk_range(Meta const& meta, uint32_t begin, uint32_t end)
      : meta_(&meta)
      , begin_(begin)
      , end_(end) {}

  Meta const* meta_{nullptr};
  uint32_t begin_{0};
  uint32_t end_{0};
};

} // namespace reader::internal

} // namespace dwarfs

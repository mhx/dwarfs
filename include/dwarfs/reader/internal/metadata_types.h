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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

#include <boost/iterator/iterator_facade.hpp>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <dwarfs/file_stat.h>
#include <dwarfs/file_type.h>
#include <dwarfs/metadata_defs.h>
#include <dwarfs/types.h>

#include <dwarfs/internal/packed_ptr.h>
#include <dwarfs/internal/string_table.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>

namespace dwarfs {

class logger;

namespace reader::internal {

class metadata_v2_data;

class global_metadata {
 public:
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  using directories_view = ::apache::thrift::frozen::Layout<
      std::vector<thrift::metadata::directory>>::View;
  using bundled_directories_view =
      ::apache::thrift::frozen::Bundled<directories_view>;

  global_metadata(logger& lgr, Meta const& meta);

  static void check_consistency(logger& lgr, Meta const& meta);
  void check_consistency(logger& lgr) const;

  Meta const& meta() const { return meta_; }

  uint32_t first_dir_entry(uint32_t ino) const;
  uint32_t parent_dir_entry(uint32_t ino) const;
  uint32_t self_dir_entry(uint32_t ino) const;

  dwarfs::internal::string_table const& names() const { return names_; }

  std::optional<directories_view> bundled_directories() const;

 private:
  Meta const& meta_;
  std::optional<bundled_directories_view> const bundled_directories_;
  directories_view const directories_;
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

  enum class entry_name_type : uint8_t {
    other,
    self,
    parent,
  };

  dir_entry_view_impl(DirEntryView v, uint32_t self_index,
                      uint32_t parent_index, global_metadata const& g,
                      entry_name_type name_type)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g, name_type} {}

  dir_entry_view_impl(InodeView v, uint32_t self_index, uint32_t parent_index,
                      global_metadata const& g, entry_name_type name_type)
      : v_{v}
      , self_index_{self_index}
      , parent_index_{parent_index}
      , g_{&g, name_type} {}

  static std::shared_ptr<dir_entry_view_impl> from_dir_entry_index_shared(
      uint32_t self_index, uint32_t parent_index, global_metadata const& g,
      entry_name_type name_type = entry_name_type::other);
  static std::shared_ptr<dir_entry_view_impl> from_dir_entry_index_shared(
      uint32_t self_index, global_metadata const& g,
      entry_name_type name_type = entry_name_type::other);

  static dir_entry_view_impl
  from_dir_entry_index(uint32_t self_index, uint32_t parent_index,
                       global_metadata const& g,
                       entry_name_type name_type = entry_name_type::other);

  static std::string name(uint32_t index, global_metadata const& g);

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
  uint32_t parent_index() const { return parent_index_; }

 private:
  template <template <typename...> class Ctor>
  auto make_inode() const;

  template <template <typename...> class Ctor>
  static auto
  make_dir_entry_view(uint32_t self_index, uint32_t parent_index,
                      global_metadata const& g, entry_name_type name_type);

  template <template <typename...> class Ctor>
  static auto make_dir_entry_view(uint32_t self_index, global_metadata const& g,
                                  entry_name_type name_type);

  std::variant<DirEntryView, InodeView> v_;
  uint32_t self_index_, parent_index_;
  dwarfs::internal::packed_ptr<global_metadata const, 2, entry_name_type> const
      g_;
};

class chunk_view {
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

 public:
  chunk_view() = default;
  chunk_view(Meta const* meta,
             ::apache::thrift::frozen::View<thrift::metadata::chunk> v) {
    auto const b = v.block();
    auto const o = v.offset();
    auto const s = v.size();
    auto const hole_ix = meta->hole_block_index();

    if (hole_ix.has_value() && b == *hole_ix) { // this is a hole
      block_ = 0;
      offset_ = 0;
      if (o == kChunkOffsetIsLargeHole) {
        assert(meta->large_hole_size().has_value());
        assert(s < meta->large_hole_size()->size());
        bits_ = (*meta->large_hole_size())[s];
      } else {
        auto const block_size = meta->block_size();
        assert(std::has_single_bit(block_size));
        assert(o < block_size);
        bits_ = (static_cast<uint64_t>(s) * block_size) + o;
      }
      bits_ |= kChunkBitsHoleBit;
    } else { // this is data
      block_ = b;
      offset_ = o;
      bits_ = s;
    }
  }

  bool is_data() const { return (bits_ & kChunkBitsHoleBit) == 0; }

  bool is_hole() const {
    return (bits_ & kChunkBitsHoleBit) == kChunkBitsHoleBit;
  }

  uint32_t block() const {
    assert(is_data());
    return block_;
  }

  uint32_t offset() const {
    assert(is_data());
    return offset_;
  }

  file_off_t size() const { return bits_ & kChunkBitsSizeMask; }

 private:
  uint32_t block_{0};
  uint32_t offset_{0};
  uint64_t bits_{0};
};

class chunk_range {
  using Meta =
      ::apache::thrift::frozen::MappedFrozen<thrift::metadata::metadata>;

  friend class internal::metadata_v2_data;

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
      view_ = chunk_view(meta_, meta_->chunks()[it_]);
      return view_;
    }

    Meta const* meta_;
    uint32_t it_{0};
    mutable chunk_view view_;
  };

  iterator begin() const { return {meta_, begin_}; }

  iterator end() const { return {meta_, end_}; }

  size_t size() const { return end_ - begin_; }

  bool empty() const { return end_ == begin_; }

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

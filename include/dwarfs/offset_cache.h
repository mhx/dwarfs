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

#include <algorithm>
#include <cassert>
#include <memory>
#include <mutex>
#include <ostream>
#include <span>
#include <vector>

#include <folly/container/EvictingCacheMap.h>
#include <folly/small_vector.h>

namespace dwarfs {

template <typename InodeT, typename FileOffsetT, typename ChunkIndexT,
          size_t ChunkIndexInterval, size_t UpdaterMaxInlineOffsets>
class basic_offset_cache {
 public:
  using inode_type = InodeT;
  using file_offset_type = FileOffsetT;
  using chunk_index_type = ChunkIndexT;

  static constexpr size_t const chunk_index_interval = ChunkIndexInterval;

  class updater;

  class chunk_offsets {
   public:
    chunk_offsets(chunk_index_type total_chunks) {
      offsets_.reserve(total_chunks / chunk_index_interval - 1);
    }

    void update(chunk_index_type first_index,
                std::span<file_offset_type const> offsets,
                chunk_index_type chunk_index, file_offset_type file_offset,
                file_offset_type chunk_size) {
      std::lock_guard lock(mx_);

      last_chunk_index_ = chunk_index;
      last_file_offset_ = file_offset;
      last_chunk_size_ = chunk_size;

      if (first_index + offsets.size() > offsets_.size()) {
        assert(first_index <= offsets_.size());
        auto new_offsets = offsets.subspan(offsets_.size() - first_index);
        std::copy(new_offsets.begin(), new_offsets.end(),
                  std::back_inserter(offsets_));
      }
    }

    void update(updater const& upd, chunk_index_type chunk_index,
                file_offset_type file_offset, file_offset_type chunk_size) {
      update(upd.first_index(), upd.offsets(), chunk_index, file_offset,
             chunk_size);
    }

    std::pair<chunk_index_type, file_offset_type>
    find(file_offset_type offset, updater& upd) {
      std::lock_guard lock(mx_);

      upd.set_first_index(offsets_.size());

      if (last_file_offset_ <= offset &&
          offset <= last_file_offset_ + last_chunk_size_) {
        // this is likely a sequential read
        return {last_chunk_index_, last_file_offset_};
      }

      if (!offsets_.empty()) {
        chunk_index_type best_index = offsets_.size();

        if (offset < offsets_.back()) {
          auto it = std::lower_bound(offsets_.begin(), offsets_.end(), offset);

          if (it != offsets_.end()) {
            best_index = std::distance(offsets_.begin(), it);
          }
        }

        if (best_index > 0) {
          return {chunk_index_interval * best_index, offsets_[best_index - 1]};
        }
      }

      return {0, 0};
    }

    void dump(std::ostream& os) const {
      std::vector<file_offset_type> offsets;
      {
        std::lock_guard lock(mx_);
        offsets = offsets_;
      }
      for (auto off : offsets) {
        os << "  " << off << "\n";
      }
    }

   private:
    std::mutex mutable mx_;
    chunk_index_type last_chunk_index_{0};
    file_offset_type last_file_offset_{0};
    file_offset_type last_chunk_size_{0};
    std::vector<file_offset_type> offsets_;
  };

  using value_type = std::shared_ptr<chunk_offsets>;

  class updater {
   public:
    static constexpr size_t const max_inline_offsets = UpdaterMaxInlineOffsets;

    void set_first_index(chunk_index_type first_ix) { first_index_ = first_ix; }

    void add_offset(chunk_index_type index, file_offset_type offset) {
      if (index < chunk_index_interval || index % chunk_index_interval != 0)
#if __has_cpp_attribute(likely)
          [[likely]]
#endif
      {
        return;
      }

      auto ix = index / chunk_index_interval - 1;
      assert(ix <= first_index_ + offsets_.size());

      if (ix == first_index_ + offsets_.size()) {
        offsets_.push_back(offset);
      }
    }

    chunk_index_type first_index() const { return first_index_; }

    std::span<file_offset_type const> offsets() const { return offsets_; }

   private:
    folly::small_vector<file_offset_type, max_inline_offsets> offsets_;
    chunk_index_type first_index_{0};
  };

  basic_offset_cache(size_t cache_size)
      : cache_{cache_size} {}

  value_type find(inode_type inode, chunk_index_type num_chunks) const {
    {
      std::lock_guard lock(mx_);

      if (auto it = cache_.find(inode); it != cache_.end()) {
        return it->second;
      }
    }

    return std::make_shared<chunk_offsets>(num_chunks);
  }

  void set(inode_type inode, value_type value) {
    std::lock_guard lock(mx_);
    cache_.set(inode, std::move(value));
  }

  void dump(std::ostream& os) const {
    std::vector<std::pair<typename cache_type::key_type,
                          typename cache_type::mapped_type>>
        contents;

    {
      std::lock_guard lock(mx_);
      std::copy(cache_.begin(), cache_.end(), std::back_inserter(contents));
    }

    for (auto const& [inode, ent] : contents) {
      os << "inode " << inode << ":\n";
      ent->dump(os);
    }
  }

 private:
  using cache_type = folly::EvictingCacheMap<inode_type, value_type>;

  cache_type mutable cache_;
  std::mutex mutable mx_;
};

} // namespace dwarfs

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

#include <condition_variable>
#include <map>
#include <mutex>

#include <cstring>

#include <sparsehash/dense_hash_map>

#include <folly/Format.h>

#include "dwarfs/block_manager.h"
#include "dwarfs/file_interface.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_hasher.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {
// TODO: for now, this has to work better...
// we lose this hash value, but that's life...
const uint32_t blockhash_emtpy = 0;

struct bm_stats {
  size_t total_hashes{0};
  size_t collisions{0};
  size_t real_matches{0};
  size_t bad_matches{0};
  size_t saved_bytes{0};
  size_t largest_block{0};
};
} // namespace

template <typename Key, typename T>
struct block_hashes {
  using blockhash_t = google::dense_hash_map<Key, T>;

  block_hashes(size_t size)
      : size(size) {
    values.set_empty_key(blockhash_emtpy);
  }

  const size_t size;
  blockhash_t values;
};

template <typename LoggerPolicy>
class block_manager_ : public block_manager::impl {
 private:
  using cyclic_hash_t = uint32_t;
  using offset_t = uint32_t;
  using block_hashes_t = block_hashes<cyclic_hash_t, offset_t>;
  using bhv_ri = std::vector<block_hashes_t>::reverse_iterator;
  using hasher_type = inode_hasher<LoggerPolicy, cyclic_hash_t>;
  using hash_map_type = typename hasher_type::result_type;

  struct match_window {
    match_window()
        : first(0)
        , last(0) {}

    match_window(size_t first, size_t last)
        : first(first)
        , last(last) {}

    size_t size() const { return last - first; }

    size_t first;
    size_t last;
  };

  template <typename T>
  static std::vector<T> sorted(const std::vector<T>& vec) {
    std::vector<T> tmp(vec);
    std::sort(tmp.begin(), tmp.end());
    return tmp;
  }

 public:
  block_manager_(logger& lgr, progress& prog, const block_manager::config& cfg,
                 std::shared_ptr<os_access> os, filesystem_writer& fsw)
      : cfg_(cfg)
      , block_size_(static_cast<size_t>(1) << cfg.block_size_bits)
      , blockhash_window_size_(sorted(cfg.blockhash_window_size))
      , fsw_(fsw)
      , os_(os)
      , current_block_(0)
      , total_blocks_size_(0)
      , hasher_(lgr, byte_hasher_, blockhash_window_size_)
      , log_(lgr)
      , prog_(prog) {
    block_.reserve(block_size_);

    for (auto size : blockhash_window_size_) {
      block_hashes_.emplace_back(size);
    }
  }

  ~block_manager_() noexcept override {}

  void add_inode(std::shared_ptr<inode> ino) override;
  void finish_blocks() override;

  size_t total_size() const override { return total_blocks_size_; }

  size_t total_blocks() const override {
    return total_blocks_size_ > 0 ? current_block_ + 1 : 0;
  }

 private:
  size_t cur_offset() const { return block_.size(); }

  void block_ready();
  void update_hashes(const hash_map_type& hm, size_t offset, size_t size);
  void add_chunk(const std::shared_ptr<inode>& ino, const uint8_t* p,
                 size_t offset, size_t size, const hash_map_type* hm);
  void add_data(const std::shared_ptr<inode>& ino, const uint8_t* p,
                size_t size, const hash_map_type* hm = nullptr);
  void segment_and_add_data(const hash_map_type& hm,
                            const std::shared_ptr<inode>& ino,
                            const uint8_t* data, size_t size);
  void
  segment_and_add_data(const std::string& indent, const hash_map_type& hm,
                       const std::shared_ptr<inode>& ino, const uint8_t* data,
                       match_window mw, bhv_ri bhcur, bhv_ri bhend);
  bool get_match_window(const std::string& indent, match_window& win,
                        size_t& block_offset, const uint8_t* data,
                        const match_window& search_win) const;

  const block_manager::config& cfg_;
  const size_t block_size_;
  std::vector<size_t> blockhash_window_size_;
  filesystem_writer& fsw_;
  std::shared_ptr<os_access> os_;
  size_t current_block_;
  size_t total_blocks_size_;
  std::vector<uint8_t> block_;
  std::vector<block_hashes_t> block_hashes_;
  byte_hash<cyclic_hash_t> byte_hasher_;
  hasher_type hasher_;
  log_proxy<LoggerPolicy> log_;
  progress& prog_;
  std::map<size_t, bm_stats> stats_;
};

block_manager::config::config()
    : window_increment_shift(1)
    , memory_limit(256 << 20)
    , block_size_bits(22) {}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::finish_blocks() {
  if (!block_.empty()) {
    block_ready();
  }

  for (const auto& sti : stats_) {
    static char const* const percent = "{:.2%}%";
    const auto& st = sti.second;
    log_.debug() << "blockhash window <" << sti.first << ">: " << st.collisions
                 << " collisions ("
                 << folly::sformat(percent,
                                   float(st.collisions) / st.total_hashes)
                 << "), " << st.real_matches << " real matches, "
                 << st.bad_matches << " bad matches, ("
                 << folly::sformat(percent,
                                   float(st.bad_matches) /
                                       (st.real_matches + st.bad_matches))
                 << "), " << size_with_unit(st.saved_bytes)
                 << " saved (largest=" << size_with_unit(st.largest_block)
                 << ")";
  }
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::block_ready() {
  std::vector<uint8_t> tmp;
  block_.swap(tmp);
  fsw_.write_block(std::move(tmp));
  block_.reserve(block_size_);
  for (auto& bh : block_hashes_) {
    bh.values.clear();
  }
  ++current_block_;
  ++prog_.block_count;
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::update_hashes(const hash_map_type& hm,
                                                 size_t offset, size_t size) {
  size_t block_offset = cur_offset();

  for (auto bhi = block_hashes_.begin(); bhi != block_hashes_.end(); ++bhi) {
    if (bhi->size <= size) {
      auto hmi = hm.find(bhi->size);
      auto& stats = stats_[bhi->size];

      if (hmi == hm.end()) {
        throw std::runtime_error(
            "internal error: no hash found for window size");
      }

      const auto& hashvec = hmi->second;
      size_t incr = bhi->size >> cfg_.window_increment_shift;
      size_t last = (size - bhi->size) + 1;

      if (hashvec.size() < offset + last) {
        log_.error() << "bhi=" << bhi->size
                     << ", hashvec.size()=" << hashvec.size()
                     << ", offset=" << offset << ", last=" << last;
        throw std::runtime_error("internal error: hashvec too small");
      }

      for (size_t off = 0; off < last; off += incr) {
        cyclic_hash_t hval = hashvec[offset + off];

        ++stats.total_hashes;

        if (hval != blockhash_emtpy) {
          auto i = bhi->values.find(hval);

          if (i != bhi->values.end()) {
            log_.trace() << "collision for hash=" << hval
                         << " (size=" << bhi->size << "): " << i->second
                         << " <-> " << block_offset + off;
            ++stats.collisions;
          }

          bhi->values[hval] = block_offset + off;
        }
      }
    }
  }
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::add_chunk(const std::shared_ptr<inode>& ino,
                                             const uint8_t* p, size_t offset,
                                             size_t size,
                                             const hash_map_type* hm) {
  log_.trace() << "block " << current_block_ << " size: " << block_.size()
               << " of " << block_size_;

  if (hm) {
    update_hashes(*hm, offset, size);
  }

  size_t block_offset = cur_offset();

  log_.trace() << "adding chunk for inode " << ino->num() << " ["
               << ino->any()->name() << "] - block: " << current_block_
               << " offset: " << block_offset << ", size: " << size;

  block_.resize(block_offset + size);

  ::memcpy(&block_[block_offset], p + offset, size);

  ino->add_chunk(current_block_, block_offset, size);
  prog_.chunk_count++;
  prog_.filesystem_size += size;

  if (block_.size() == block_size_) {
    block_ready();
  }
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::add_data(const std::shared_ptr<inode>& ino,
                                            const uint8_t* p, size_t size,
                                            const hash_map_type* hm) {
  size_t offset = 0;

  while (size > 0) {
    size_t block_offset = cur_offset();

    if (block_offset == block_size_) {
      // apparently we've filled a block, so a new one will be created once
      // we're adding another chunk
      block_offset = 0;
    }

    if (block_offset + size <= block_size_) {
      add_chunk(ino, p, offset, size, hm);
      break;
    } else {
      size_t chunksize = block_size_ - block_offset;
      add_chunk(ino, p, offset, chunksize, hm);
      offset += chunksize;
      size -= chunksize;
    }
  }
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::add_inode(std::shared_ptr<inode> ino) {
  const file_interface* e = ino->any();
  size_t size = e->size();

  if (size > 0) {
    auto mm = os_->map_file(e->path(), size);

    log_.trace() << "adding inode " << ino->num() << " [" << ino->any()->name()
                 << "] - size: " << size;

    if (blockhash_window_size_.empty() or
        size < blockhash_window_size_.front()) {
      // no point dealing with hashes, just write it out
      add_data(ino, mm->as<uint8_t>(), size);
    } else {
      const uint8_t* data = mm->as<uint8_t>();
      hash_map_type hm;

      hasher_(hm, data, size);
      segment_and_add_data(hm, ino, data, size);
    }
  }
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::segment_and_add_data(
    const hash_map_type& hm, const std::shared_ptr<inode>& ino,
    const uint8_t* data, size_t size) {
  ;
  segment_and_add_data("", hm, ino, data, match_window(0, size),
                       block_hashes_.rbegin(), block_hashes_.rend());
}

template <typename LoggerPolicy>
void block_manager_<LoggerPolicy>::segment_and_add_data(
    const std::string& indent, const hash_map_type& hm,
    const std::shared_ptr<inode>& ino, const uint8_t* data, match_window mw,
    bhv_ri bhcur, bhv_ri bhend) {
  log_.trace() << indent << "segment_and_add_data([" << mw.first << ", "
               << mw.last << "], " << (bhcur != bhend ? bhcur->size : 0) << ")";

  while (bhcur != bhend) {
    log_.trace() << indent << "   wsize=" << bhcur->size;

    if (bhcur->size <= mw.size()) {
      auto hmi = hm.find(bhcur->size);
      auto& stats = stats_[bhcur->size];

      if (hmi == hm.end()) {
        throw std::runtime_error(
            "internal error: no hash found for window size");
      }

      const auto& hashvec = hmi->second;

      for (size_t off = mw.first; off <= mw.last - bhcur->size;) {
        match_window best;
        size_t best_offset = 0;

        auto bhi = bhcur->values.find(hashvec[off]);

        if (bhi != bhcur->values.end()) {
          log_.trace() << indent << "potentially matched " << bhcur->size
                       << " bytes at offset " << off
                       << ", hash=" << hashvec[off];

          match_window win(off, off + bhcur->size);
          size_t block_offset = bhi->second;

          if (get_match_window(indent, win, block_offset, data, mw)) {
            log_.trace() << indent << "definitely matched " << win.size()
                         << " bytes at offset " << win.first;
            ++stats.real_matches;

            // fuck yeah, we've got a block...

            if (win.size() > best.size()) {
              best = win;
              best_offset = block_offset;
            }
          } else {
            log_.trace() << indent << "bad match: " << bhcur->size
                         << " bytes at offset " << off
                         << ", hash=" << hashvec[off];
            ++stats.bad_matches;
          }
        }

        if (best.size() > 0) {
          log_.trace() << indent << "mw=[" << mw.first << ", " << mw.last
                       << "], best=[" << best.first << ", " << best.last << "]";

          // 1) search for smaller blocks on the left recursively
          match_window left(mw.first, best.first);
          log_.trace() << indent << "left=[" << left.first << ", " << left.last
                       << "]";

          // 2) save the block number before recursing, as current_block_
          //    may change before we add the chunk
          size_t block_no = current_block_;

          // 3) divide and conquer!
          segment_and_add_data(indent + "  ", hm, ino, data, left, bhcur + 1,
                               bhend);

          // 4) add the block we found
          log_.trace() << "adding (existing) chunk for inode " << ino->num()
                       << " [" << ino->any()->name()
                       << "] - offset: " << best_offset
                       << ", size: " << best.size();

          ino->add_chunk(block_no, best_offset, best.size());
          prog_.chunk_count++;
          prog_.saved_by_segmentation += best.size();
          stats.saved_bytes += best.size();
          if (best.size() > stats.largest_block) {
            stats.largest_block = best.size();
          }

          // 5) continue to look for more blocks on the right and
          //    make sure that we never go back to the left again
          mw.first = best.last;
          off = mw.first;
          log_.trace() << indent << "updated mw=[" << mw.first << ", "
                       << mw.last << "]";
        } else {
          ++off;
        }
      }
    }

    ++bhcur;
  }

  add_data(ino, data + mw.first, mw.size(), &hm);
}

template <typename LoggerPolicy>
bool block_manager_<LoggerPolicy>::get_match_window(
    const std::string& indent, match_window& win, size_t& block_offset,
    const uint8_t* data, const match_window& search_win) const {
  const uint8_t* blockdata = &block_[0];

  log_.trace() << indent << "match(block_offset=" << block_offset
               << ", window=[" << win.first << ", " << win.last
               << "], search_win=[" << search_win.first << ", "
               << search_win.last << "])";

  if (block_offset + win.size() > block_size_) {
    log_.trace() << indent << "bad match size";
    return false;
  }

  if (::memcmp(blockdata + block_offset, data + win.first, win.size()) != 0) {
    log_.trace() << indent << "block data mismatch";
    return false;
  }

  // Looking good! Let's see how much we can get out of it...

  while (block_offset + win.size() < block_size_ and
         win.last < search_win.last and
         block_offset + win.size() < block_.size() and
         blockdata[block_offset + win.size()] == data[win.last]) {
    ++win.last;
  }

  while (win.first > search_win.first and block_offset > 0 and
         blockdata[block_offset - 1] == data[win.first - 1]) {
    --block_offset;
    --win.first;
  }

  return true;
}

block_manager::block_manager(logger& lgr, progress& prog, const config& cfg,
                             std::shared_ptr<os_access> os,
                             filesystem_writer& fsw)
    : impl_(make_unique_logging_object<impl, block_manager_, logger_policies>(
          lgr, prog, cfg, os, fsw)) {}
} // namespace dwarfs

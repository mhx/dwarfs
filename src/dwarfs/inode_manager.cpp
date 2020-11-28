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

#include <algorithm>
#include <numeric>
#include <vector>

#include "dwarfs/config.h"
#include "dwarfs/entry.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/script.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

class inode_manager_ : public inode_manager {
 private:
  class inode_ : public inode {
   public:
    using chunk_type = thrift::metadata::chunk;

    void set_num(uint32_t num) override { num_ = num; }
    uint32_t num() const override { return num_; }
    uint32_t similarity_hash() const override {
      if (!file_) {
        throw std::runtime_error("inode has no file");
      }
      return file_->similarity_hash();
    }

    size_t size() const override { return any()->size(); }

    void set_file(const file* f) override {
      if (file_) {
        throw std::runtime_error("file already set for inode");
      }
      file_ = f;
    }

    void add_chunk(size_t block, size_t offset, size_t size) override {
      chunk_type c;
      c.block = block;
      c.offset = offset;
      c.size = size;
      chunks_.push_back(c);
    }

    std::string path() const override { return any()->path(); }

    const std::string& name() const override { return any()->name(); }

    std::string type_string() const override { return any()->type_string(); }

    const file_interface* any() const override {
      if (!file_) {
        throw std::runtime_error("inode has no file");
      }
      return file_;
    }

    void append_chunks_to(std::vector<chunk_type>& vec) const override {
      vec.insert(vec.end(), chunks_.begin(), chunks_.end());
    }

   private:
    uint32_t num_{std::numeric_limits<uint32_t>::max()};
    file const* file_{nullptr};
    std::vector<chunk_type> chunks_;
  };

 public:
  std::shared_ptr<inode> create_inode() override {
    auto ino = std::make_shared<inode_>();
    inodes_.push_back(ino);
    return ino;
  }

  size_t count() const override { return inodes_.size(); }

  void order_inodes(std::shared_ptr<script> scr) override {
    scr->order(inodes_);
  }

  void order_inodes() override {
    std::vector<std::string> paths;
    std::vector<size_t> index(inodes_.size());

    paths.reserve(inodes_.size());

    for (auto const& ino : inodes_) {
      paths.emplace_back(ino->any()->path());
    }

    std::iota(index.begin(), index.end(), size_t(0));

    std::sort(index.begin(), index.end(),
              [&](size_t a, size_t b) { return paths[a] < paths[b]; });

    std::vector<std::shared_ptr<inode>> tmp;
    tmp.reserve(inodes_.size());

    for (size_t ix : index) {
      tmp.emplace_back(inodes_[ix]);
    }

    inodes_.swap(tmp);
  }

  void order_inodes_by_similarity() override {
    std::sort(
        inodes_.begin(), inodes_.end(),
        [](const std::shared_ptr<inode>& a, const std::shared_ptr<inode>& b) {
          auto ash = a->similarity_hash();
          auto bsh = b->similarity_hash();
          return ash < bsh ||
                 (ash == bsh &&
                  (a->size() > b->size() ||
                   (a->size() == b->size() && a->path() < b->path())));
        });
  }

  void number_inodes(size_t first_no) override {
    for (auto& i : inodes_) {
      i->set_num(first_no++);
    }
  }

  void
  for_each_inode(std::function<void(std::shared_ptr<inode> const&)> const& fn)
      const override {
    for (const auto& ino : inodes_) {
      fn(ino);
    }
  }

 private:
  std::vector<std::shared_ptr<inode>> inodes_;
};

std::unique_ptr<inode_manager> inode_manager::create() {
  return std::make_unique<inode_manager_>();
}
} // namespace dwarfs

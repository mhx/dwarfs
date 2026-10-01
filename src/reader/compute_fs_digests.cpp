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

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/boxed_endian.h>
#include <dwarfs/checksum.h>
#include <dwarfs/device_number.h>
#include <dwarfs/error.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/compute_file_hashes.h>
#include <dwarfs/reader/compute_fs_digests.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/vfs_stat.h>

namespace dwarfs::reader {

namespace {

class entry_hasher {
 public:
  entry_hasher(filesystem_digests_config const& config, std::string_view id) {
    attr_.update(id);

    if (config.compute_tree_digest) {
      tree_.emplace(checksum::blake3_256);
      tree_->update(id);
    }
  }

  checksum& attr() { return attr_; }
  std::optional<checksum>& tree() { return tree_; }

  filesystem_digests finalize() {
    filesystem_digests digests;

    digests.attr_digest = attr_.finalize();

    if (tree_) {
      digests.tree_digest = tree_->finalize();
    }

    return digests;
  }

 private:
  checksum attr_{checksum::blake3_256};
  std::optional<checksum> tree_;
};

class fs_digest_computer {
 public:
  using digest_type = std::array<std::byte, 32>;

  static constexpr std::string_view digest_algo{"blake3-256"};

  fs_digest_computer(logger& lgr, os_access const& os, filesystem_v2 const& fs,
                     filesystem_digests_config const& config);

  filesystem_digests compute() const;

 private:
  class state {
   public:
    std::uint64_t hardlink_group(file_stat const& fstat) {
      if (fstat.nlink() <= 1) {
        return 0;
      }

      auto const inode = fstat.ino_unchecked();

      auto it = hardlink_groups_.find(inode);

      if (it != hardlink_groups_.end()) {
        return it->second;
      }

      auto const group_id =
          static_cast<std::uint64_t>(hardlink_groups_.size() + 1);

      hardlink_groups_.emplace(inode, group_id);

      return group_id;
    }

   private:
    std::unordered_map<file_stat::ino_type, std::uint64_t> hardlink_groups_;
  };

  entry_hasher make_entry_hasher(std::string_view id, state& st,
                                 file_stat const& fstat) const {
    auto h = entry_hasher{config_, id};
    hash_inode_common(h, st, fstat);
    return h;
  }

  void
  hash_inode_common(entry_hasher& h, state& st, file_stat const& fstat) const;

  filesystem_digests
  compute_entry_digests(state& st, dir_entry_view const& de) const;
  filesystem_digests compute_dir_digests(state& st, dir_entry_view const& de,
                                         inode_view const& iv) const;
  filesystem_digests
  compute_file_digests(state& st, inode_view const& iv) const;
  filesystem_digests
  compute_symlink_digests(state& st, inode_view const& iv) const;
  filesystem_digests
  compute_device_digests(state& st, inode_view const& iv) const;
  filesystem_digests
  compute_special_digests(state& st, inode_view const& iv) const;

  LOG_PROXY_DECL(debug_logger_policy);
  os_access const& os_;
  filesystem_v2 const& fs_;
  filesystem_digests_config const& config_;
  std::vector<digest_type> content_digests_;
};

fs_digest_computer::fs_digest_computer(logger& lgr, os_access const& os,
                                       filesystem_v2 const& fs,
                                       filesystem_digests_config const& config)
    : LOG_PROXY_INIT(lgr)
    , os_{os}
    , fs_{fs}
    , config_{config} {
  if (config_.compute_tree_digest) {
    auto tv = LOG_TIMED_VERBOSE;

    vfs_stat st;
    fs_.statvfs(&st);
    content_digests_.resize(st.unique_content_count);
    std::vector<bool> seen(st.unique_content_count, false);

    compute_hash_config chc{
        .hash_algorithm = std::string{digest_algo},
        .max_queued_bytes = config_.max_queued_bytes,
        .num_worker_threads = config_.num_worker_threads,
        .result_key = reader::compute_hash_result_key::content_id,
        .digest_format = reader::compute_hash_digest_format::raw,
    };

    compute_file_hashes(LOG_GET_LOGGER, os_, fs_, chc,
                        [this, &seen](compute_hash_result const& r) {
                          assert(r.digest.size() == digest_type{}.size());
                          auto const idx = std::get<std::uint32_t>(r.key);
                          auto& dent = content_digests_.at(idx);
                          std::memcpy(dent.data(), r.digest.data(),
                                      dent.size());
                          assert(!seen.at(idx));
                          seen[idx] = true;
                        });

    std::size_t const seen_count = std::ranges::count(seen, true);

    DWARFS_CHECK(
        seen_count == st.unique_content_count,
        fmt::format("expected {} unique content digests, but computed only {}",
                    st.unique_content_count, seen_count));

    tv << "computed " << seen_count << " unique content digests";
  }
}

void fs_digest_computer::hash_inode_common(entry_hasher& h, state& st,
                                           file_stat const& fstat) const {
  struct attr_common {
    uint32be_t mode; // includes type + permissions
    uint32be_t uid;
    uint32be_t gid;
    uint32be_t reserved0{0};
    uint64be_t hardlink_group;
    uint64be_t nlink;
    int64be_t atime_sec;
    int64be_t mtime_sec;
    int64be_t ctime_sec;
    int64be_t reserved1{0}; // reserved for birth time
    uint32be_t atime_nsec;
    uint32be_t mtime_nsec;
    uint32be_t ctime_nsec;
    uint32be_t reserved2{0}; // reserved for birth time
  };

  static_assert(sizeof(attr_common) == 80);

  struct tree_common {
    uint32be_t type; // just the type, no permissions
  };

  static_assert(sizeof(tree_common) == 4);

  fstat.ensure_valid(file_stat::mode_valid | file_stat::uid_valid |
                     file_stat::gid_valid | file_stat::nlink_valid |
                     file_stat::ino_valid | file_stat::atime_valid |
                     file_stat::mtime_valid | file_stat::ctime_valid);

  attr_common ac{};

  ac.mode = fstat.mode_unchecked();
  ac.uid = fstat.uid_unchecked();
  ac.gid = fstat.gid_unchecked();

  ac.hardlink_group = st.hardlink_group(fstat);
  ac.nlink = fstat.nlink_unchecked();

  auto const& atime = fstat.atimespec_unchecked();
  auto const& mtime = fstat.mtimespec_unchecked();
  auto const& ctime = fstat.ctimespec_unchecked();

  ac.atime_sec = atime.sec;
  ac.mtime_sec = mtime.sec;
  ac.ctime_sec = ctime.sec;
  ac.atime_nsec = atime.nsec;
  ac.mtime_nsec = mtime.nsec;
  ac.ctime_nsec = ctime.nsec;

  h.attr().update(&ac, sizeof(ac));

  if (auto& tree = h.tree()) {
    tree_common tc{};

    tc.type = std::to_underlying(fstat.type());

    tree->update(&tc, sizeof(tc));
  }
}

filesystem_digests
fs_digest_computer::compute_entry_digests(state& st,
                                          dir_entry_view const& de) const {
  auto const iv = de.inode();

  switch (iv.type()) {
    using enum posix_file_type::value;

  case directory:
    return compute_dir_digests(st, de, iv);

  case regular:
    return compute_file_digests(st, iv);

  case symlink:
    return compute_symlink_digests(st, iv);

  case character:
  case block:
    return compute_device_digests(st, iv);

  default:
    return compute_special_digests(st, iv);
  }
}

filesystem_digests
fs_digest_computer::compute_dir_digests(state& st, dir_entry_view const& de,
                                        inode_view const& iv) const {
  auto const dir = fs_.opendir(iv);

  DWARFS_CHECK(dir, fmt::format("failed to open directory inode {} ({})",
                                iv.inode_num(), de.unix_path()));

  auto const num_entries = fs_.dirsize(*dir);
  assert(num_entries >= 2); // "." and ".." entries

  auto const fstat = fs_.getattr(iv);
  auto h = make_entry_hasher("dir-v1", st, fstat);

  auto& attr = h.attr();
  auto& tree = h.tree();

  uint64be_t count{num_entries - 2};

  attr.update(&count, sizeof(count));

  if (tree) {
    tree->update(&count, sizeof(count));
  }

  for (size_t offset = 2; offset < num_entries; ++offset) {
    auto const ent = fs_.readdir(*dir, offset);

    DWARFS_CHECK(
        ent, fmt::format("failed to read directory entry {} in inode {} ({})",
                         offset, iv.inode_num(), de.unix_path()));

    auto const child_digests = compute_entry_digests(st, *ent);

    uint64be_t name_len{ent->name().size()};

    attr.update(&name_len, sizeof(name_len));
    attr.update(ent->name());
    attr.update(child_digests.attr_digest.view());

    if (tree) {
      tree->update(&name_len, sizeof(name_len));
      tree->update(ent->name());
      tree->update(child_digests.tree_digest.view());
    }
  }

  return h.finalize();
}

filesystem_digests
fs_digest_computer::compute_file_digests(state& st,
                                         inode_view const& iv) const {
  auto const fstat = fs_.getattr(iv);
  auto h = make_entry_hasher("file-v1", st, fstat);

  uint64be_t size{static_cast<uint64_t>(fstat.size())};

  h.attr().update(&size, sizeof(size));

  if (auto& tree = h.tree()) {
    tree->update(&size, sizeof(size));

    auto const dup_info = fs_.get_duplication_info(iv);
    auto const& content_digest =
        content_digests_.at(dup_info.unique_content_id);

    tree->update(content_digest);
  }

  return h.finalize();
}

filesystem_digests
fs_digest_computer::compute_symlink_digests(state& st,
                                            inode_view const& iv) const {
  auto const fstat = fs_.getattr(iv);
  auto h = make_entry_hasher("symlink-v1", st, fstat);

  uint64be_t size{static_cast<uint64_t>(fstat.size())};

  h.attr().update(&size, sizeof(size));

  if (auto& tree = h.tree()) {
    tree->update(&size, sizeof(size));
    tree->update(fs_.readlink(iv, readlink_mode::posix));
  }

  return h.finalize();
}

filesystem_digests
fs_digest_computer::compute_device_digests(state& st,
                                           inode_view const& iv) const {
  auto const fstat = fs_.getattr(iv);
  auto h = make_entry_hasher("device-v1", st, fstat);

  assert(fstat.is_device());

  struct device_id {
    uint32be_t rdev_major;
    uint32be_t rdev_minor;
  };

  static_assert(sizeof(device_id) == 8);

  std::error_code ec;
  auto const rdev = fs_.get_device(iv, ec);
  DWARFS_CHECK(!ec, fmt::format("failed to get device id for inode {}: {}",
                                iv.inode_num(), ec.message()));

  device_id dev{};

  dev.rdev_major = rdev.major_id();
  dev.rdev_minor = rdev.minor_id();

  h.attr().update(&dev, sizeof(dev));

  if (auto& tree = h.tree()) {
    tree->update(&dev, sizeof(dev));
  }

  return h.finalize();
}

filesystem_digests
fs_digest_computer::compute_special_digests(state& st,
                                            inode_view const& iv) const {
  auto const fstat = fs_.getattr(iv);
  auto h = make_entry_hasher("special-v1", st, fstat);

  return h.finalize();
}

filesystem_digests fs_digest_computer::compute() const {
  auto tv = LOG_TIMED_VERBOSE;

  state st;
  auto const d = compute_entry_digests(st, fs_.root());

  tv << "computed filesystem digests";

  return d;
}

} // namespace

filesystem_digests
compute_filesystem_digests(logger& lgr, os_access const& os,
                           filesystem_v2 const& fs,
                           filesystem_digests_config const& config) {
  return fs_digest_computer{lgr, os, fs, config}.compute();
}

} // namespace dwarfs::reader

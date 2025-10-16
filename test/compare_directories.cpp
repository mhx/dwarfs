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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

#include <dwarfs/error.h>
#include <dwarfs/file_range_utils.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/sorted_array_map.h>
#include <dwarfs/util.h>

#include "compare_directories.h"

namespace dwarfs::test {

namespace {

namespace fs = std::filesystem;
using namespace std::string_view_literals;

constexpr sorted_array_map file_type_name{
    std::pair{fs::file_type::none, "none"sv},
    std::pair{fs::file_type::not_found, "not_found"sv},
    std::pair{fs::file_type::regular, "regular"sv},
    std::pair{fs::file_type::directory, "directory"sv},
#ifdef _WIN32
    std::pair{fs::file_type::junction, "junction"sv},
#endif
    std::pair{fs::file_type::symlink, "symlink"sv},
    std::pair{fs::file_type::block, "block"sv},
    std::pair{fs::file_type::character, "character"sv},
    std::pair{fs::file_type::fifo, "fifo"sv},
    std::pair{fs::file_type::socket, "socket"sv},
    std::pair{fs::file_type::unknown, "unknown"sv},
};

bool is_directory(fs::file_type ft) {
  return ft == fs::file_type::directory
#ifdef _WIN32
         || ft == fs::file_type::junction
#endif
      ;
}

std::string_view to_string(fs::file_type ft) {
  if (auto const it = file_type_name.find(ft); it != file_type_name.end()) {
    return it->second;
  }
  return "invalid";
}

fs::file_type get_type(fs::path const& p, std::error_code& ec) {
  auto st = fs::symlink_status(p, ec);
  return ec ? fs::file_type::none : st.type();
}

std::unordered_set<fs::path>
get_dir_entries(fs::path const& dir, std::error_code& ec) {
  std::unordered_set<fs::path> out;
  fs::directory_options opts = fs::directory_options::skip_permission_denied;
  for (fs::directory_iterator it(dir, opts, ec), end; !ec && it != end;
       it.increment(ec)) {
    out.emplace(it->path().filename());
  }
  return out;
}

void compare_dirs_impl(fs::path const& left_root, fs::path const& right_root,
                       fs::path const& rel, directory_diff& out) {
  fs::path const left_dir = left_root / rel;
  fs::path const right_dir = right_root / rel;

  std::error_code ec_l, ec_r;
  auto left_set = get_dir_entries(left_dir, ec_l);
  auto right_set = get_dir_entries(right_dir, ec_r);

  if (ec_l) {
    out.differences.push_back(entry_diff{
        .relpath = rel,
        .kind = diff_kind::error,
        .left_type = fs::file_type::directory,
        .right_type = fs::file_type::directory,
        .error_message = "Failed to read left directory: " + ec_l.message(),
    });
  }
  if (ec_r) {
    out.differences.push_back(entry_diff{
        .relpath = rel,
        .kind = diff_kind::error,
        .left_type = fs::file_type::directory,
        .right_type = fs::file_type::directory,
        .error_message = "Failed to read right directory: " + ec_r.message(),
    });
  }

  std::unordered_set<fs::path> all_files;
  all_files.insert(left_set.begin(), left_set.end());
  all_files.insert(right_set.begin(), right_set.end());

  for (auto const& file : all_files) {
    bool const in_left = left_set.contains(file);
    bool const in_right = right_set.contains(file);
    fs::path relpath = rel / file;

    if (!in_left) {
      out.differences.push_back(entry_diff{.relpath = relpath,
                                           .kind = diff_kind::only_in_right,
                                           .left_type = fs::file_type::none,
                                           .right_type = fs::file_type::none});
      continue;
    }
    if (!in_right) {
      out.differences.push_back(entry_diff{.relpath = relpath,
                                           .kind = diff_kind::only_in_left,
                                           .left_type = fs::file_type::none,
                                           .right_type = fs::file_type::none});
      continue;
    }

    fs::path const lp = left_dir / file;
    fs::path const rp = right_dir / file;

    std::error_code ec1, ec2;
    auto const lt = get_type(lp, ec1);
    auto const rt = get_type(rp, ec2);
    if (ec1 || ec2) {
      out.differences.push_back(
          entry_diff{.relpath = relpath,
                     .kind = diff_kind::error,
                     .left_type = lt,
                     .right_type = rt,
                     .error_message = std::string{"stat error: "} +
                                      (ec1 ? ("left=" + ec1.message()) : "") +
                                      (ec1 && ec2 ? ", " : "") +
                                      (ec2 ? ("right=" + ec2.message()) : "")});
      continue;
    }

    if (lt != rt) {
      out.differences.push_back(entry_diff{.relpath = relpath,
                                           .kind = diff_kind::type_mismatch,
                                           .left_type = lt,
                                           .right_type = rt});
      continue;
    }

    if (is_directory(lt)) {
      out.matching_directories.push_back(relpath);
      compare_dirs_impl(left_root, right_root, relpath, out);
    } else if (lt == fs::file_type::symlink) {
      std::error_code e1, e2;
      auto ltarget = fs::read_symlink(lp, e1);
      auto rtarget = fs::read_symlink(rp, e2);
      if (e1 || e2) {
        out.differences.push_back(
            entry_diff{.relpath = relpath,
                       .kind = diff_kind::error,
                       .left_type = lt,
                       .right_type = rt,
                       .error_message = std::string{"read_symlink error: "} +
                                        (e1 ? ("left=" + e1.message()) : "") +
                                        (e1 && e2 ? ", " : "") +
                                        (e2 ? ("right=" + e2.message()) : "")});
      } else if (ltarget != rtarget) {
        out.differences.push_back(
            entry_diff{.relpath = relpath,
                       .kind = diff_kind::symlink_target_diff,
                       .left_type = lt,
                       .right_type = rt,
                       .left_size = std::nullopt,
                       .right_size = std::nullopt,
                       .left_link_target = ltarget,
                       .right_link_target = rtarget});
      } else {
        out.matching_symlinks.push_back(relpath);
      }
    } else if (lt == fs::file_type::regular) {
      std::error_code se1, se2;
      auto lsize = fs::file_size(lp, se1);
      auto rsize = fs::file_size(rp, se2);
      if (se1 || se2) {
        out.differences.push_back(entry_diff{
            .relpath = relpath,
            .kind = diff_kind::error,
            .left_type = lt,
            .right_type = rt,
            .error_message = std::string{"file_size error: "} +
                             (se1 ? ("left=" + se1.message()) : "") +
                             (se1 && se2 ? ", " : "") +
                             (se2 ? ("right=" + se2.message()) : "")});
        continue;
      }
      if (lsize != rsize) {
        out.differences.push_back(entry_diff{.relpath = relpath,
                                             .kind = diff_kind::file_size_diff,
                                             .left_type = lt,
                                             .right_type = rt,
                                             .left_size = lsize,
                                             .right_size = rsize});
        continue; // size differs; skip detailed compare
      }

      entry_diff ed;

      if (lsize > 0) {
        test::detail::compare_files(lp, rp, ed);
      }

      if (!ed.ranges.empty()) {
        ed.relpath = relpath;
        ed.kind = diff_kind::file_content_diff;
        ed.left_type = lt;
        ed.right_type = rt;
        ed.left_size = lsize;
        ed.right_size = rsize;
        out.differences.emplace_back(std::move(ed));
      } else {
        out.matching_regular_files.push_back(relpath);
        out.total_matching_regular_file_size += lsize;
        out.total_left_data_size += ed.left_data_size;
        out.total_right_data_size += ed.right_data_size;
      }
    }
  }
}

} // namespace

namespace detail {

void compare_files(fs::path const& a, fs::path const& b, entry_diff& ed,
                   bool strict_extents) {
  static os_access_generic const os;
  auto& out = ed.ranges;

  auto fa = os.open_file(a);
  auto fb = os.open_file(b);

  assert(fa.size() == fb.size());

  auto ext_a = fa.extents();
  auto ext_b = fb.extents();

  auto get_ranges = [](auto const& exts, extent_kind kind) {
    std::vector<file_range> holes;

    for (auto const& e : exts) {
      if (e.kind() == kind) {
        holes.push_back(e.range());
      }
    }

    return holes;
  };

  auto copy_extents = [](auto const& src, auto& dest) {
    for (auto const& e : src) {
      dest.emplace_back(e.kind(), e.range());
    }
  };

  auto get_data_size = [](auto const& exts) {
    file_size_t size = 0;
    for (auto const& e : exts) {
      if (e.kind() == extent_kind::data) {
        size += e.range().size();
      }
    }
    return size;
  };

  copy_extents(ext_a, ed.left_extents);
  copy_extents(ext_b, ed.right_extents);

  ed.left_data_size = get_data_size(ext_a);
  ed.right_data_size = get_data_size(ext_b);

  auto const holes_a = get_ranges(ext_a, extent_kind::hole);
  auto const holes_b = get_ranges(ext_b, extent_kind::hole);

  if (strict_extents && holes_a != holes_b) {
    // extents differ; report entire file as mismatched in strict mode
    out.emplace_back(file_range{0, fa.size()});
    return;
  }

  auto const hole_ranges = intersect_ranges(holes_a, holes_b);
  auto const data_ranges = complement_ranges(hole_ranges, fa.size());

  for (auto const& r : data_ranges) {
    auto seg_a = fa.segment_at(r);
    auto seg_b = fb.segment_at(r);

    auto sa = seg_a.span();
    auto sb = seg_b.span();

    if (std::memcmp(sa.data(), sb.data(), sa.size()) != 0) {
      auto const start =
          std::mismatch(sa.begin(), sa.end(), sb.begin()).first - sa.begin();
      auto const end =
          sa.size() -
          (std::mismatch(sa.rbegin(), sa.rend(), sb.rbegin()).first -
           sa.rbegin());
      auto const len = end - start;
      auto const snippet_len = std::min<size_t>(len, 64);
      out.emplace_back(r.subrange(start, len), sa.subspan(start, snippet_len),
                       sb.subspan(start, snippet_len));
    }

    ed.total_compared_bytes += r.size();
  }
}

} // namespace detail

directory_diff
compare_directories(fs::path const& left_root, fs::path const& right_root) {
  directory_diff out;

  // Validate roots are directories without following symlinks
  auto ensure_dir = [&](fs::path const& p, std::string_view which) {
    std::error_code ec;
    auto st = fs::symlink_status(p, ec);
    if (ec || !is_directory(st.type())) {
      entry_diff e;
      e.relpath = fs::path{};
      e.kind = diff_kind::error;
      e.left_type = fs::file_type::none;
      e.right_type = fs::file_type::none;
      if (ec) {
        e.error_message = std::string(which) +
                          std::string(" is not accessible: ") + ec.message();
      } else {
        e.error_message = std::string(which) +
                          std::string(" is not a directory: ") +
                          std::string(to_string(st.type()));
      }
      out.differences.emplace_back(std::move(e));
      return false;
    }
    return true;
  };

  bool ok_left = ensure_dir(left_root, "left_root");
  bool ok_right = ensure_dir(right_root, "right_root");
  if (!ok_left || !ok_right)
    return out;

  compare_dirs_impl(left_root, right_root, /*rel=*/fs::path{}, out);
  return out;
}

std::ostream& operator<<(std::ostream& os, directory_diff const& d) {
  auto output_list = [&](std::string_view type,
                         std::vector<fs::path> const& items) {
    if (!items.empty()) {
      os << type << " (" << items.size() << "):\n";
      for (auto const& p : items) {
        os << "  " << path_to_utf8_string_sanitized(p) << '\n';
      }
    }
  };

  auto output_size = [&](std::string_view type, size_t count) {
    os << type << ": " << count << " (" << size_with_unit(count) << ")\n";
  };

  output_list("Matching directories", d.matching_directories);
  output_list("Matching symlinks", d.matching_symlinks);
  output_list("Matching regular files", d.matching_regular_files);

  output_size("Total size of matching regular files",
              d.total_matching_regular_file_size);
  output_size("Total left data size", d.total_left_data_size);
  output_size("Total right data size", d.total_right_data_size);

  if (!d.differences.empty()) {
    os << "Differences (" << d.differences.size() << "):\n";

    for (auto const& e : d.differences) {
      auto path_str = path_to_utf8_string_sanitized(e.relpath);

      switch (e.kind) {
      case diff_kind::only_in_left:
        os << "Only in left: " << path_str << '\n';
        break;
      case diff_kind::only_in_right:
        os << "Only in right: " << path_str << '\n';
        break;
      case diff_kind::type_mismatch:
        os << "Type mismatch: " << path_str
           << " (left=" << to_string(e.left_type)
           << ", right=" << to_string(e.right_type) << ")\n";
        break;
      case diff_kind::symlink_target_diff:
        os << "Symlink target differs: " << path_str << "\n  left ->  "
           << path_to_utf8_string_sanitized(e.left_link_target.value())
           << "\n  right -> "
           << path_to_utf8_string_sanitized(e.right_link_target.value())
           << '\n';
        break;
      case diff_kind::file_size_diff:
        os << "File size differs: " << path_str << " (left="
           << (e.left_size ? std::to_string(*e.left_size)
                           : std::string{"<err>"})
           << ", right="
           << (e.right_size ? std::to_string(*e.right_size)
                            : std::string{"<err>"})
           << ")\n";
        break;
      case diff_kind::file_content_diff: {
        os << "File content differs: " << path_str << '\n';
        for (auto const& r : e.ranges) {
          os << "  range [offset=" << r.range.offset()
             << ", size=" << r.range.size() << ", end=" << r.range.end()
             << "]\n";
          if (!r.left_data.empty() && !r.right_data.empty()) {
            os << "---- left data ----\n" << hexdump(r.left_data);
            os << "---- right data ----\n" << hexdump(r.right_data);
          }
        }
        os << "  left extents (" << e.left_extents.size() << "):\n";
        for (auto const& ex : e.left_extents) {
          os << "    " << ex << '\n';
        }
        os << "  right extents (" << e.right_extents.size() << "):\n";
        for (auto const& ex : e.right_extents) {
          os << "    " << ex << '\n';
        }
        break;
      }
      case diff_kind::error:
        os << "Error at " << path_str
           << ": " + e.error_message.value_or("<unknown>") << '\n';
        break;
      }
    }
  }
  return os;
}

} // namespace dwarfs::test

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

#define FUSE_USE_VERSION 35

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <fuse3/fuse.h>
#include <fuse3/fuse_opt.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

fs::path directory;
fs::path output;

struct options {
  int seen_mountpoint{0};
  bool is_help{false};
  char const* output_str{nullptr};
} fuselog_opts;

struct logdata {
  std::mutex mx;
  std::unordered_set<std::string> seen;
  std::vector<std::string> opened;

  void open_file(std::string const& path) {
    std::lock_guard<std::mutex> lock(mx);
    if (seen.emplace(path).second) {
      opened.push_back(path);
    }
  }
} fuselog_data;

#define FUSELOG_OPT(t, p, v)                                                   \
  ::fuse_opt { t, offsetof(struct options, p), v }
constexpr std::array fuselog_optspec{
    FUSELOG_OPT("output=%s", output_str, 0),
    ::fuse_opt(FUSE_OPT_END),
};

int option_hdl(void* data, char const* arg, int key,
               struct fuse_args* /*outargs*/) {
  auto& opts = *reinterpret_cast<options*>(data);
  std::string_view argsv{arg};

  switch (key) {
  case FUSE_OPT_KEY_NONOPT:
    if (opts.seen_mountpoint) {
      return -1;
    }

    if (!directory.empty()) {
      opts.seen_mountpoint = 1;
      return 1;
    }

    directory = fs::canonical(argsv);

    return 0;

  case FUSE_OPT_KEY_OPT:
    if (argsv == "-h" || argsv == "--help") {
      opts.is_help = true;
      return 1;
    }
    break;

  default:
    break;
  }

  return 1;
}

fs::path get_real_path(std::string_view path) {
  return directory / path.substr(1);
}

int fuselog_getattr(char const* path, struct ::stat* stbuf,
                    struct fuse_file_info*) {
  auto real_path = get_real_path(path);
  return ::lstat(real_path.c_str(), stbuf) == -1 ? -errno : 0;
}

int fuselog_readdir(char const* path, void* buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info*,
                    enum fuse_readdir_flags) {
  auto real_path = get_real_path(path);

  auto dp = ::opendir(real_path.c_str());

  if (!dp) {
    return -errno;
  }

  while (auto de = ::readdir(dp)) {
    if (offset > 0) {
      --offset;
      continue;
    }

    struct ::stat st;

    ::fstatat(::dirfd(dp), de->d_name, &st, AT_SYMLINK_NOFOLLOW);

    if (filler(buf, de->d_name, &st, 0, FUSE_FILL_DIR_PLUS) != 0) {
      break;
    }
  }

  ::closedir(dp);

  return 0;
}

int fuselog_open(char const* path, struct fuse_file_info* fi) {
  auto real_path = get_real_path(path);

  auto fd = ::open(real_path.c_str(), fi->flags);

  if (fd == -1) {
    return -errno;
  }

  fuselog_data.open_file(path);

  fi->fh = fd;

  return 0;
}

int fuselog_release(char const*, struct fuse_file_info* fi) {
  return ::close(fi->fh) == -1 ? -errno : 0;
}

int fuselog_read(char const*, char* buf, size_t size, off_t offset,
                 struct fuse_file_info* fi) {
  assert(fi != nullptr);
  auto res = ::pread(fi->fh, buf, size, offset);
  return res == -1 ? -errno : res;
}

int fuselog_readlink(char const* path, char* buf, size_t size) {
  auto real_path = get_real_path(path);
  auto res = ::readlink(real_path.c_str(), buf, size - 1);
  if (res == -1) {
    return -errno;
  }
  buf[res] = '\0';
  return 0;
}

void fuselog_destroy(void*) {
  if (output.empty()) {
    return;
  }

  std::ofstream ofs(output.c_str(), std::ios::trunc);

  for (auto const& path : fuselog_data.opened) {
    ofs << path << '\n';
  }
}

const struct fuse_operations fuselog_oper = {
    .getattr = fuselog_getattr,
    .readlink = fuselog_readlink,
    .open = fuselog_open,
    .read = fuselog_read,
    .release = fuselog_release,
    .readdir = fuselog_readdir,
    .destroy = fuselog_destroy,
};

void usage(fs::path const& progname) {
  std::cerr << "Usage: " << progname.filename().string()
            << " <directory> <mountpoint> [options]\n\n"
            << "fuselog options:\n"
            << "    -o output=FILE         output log file\n"
            << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  if (fuse_opt_parse(&args, &fuselog_opts, fuselog_optspec.data(),
                     option_hdl) == -1) {
    return 1;
  }

  if (fuselog_opts.is_help) {
    usage(argv[0]);
    fuse_opt_add_arg(&args, "-ho");
    args.argv[0][0] = '\0';
  }

  if (fuselog_opts.output_str) {
    output = fs::absolute(fuselog_opts.output_str);
  }

  auto ret = fuse_main(args.argc, args.argv, &fuselog_oper, nullptr);

  fuse_opt_free_args(&args);

  return ret;
}

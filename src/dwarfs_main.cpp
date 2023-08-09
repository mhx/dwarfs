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

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <fmt/format.h>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/experimental/symbolizer/SignalHandler.h>

#ifndef DWARFS_FUSE_LOWLEVEL
#define DWARFS_FUSE_LOWLEVEL 1
#endif

#if FUSE_USE_VERSION >= 30
#if DWARFS_FUSE_LOWLEVEL
#include <fuse3/fuse_lowlevel.h>
#else
#include <fuse3/fuse.h>
#endif
#else
#include <fuse.h>
#if DWARFS_FUSE_LOWLEVEL
#include <fuse/fuse_lowlevel.h>
#endif
#endif

#ifdef _WIN32
#include <fuse3/winfsp_fuse.h>
#define st_atime st_atim.tv_sec
#define st_ctime st_ctim.tv_sec
#define st_mtime st_mtim.tv_sec
#define DWARFS_FSP_COMPAT
#endif

#include "dwarfs/error.h"
#include "dwarfs/file_stat.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/iovec_read_buf.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/performance_monitor.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs/version.h"
#include "dwarfs/vfs_stat.h"
#include "dwarfs_tool_main.h"

namespace {
#ifdef DWARFS_FSP_COMPAT
using native_stat = struct ::fuse_stat;
using native_statvfs = struct ::fuse_statvfs;
using native_off_t = ::fuse_off_t;
#else
using native_stat = struct ::stat;
using native_statvfs = struct ::statvfs;
using native_off_t = ::off_t;
#endif
} // namespace

namespace dwarfs {

struct options {
  std::filesystem::path progname;
  std::filesystem::path fsimage;
  int seen_mountpoint{0};
  char const* cachesize_str{nullptr};           // TODO: const?? -> use string?
  char const* debuglevel_str{nullptr};          // TODO: const?? -> use string?
  char const* workers_str{nullptr};             // TODO: const?? -> use string?
  char const* mlock_str{nullptr};               // TODO: const?? -> use string?
  char const* decompress_ratio_str{nullptr};    // TODO: const?? -> use string?
  char const* image_offset_str{nullptr};        // TODO: const?? -> use string?
  char const* cache_tidy_strategy_str{nullptr}; // TODO: const?? -> use string?
  char const* cache_tidy_interval_str{nullptr}; // TODO: const?? -> use string?
  char const* cache_tidy_max_age_str{nullptr};  // TODO: const?? -> use string?
#if DWARFS_PERFMON_ENABLED
  char const* perfmon_enabled_str{nullptr}; // TODO: const?? -> use string?
#endif
  int enable_nlink{0};
  int readonly{0};
  int cache_image{0};
  int cache_files{0};
  size_t cachesize{0};
  size_t workers{0};
  mlock_mode lock_mode{mlock_mode::NONE};
  double decompress_ratio{0.0};
  logger::level_type debuglevel{logger::level_type::ERROR};
  cache_tidy_strategy block_cache_tidy_strategy{cache_tidy_strategy::NONE};
  std::chrono::milliseconds block_cache_tidy_interval{std::chrono::minutes(5)};
  std::chrono::milliseconds block_cache_tidy_max_age{std::chrono::minutes{10}};
};

struct dwarfs_userdata {
  explicit dwarfs_userdata(std::ostream& os)
      : lgr{os} {}

  options opts;
  stream_logger lgr;
  filesystem_v2 fs;
  std::shared_ptr<performance_monitor> perfmon;
  PERFMON_EXT_PROXY_DECL
  PERFMON_EXT_TIMER_DECL(op_init)
  PERFMON_EXT_TIMER_DECL(op_lookup)
  PERFMON_EXT_TIMER_DECL(op_getattr)
  PERFMON_EXT_TIMER_DECL(op_access)
  PERFMON_EXT_TIMER_DECL(op_readlink)
  PERFMON_EXT_TIMER_DECL(op_open)
  PERFMON_EXT_TIMER_DECL(op_read)
  PERFMON_EXT_TIMER_DECL(op_readdir)
  PERFMON_EXT_TIMER_DECL(op_statfs)
  PERFMON_EXT_TIMER_DECL(op_getxattr)
  PERFMON_EXT_TIMER_DECL(op_listxattr)
};

// TODO: better error handling

#define DWARFS_OPT(t, p, v)                                                    \
  { t, offsetof(struct options, p), v }

constexpr struct ::fuse_opt dwarfs_opts[] = {
    // TODO: user, group, atime, mtime, ctime for those fs who don't have it?
    DWARFS_OPT("cachesize=%s", cachesize_str, 0),
    DWARFS_OPT("debuglevel=%s", debuglevel_str, 0),
    DWARFS_OPT("workers=%s", workers_str, 0),
    DWARFS_OPT("mlock=%s", mlock_str, 0),
    DWARFS_OPT("decratio=%s", decompress_ratio_str, 0),
    DWARFS_OPT("offset=%s", image_offset_str, 0),
    DWARFS_OPT("tidy_strategy=%s", cache_tidy_strategy_str, 0),
    DWARFS_OPT("tidy_interval=%s", cache_tidy_interval_str, 0),
    DWARFS_OPT("tidy_max_age=%s", cache_tidy_max_age_str, 0),
    DWARFS_OPT("enable_nlink", enable_nlink, 1),
    DWARFS_OPT("readonly", readonly, 1),
    DWARFS_OPT("cache_image", cache_image, 1),
    DWARFS_OPT("no_cache_image", cache_image, 0),
    DWARFS_OPT("cache_files", cache_files, 1),
    DWARFS_OPT("no_cache_files", cache_files, 0),
#if DWARFS_PERFMON_ENABLED
    DWARFS_OPT("perfmon=%s", perfmon_enabled_str, 0),
#endif
    FUSE_OPT_END};

std::unordered_map<std::string_view, cache_tidy_strategy> const
    cache_tidy_strategy_map{
        {"none", cache_tidy_strategy::NONE},
        {"time", cache_tidy_strategy::EXPIRY_TIME},
        {"swap", cache_tidy_strategy::BLOCK_SWAPPED_OUT},
    };

namespace {

constexpr std::string_view pid_xattr{"user.dwarfs.driver.pid"};
constexpr std::string_view perfmon_xattr{"user.dwarfs.driver.perfmon"};

} // namespace

#if DWARFS_FUSE_LOWLEVEL
#define dUSERDATA                                                              \
  auto userdata = reinterpret_cast<dwarfs_userdata*>(fuse_req_userdata(req))
#else
#define dUSERDATA                                                              \
  auto userdata =                                                              \
      reinterpret_cast<dwarfs_userdata*>(fuse_get_context()->private_data)
#endif

template <typename LoggerPolicy>
void op_init_common(void* data) {
  auto userdata = reinterpret_cast<dwarfs_userdata*>(data);
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_init)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  // we must do this *after* the fuse driver has forked into background
  userdata->fs.set_num_workers(userdata->opts.workers);

  cache_tidy_config tidy;
  tidy.strategy = userdata->opts.block_cache_tidy_strategy;
  tidy.interval = userdata->opts.block_cache_tidy_interval;
  tidy.expiry_time = userdata->opts.block_cache_tidy_max_age;

  // we must do this *after* the fuse driver has forked into background
  userdata->fs.set_cache_tidy_config(tidy);
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_init(void* data, struct fuse_conn_info* /*conn*/) {
  op_init_common<LoggerPolicy>(data);
}
#else
template <typename LoggerPolicy>
void* op_init(struct fuse_conn_info* /*conn*/, struct fuse_config* /*cfg*/) {
  // TODO: config?
  auto userdata = fuse_get_context()->private_data;
  op_init_common<LoggerPolicy>(userdata);
  return userdata;
}
#endif

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_lookup(fuse_req_t req, fuse_ino_t parent, char const* name) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_lookup)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << parent << ", " << name << ")";

  int err = ENOENT;

  try {
    auto entry = userdata->fs.find(parent, name);

    if (entry) {
      file_stat stbuf;

      err = userdata->fs.getattr(*entry, &stbuf);

      if (err == 0) {
        struct ::fuse_entry_param e;

        ::memset(&e.attr, 0, sizeof(e.attr));
        copy_file_stat(&e.attr, stbuf);
        e.generation = 1;
        e.ino = e.attr.st_ino;
        e.attr_timeout = std::numeric_limits<double>::max();
        e.entry_timeout = std::numeric_limits<double>::max();

        fuse_reply_entry(req, &e);

        return;
      }
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  fuse_reply_err(req, err);
}
#endif

template <typename LogProxy, typename Find>
int op_getattr_common(LogProxy& log_, dwarfs_userdata* userdata,
                      native_stat* st, Find const& find) {
  int err = ENOENT;

  try {
    auto entry = find();

    if (entry) {
      file_stat stbuf;

      err = userdata->fs.getattr(*entry, &stbuf);

      if (err == 0) {
        ::memset(st, 0, sizeof(*st));
        copy_file_stat(st, stbuf);
      }
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  return err;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info*) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_getattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ")";

  native_stat st;

  int err = op_getattr_common(
      log_, userdata, &st, [userdata, ino] { return userdata->fs.find(ino); });

  if (err == 0) {
    fuse_reply_attr(req, &st, std::numeric_limits<double>::max());
  } else {
    fuse_reply_err(req, err);
  }
}
#else
template <typename LoggerPolicy>
int op_getattr(char const* path, native_stat* st, struct fuse_file_info*) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_getattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ")";

  return -op_getattr_common(
      log_, userdata, st, [userdata, path] { return userdata->fs.find(path); });
}
#endif

template <typename LogProxy, typename Find>
int op_access_common(LogProxy& log_, dwarfs_userdata* userdata, int mode,
                     uid_t uid, gid_t gid, Find const& find) {
  int err = ENOENT;

  try {
    if (auto entry = find()) {
      err = userdata->fs.access(*entry, mode, uid, gid);
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  return err;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_access(fuse_req_t req, fuse_ino_t ino, int mode) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_access)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ")";

  auto ctx = fuse_req_ctx(req);

  int err =
      op_access_common(log_, userdata, mode, ctx->uid, ctx->gid,
                       [userdata, ino] { return userdata->fs.find(ino); });

  fuse_reply_err(req, err);
}
#else
template <typename LoggerPolicy>
int op_access(char const* path, int mode) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_access)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ")";

  auto ctx = fuse_get_context();

  return -op_access_common(
      log_, userdata, mode, ctx->uid, ctx->gid,
      [userdata, path] { return userdata->fs.find(path); });
}
#endif

template <typename LogProxy, typename Find>
int op_readlink_common(LogProxy& log_, dwarfs_userdata* userdata,
                       std::string* str, Find const& find) {
  int err = ENOENT;

  try {
    if (auto entry = find()) {
      err = userdata->fs.readlink(*entry, str, readlink_mode::unix);
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  return err;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_readlink(fuse_req_t req, fuse_ino_t ino) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_readlink)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  std::string symlink;

  auto err = op_readlink_common(log_, userdata, &symlink, [userdata, ino] {
    return userdata->fs.find(ino);
  });

  if (err == 0) {
    fuse_reply_readlink(req, symlink.c_str());
  } else {
    fuse_reply_err(req, err);
  }
}
#else
template <typename LoggerPolicy>
int op_readlink(char const* path, char* buf, size_t buflen) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_readlink)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  std::string symlink;

  auto err = op_readlink_common(log_, userdata, &symlink, [userdata, path] {
    return userdata->fs.find(path);
  });

  if (err == 0) {
    ::strncpy_s(buf, buflen, symlink.data(), symlink.size());
  }

  return -err;
}
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

template <typename LogProxy, typename Find>
int op_open_common(LogProxy& log_, dwarfs_userdata* userdata,
                   struct fuse_file_info* fi, Find const& find) {
  int err = ENOENT;

  try {
    auto entry = find();

    if (entry) {
      if (entry->is_directory()) {
        err = EISDIR;
      } else if ((fi->flags & O_ACCMODE) != O_RDONLY ||
                 (fi->flags & (O_APPEND | O_TRUNC)) != 0) {
        err = EACCES;
      } else {
        fi->fh = entry->inode_num();
        fi->direct_io = !userdata->opts.cache_files;
        fi->keep_cache = userdata->opts.cache_files;
        return 0;
      }
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  return err;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_open)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  auto err = op_open_common(log_, userdata, fi,
                            [userdata, ino] { return userdata->fs.find(ino); });

  if (err == 0) {
    fuse_reply_open(req, fi);
  } else {
    fuse_reply_err(req, err);
  }
}
#else
template <typename LoggerPolicy>
int op_open(char const* path, struct fuse_file_info* fi) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_open)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  return -op_open_common(log_, userdata, fi,
                         [userdata, path] { return userdata->fs.find(path); });
}
#endif

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_read(fuse_req_t req, fuse_ino_t ino, size_t size, file_off_t off,
             struct fuse_file_info* fi) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_read)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    if (FUSE_ROOT_ID + fi->fh == ino) {
      iovec_read_buf buf;
      ssize_t rv = userdata->fs.readv(ino, buf, size, off);

      LOG_DEBUG << "readv(" << ino << ", " << size << ", " << off << ") -> "
                << rv << " [size = " << buf.buf.size() << "]";

      if (rv >= 0) {
        int frv = fuse_reply_iov(req, buf.buf.empty() ? nullptr : &buf.buf[0],
                                 buf.buf.size());

        if (frv == 0) {
          return;
        }

        err = -frv;
      } else {
        err = -rv;
      }
    } else {
      err = EIO;
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  fuse_reply_err(req, err);
}
#else
template <typename LoggerPolicy>
int op_read(char const* path, char* buf, size_t size, native_off_t off,
            struct fuse_file_info* fi) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_read)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = -ENOENT;

  try {
    ssize_t rv = userdata->fs.read(fi->fh, buf, size, off);

    LOG_DEBUG << "read(" << path << " [" << fi->fh << "], " << size << ", "
              << off << ") -> " << rv;

    if (rv >= 0) {
      return rv;
    }

    err = rv;
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = -e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = -EIO;
  }

  return err;
}
#endif

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, file_off_t off,
                struct fuse_file_info* /*fi*/) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_readdir)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ", " << size << ", " << off << ")";

  int err = ENOENT;

  try {
    auto dirent = userdata->fs.find(ino);

    if (dirent) {
      auto dir = userdata->fs.opendir(*dirent);

      if (dir) {
        file_off_t lastoff = userdata->fs.dirsize(*dir);
        file_stat stbuf;
        native_stat st;
        std::vector<char> buf(size);
        size_t written = 0;

        ::memset(&st, 0, sizeof(st));

        while (off < lastoff && written < size) {
          auto res = userdata->fs.readdir(*dir, off);
          assert(res);

          auto [entry, name_view] = *res;
          std::string name(name_view);

          userdata->fs.getattr(entry, &stbuf);
          copy_file_stat(&st, stbuf);

          assert(written < buf.size());

          size_t needed =
              fuse_add_direntry(req, &buf[written], buf.size() - written,
                                name.c_str(), &st, off + 1);

          if (written + needed > buf.size()) {
            break;
          }

          written += needed;
          ++off;
        }

        fuse_reply_buf(req, written > 0 ? &buf[0] : nullptr, written);

        return;
      }

      err = ENOTDIR;
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  fuse_reply_err(req, err);
}
#else
template <typename LoggerPolicy>
int op_readdir(char const* path, void* buf, fuse_fill_dir_t filler,
               native_off_t off, struct fuse_file_info* /*fi*/,
               enum fuse_readdir_flags /*flags*/) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_readdir)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ")";

  int err = -ENOENT;

  try {
    auto dirent = userdata->fs.find(path);

    if (dirent) {
      auto dir = userdata->fs.opendir(*dirent);

      if (dir) {
        file_off_t lastoff = userdata->fs.dirsize(*dir);
        file_stat stbuf;
        native_stat st;

        ::memset(&st, 0, sizeof(st));

        while (off < lastoff) {
          auto res = userdata->fs.readdir(*dir, off);
          assert(res);

          auto [entry, name_view] = *res;
          std::string name(name_view);

          userdata->fs.getattr(entry, &stbuf);
          copy_file_stat(&st, stbuf);

          if (filler(buf, name.c_str(), &st, off + 1, FUSE_FILL_DIR_PLUS) !=
              0) {
            break;
          }

          ++off;
        }

        return 0;
      }

      err = -ENOTDIR;
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = -e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = -EIO;
  }

  return err;
}
#endif

template <typename LogProxy>
int op_statfs_common(LogProxy& log_, dwarfs_userdata* userdata,
                     native_statvfs* st) {
  int err = EIO;

  try {
    vfs_stat stbuf;

    err = userdata->fs.statvfs(&stbuf);

    if (err == 0) {
      ::memset(st, 0, sizeof(*st));
      copy_vfs_stat(st, stbuf);

#ifndef _WIN32
      if (stbuf.readonly) {
        st->f_flag |= ST_RDONLY;
      }
#endif
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  return err;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_statfs(fuse_req_t req, fuse_ino_t /*ino*/) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_statfs)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  struct ::statvfs st;

  int err = op_statfs_common(log_, userdata, &st);

  if (err == 0) {
    fuse_reply_statfs(req, &st);
  } else {
    fuse_reply_err(req, err);
  }
}
#else
template <typename LoggerPolicy>
int op_statfs(char const* path, native_statvfs* st) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_statfs)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ")";

  return -op_statfs_common(log_, userdata, st);
}
#endif

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_getxattr(fuse_req_t req, fuse_ino_t ino, char const* name,
                 size_t size) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_getxattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ", " << name << ", " << size << ")";

  int err = ENODATA;

  try {
    std::ostringstream oss;
    size_t extra_size = 0;

    if (ino == FUSE_ROOT_ID) {
      if (name == pid_xattr) {
        // use to_string() to prevent locale-specific formatting
        oss << std::to_string(::getpid());
      } else if (name == perfmon_xattr) {
#if DWARFS_PERFMON_ENABLED
        if (userdata->perfmon) {
          userdata->perfmon->summarize(oss);
          extra_size = 4096;
        } else {
          oss << "performance monitor is disabled\n";
        }
#else
        oss << "no performance monitor support\n";
#endif
      }
    }

    auto value = oss.view();

    LOG_TRACE << __func__ << ": value.size=" << value.size()
              << ", extra_size=" << extra_size;

    if (!value.empty()) {
      if (size == 0) {
        fuse_reply_xattr(req, value.size() + extra_size);
        return;
      } else if (size >= value.size()) {
        fuse_reply_buf(req, value.data(), value.size());
        return;
      } else {
        err = ERANGE;
      }
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  fuse_reply_err(req, err);
}
#else
template <typename LoggerPolicy>
int op_getxattr(char const* path, char const* name, char* value, size_t size) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_getxattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ", " << name << ", " << size << ")";

  return -ENOTSUP;
}
#endif

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void op_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_listxattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ", " << size << ")";

  int err = ERANGE;

  try {
    std::ostringstream oss;

    if (ino == FUSE_ROOT_ID) {
      oss << pid_xattr << '\0';
      oss << perfmon_xattr << '\0';
    }

    auto xattrs = oss.view();

    LOG_TRACE << __func__ << ": xattrs.size=" << xattrs.size();

    if (size == 0) {
      fuse_reply_xattr(req, xattrs.size());
      return;
    } else if (size >= xattrs.size()) {
      fuse_reply_buf(req, xattrs.data(), xattrs.size());
      return;
    }
  } catch (dwarfs::system_error const& e) {
    LOG_ERROR << e.what();
    err = e.get_errno();
  } catch (std::exception const& e) {
    LOG_ERROR << e.what();
    err = EIO;
  }

  fuse_reply_err(req, err);
}
#else
template <typename LoggerPolicy>
int op_listxattr(char const* path, char* list, size_t size) {
  dUSERDATA;
  PERFMON_EXT_SCOPED_SECTION(*userdata, op_listxattr)
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << path << ", " << size << ")";

  return -ENOTSUP;
}
#endif

#if !DWARFS_FUSE_LOWLEVEL
// XXX: Not implementing this currently crashes WinFsp when a file is renamed
template <typename LoggerPolicy>
int op_rename(char const* from, char const* to, unsigned int flags) {
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << from << ", " << to << ", " << flags << ")";

  return -ENOSYS;
}
#endif

void usage(std::filesystem::path const& progname) {
  std::cerr
      << tool_header("dwarfs",
                     fmt::format(", fuse version {}", FUSE_USE_VERSION))
#if !DWARFS_FUSE_LOWLEVEL
      << "USING HIGH-LEVEL FUSE API\n\n"
#endif
      << "usage: " << progname.filename().string()
      << " <image> <mountpoint> [options]\n\n"
      << "DWARFS options:\n"
      << "    -o cachesize=SIZE      set size of block cache (512M)\n"
      << "    -o workers=NUM         number of worker threads (2)\n"
      << "    -o mlock=NAME          mlock mode: (none), try, must\n"
      << "    -o decratio=NUM        ratio for full decompression (0.8)\n"
      << "    -o offset=NUM|auto     filesystem image offset in bytes (0)\n"
      << "    -o enable_nlink        show correct hardlink numbers\n"
      << "    -o readonly            show read-only file system\n"
      << "    -o (no_)cache_image    (don't) keep image in kernel cache\n"
      << "    -o (no_)cache_files    (don't) keep files in kernel cache\n"
      << "    -o debuglevel=NAME     error, warn, info, debug, trace\n"
      << "    -o tidy_strategy=NAME  (none)|time|swap\n"
      << "    -o tidy_interval=TIME  interval for cache tidying (5m)\n"
      << "    -o tidy_max_age=TIME   tidy blocks after this time (10m)\n"
#if DWARFS_PERFMON_ENABLED
      << "    -o perfmon=name[,...]  enable performance monitor\n"
#endif
      << "\n";

#if DWARFS_FUSE_LOWLEVEL && FUSE_USE_VERSION >= 30
  std::cerr << "FUSE options:\n";
  fuse_cmdline_help();
#else
  struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, "");
  fuse_opt_add_arg(&args, "-ho");
  struct fuse_operations fsops;
  ::memset(&fsops, 0, sizeof(fsops));
  fuse_main(args.argc, args.argv, &fsops, nullptr);
  fuse_opt_free_args(&args);
#endif

  ::exit(1);
}

int option_hdl(void* data, char const* arg, int key,
               struct fuse_args* /*outargs*/) {
  auto* opts = reinterpret_cast<options*>(data);

  switch (key) {
  case FUSE_OPT_KEY_NONOPT:
    if (opts->seen_mountpoint) {
      return -1;
    }

    if (!opts->fsimage.empty()) {
      opts->seen_mountpoint = 1;
      return 1;
    }

    opts->fsimage = canonical_path(
        std::filesystem::path(reinterpret_cast<char8_t const*>(arg)));

    return 0;

  case FUSE_OPT_KEY_OPT:
    if (::strncmp(arg, "-h", 2) == 0 || ::strncmp(arg, "--help", 6) == 0) {
      usage(opts->progname);
    }
    break;

  default:
    break;
  }

  return 1;
}

#if DWARFS_FUSE_LOWLEVEL
template <typename LoggerPolicy>
void init_fuse_ops(struct fuse_lowlevel_ops& ops,
                   dwarfs_userdata const& userdata) {
  ops.init = &op_init<LoggerPolicy>;
  ops.lookup = &op_lookup<LoggerPolicy>;
  ops.getattr = &op_getattr<LoggerPolicy>;
  ops.access = &op_access<LoggerPolicy>;
  if (userdata.fs.has_symlinks()) {
    ops.readlink = &op_readlink<LoggerPolicy>;
  }
  ops.open = &op_open<LoggerPolicy>;
  ops.read = &op_read<LoggerPolicy>;
  ops.readdir = &op_readdir<LoggerPolicy>;
  ops.statfs = &op_statfs<LoggerPolicy>;
  ops.getxattr = &op_getxattr<LoggerPolicy>;
  ops.listxattr = &op_listxattr<LoggerPolicy>;
}
#else
template <typename LoggerPolicy>
void init_fuse_ops(struct fuse_operations& ops,
                   dwarfs_userdata const& userdata) {
  ops.init = &op_init<LoggerPolicy>;
  ops.getattr = &op_getattr<LoggerPolicy>;
  ops.access = &op_access<LoggerPolicy>;
  if (userdata.fs.has_symlinks()) {
    ops.readlink = &op_readlink<LoggerPolicy>;
  }
  ops.open = &op_open<LoggerPolicy>;
  ops.read = &op_read<LoggerPolicy>;
  ops.readdir = &op_readdir<LoggerPolicy>;
  ops.statfs = &op_statfs<LoggerPolicy>;
  ops.getxattr = &op_getxattr<LoggerPolicy>;
  ops.listxattr = &op_listxattr<LoggerPolicy>;
  ops.rename = &op_rename<LoggerPolicy>;
}
#endif

#if FUSE_USE_VERSION > 30

int run_fuse(struct fuse_args& args,
#if DWARFS_FUSE_LOWLEVEL
             struct fuse_cmdline_opts const& fuse_opts,
#endif
             dwarfs_userdata& userdata) {
#if DWARFS_FUSE_LOWLEVEL
  struct fuse_lowlevel_ops fsops;
#else
  struct fuse_operations fsops;
#endif

  ::memset(&fsops, 0, sizeof(fsops));

  if (userdata.opts.debuglevel >= logger::DEBUG) {
    init_fuse_ops<debug_logger_policy>(fsops, userdata);
  } else {
    init_fuse_ops<prod_logger_policy>(fsops, userdata);
  }

  int err = 1;

#if DWARFS_FUSE_LOWLEVEL
  if (auto session =
          fuse_session_new(&args, &fsops, sizeof(fsops), &userdata)) {
    if (fuse_set_signal_handlers(session) == 0) {
      if (fuse_session_mount(session, fuse_opts.mountpoint) == 0) {
        if (fuse_daemonize(fuse_opts.foreground) == 0) {
          if (fuse_opts.singlethread) {
            err = fuse_session_loop(session);
          } else {
            struct fuse_loop_config config;
            config.clone_fd = fuse_opts.clone_fd;
            config.max_idle_threads = fuse_opts.max_idle_threads;
            err = fuse_session_loop_mt(session, &config);
          }
        }
        fuse_session_unmount(session);
      }
      fuse_remove_signal_handlers(session);
    }
    fuse_session_destroy(session);
  }

  ::free(fuse_opts.mountpoint);
#else
  err = fuse_main(args.argc, args.argv, &fsops, &userdata);
#endif

  fuse_opt_free_args(&args);

  return err;
}

#else

int run_fuse(struct fuse_args& args, char* mountpoint, int mt, int fg,
             dwarfs_userdata& userdata) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  if (userdata.opts.debuglevel >= logger::DEBUG) {
    init_fuse_ops<debug_logger_policy>(fsops, userdata);
  } else {
    init_fuse_ops<prod_logger_policy>(fsops, userdata);
  }

  int err = 1;

  if (auto ch = fuse_mount(mountpoint, &args)) {
    if (auto se = fuse_lowlevel_new(&args, &fsops, sizeof(fsops), &userdata)) {
      if (fuse_daemonize(fg) != -1) {
        if (fuse_set_signal_handlers(se) != -1) {
          fuse_session_add_chan(se, ch);
          err = mt ? fuse_session_loop_mt(se) : fuse_session_loop(se);
          fuse_remove_signal_handlers(se);
          fuse_session_remove_chan(ch);
        }
      }
      fuse_session_destroy(se);
    }
    fuse_unmount(mountpoint, ch);
  }

  ::free(mountpoint);
  fuse_opt_free_args(&args);

  return err;
}

#endif

template <typename LoggerPolicy>
void load_filesystem(dwarfs_userdata& userdata) {
  LOG_PROXY(LoggerPolicy, userdata.lgr);
  auto ti = LOG_TIMED_INFO;
  auto& opts = userdata.opts;

  filesystem_options fsopts;
  fsopts.lock_mode = opts.lock_mode;
  fsopts.block_cache.max_bytes = opts.cachesize;
  fsopts.block_cache.num_workers = opts.workers;
  fsopts.block_cache.decompress_ratio = opts.decompress_ratio;
  fsopts.block_cache.mm_release = !opts.cache_image;
  fsopts.block_cache.init_workers = false;
  fsopts.metadata.enable_nlink = bool(opts.enable_nlink);
  fsopts.metadata.readonly = bool(opts.readonly);

  if (opts.image_offset_str) {
    std::string image_offset{opts.image_offset_str};

    try {
      fsopts.image_offset = image_offset == "auto"
                                ? filesystem_options::IMAGE_OFFSET_AUTO
                                : folly::to<file_off_t>(image_offset);
    } catch (...) {
      DWARFS_THROW(runtime_error, "failed to parse offset: " + image_offset);
    }
  }

  constexpr int inode_offset =
#ifdef FUSE_ROOT_ID
      FUSE_ROOT_ID
#else
      0
#endif
      ;

  std::unordered_set<std::string> perfmon_enabled;
#if DWARFS_PERFMON_ENABLED
  if (opts.perfmon_enabled_str) {
    folly::splitTo<std::string>(
        ',', opts.perfmon_enabled_str,
        std::inserter(perfmon_enabled, perfmon_enabled.begin()));
  }
#endif

  userdata.perfmon = performance_monitor::create(perfmon_enabled);

  PERFMON_EXT_PROXY_SETUP(userdata, userdata.perfmon, "fuse")
  PERFMON_EXT_TIMER_SETUP(userdata, op_init)
  PERFMON_EXT_TIMER_SETUP(userdata, op_lookup)
  PERFMON_EXT_TIMER_SETUP(userdata, op_getattr)
  PERFMON_EXT_TIMER_SETUP(userdata, op_access)
  PERFMON_EXT_TIMER_SETUP(userdata, op_readlink)
  PERFMON_EXT_TIMER_SETUP(userdata, op_open)
  PERFMON_EXT_TIMER_SETUP(userdata, op_read)
  PERFMON_EXT_TIMER_SETUP(userdata, op_readdir)
  PERFMON_EXT_TIMER_SETUP(userdata, op_statfs)
  PERFMON_EXT_TIMER_SETUP(userdata, op_getxattr)
  PERFMON_EXT_TIMER_SETUP(userdata, op_listxattr)

  userdata.fs =
      filesystem_v2(userdata.lgr, std::make_shared<mmap>(opts.fsimage), fsopts,
                    inode_offset, userdata.perfmon);

  ti << "file system initialized";
}

int dwarfs_main(int argc, sys_char** argv) {
#ifdef _WIN32
  std::vector<std::string> argv_strings;
  std::vector<char*> argv_copy;
  argv_strings.reserve(argc);
  argv_copy.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    argv_strings.push_back(sys_string_to_string(argv[i]));
    argv_copy.push_back(argv_strings.back().data());
  }

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv_copy.data());
#else
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
#endif

  dwarfs_userdata userdata(std::cerr);
  auto& opts = userdata.opts;

  opts.progname = std::filesystem::path(argv[0]);
  opts.cache_image = 0;
  opts.cache_files = 1;

  fuse_opt_parse(&args, &opts, dwarfs_opts, option_hdl);

#if DWARFS_FUSE_LOWLEVEL
#if FUSE_USE_VERSION >= 30
  struct fuse_cmdline_opts fuse_opts;

  if (fuse_parse_cmdline(&args, &fuse_opts) == -1 || !fuse_opts.mountpoint) {
    usage(opts.progname);
  }

  if (fuse_opts.foreground) {
    folly::symbolizer::installFatalSignalHandler();
  }

  bool foreground = fuse_opts.foreground;
#else
  char* mountpoint = nullptr;
  int mt, fg;

  if (fuse_parse_cmdline(&args, &mountpoint, &mt, &fg) == -1 || !mountpoint) {
    usage(opts.progname);
  }

  if (fg) {
    folly::symbolizer::installFatalSignalHandler();
  }

  bool foreground = fg;
#endif
#endif

  try {
    // TODO: foreground mode, stderr vs. syslog?

    if (opts.debuglevel_str) {
      opts.debuglevel = logger::parse_level(opts.debuglevel_str);
    } else {
#if DWARFS_FUSE_LOWLEVEL
      opts.debuglevel = foreground ? logger::INFO : logger::WARN;
#else
      opts.debuglevel = logger::WARN;
#endif
    }

    userdata.lgr.set_threshold(opts.debuglevel);
    userdata.lgr.set_with_context(opts.debuglevel >= logger::DEBUG);

    opts.cachesize = opts.cachesize_str
                         ? parse_size_with_unit(opts.cachesize_str)
                         : (static_cast<size_t>(512) << 20);
    opts.workers = opts.workers_str ? folly::to<size_t>(opts.workers_str) : 2;
    opts.lock_mode =
        opts.mlock_str ? parse_mlock_mode(opts.mlock_str) : mlock_mode::NONE;
    opts.decompress_ratio = opts.decompress_ratio_str
                                ? folly::to<double>(opts.decompress_ratio_str)
                                : 0.8;

    if (opts.cache_tidy_strategy_str) {
      if (auto it = cache_tidy_strategy_map.find(opts.cache_tidy_strategy_str);
          it != cache_tidy_strategy_map.end()) {
        opts.block_cache_tidy_strategy = it->second;
      } else {
        std::cerr << "error: no such cache tidy strategy: "
                  << opts.cache_tidy_strategy_str << "\n";
        return 1;
      }

      if (opts.cache_tidy_interval_str) {
        opts.block_cache_tidy_interval =
            parse_time_with_unit(opts.cache_tidy_interval_str);
      }

      if (opts.cache_tidy_max_age_str) {
        opts.block_cache_tidy_max_age =
            parse_time_with_unit(opts.cache_tidy_max_age_str);
      }
    }
  } catch (runtime_error const& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  } catch (std::filesystem::filesystem_error const& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }

  if (opts.decompress_ratio < 0.0 || opts.decompress_ratio > 1.0) {
    std::cerr << "error: decratio must be between 0.0 and 1.0\n";
    return 1;
  }

  if (!opts.seen_mountpoint) {
    usage(opts.progname);
  }

  LOG_PROXY(debug_logger_policy, userdata.lgr);

  LOG_INFO << "dwarfs (" << PRJ_GIT_ID << ", fuse version " << FUSE_USE_VERSION
           << ")";

  try {
    if (userdata.opts.debuglevel >= logger::DEBUG) {
      load_filesystem<debug_logger_policy>(userdata);
    } else {
      load_filesystem<prod_logger_policy>(userdata);
    }
  } catch (std::exception const& e) {
    LOG_ERROR << "error initializing file system: " << e.what();
    return 1;
  }

  SCOPE_EXIT {
    if (userdata.perfmon) {
      userdata.perfmon->summarize(std::cerr);
    }
  };

#if FUSE_USE_VERSION >= 30
#if DWARFS_FUSE_LOWLEVEL
  return run_fuse(args, fuse_opts, userdata);
#else
  return run_fuse(args, userdata);
#endif
#else
  return run_fuse(args, mountpoint, mt, fg, userdata);
#endif
}

} // namespace dwarfs

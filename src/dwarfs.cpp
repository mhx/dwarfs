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

#include <iostream>
#include <stdexcept>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <folly/Conv.h>
#include <folly/experimental/symbolizer/SignalHandler.h>

#if FUSE_USE_VERSION >= 30
#include <fuse3/fuse_lowlevel.h>
#else
#include <fuse.h>
#include <fuse/fuse_lowlevel.h>
#endif

#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/util.h"
#include "dwarfs/version.h"

namespace dwarfs {

struct options {
  const char* progname;
  std::string fsimage;
  int seen_mountpoint;
  const char* cachesize_str;        // TODO: const?? -> use string?
  const char* debuglevel_str;       // TODO: const?? -> use string?
  const char* workers_str;          // TODO: const?? -> use string?
  const char* mlock_str;            // TODO: const?? -> use string?
  const char* decompress_ratio_str; // TODO: const?? -> use string?
  int enable_nlink;
  int readonly;
  int cache_image;
  int cache_files;
  size_t cachesize;
  size_t workers;
  mlock_mode lock_mode;
  double decompress_ratio;
  logger::level_type debuglevel;
  struct ::stat stat_defaults;
};

// TODO: better error handling

#define DWARFS_OPT(t, p, v)                                                    \
  { t, offsetof(struct options, p), v }

const struct fuse_opt dwarfs_opts[] = {
    // TODO: user, group, atime, mtime, ctime for those fs who don't have it?
    DWARFS_OPT("cachesize=%s", cachesize_str, 0),
    DWARFS_OPT("debuglevel=%s", debuglevel_str, 0),
    DWARFS_OPT("workers=%s", workers_str, 0),
    DWARFS_OPT("mlock=%s", mlock_str, 0),
    DWARFS_OPT("decratio=%s", decompress_ratio_str, 0),
    DWARFS_OPT("enable_nlink", enable_nlink, 1),
    DWARFS_OPT("readonly", readonly, 1),
    DWARFS_OPT("cache_image", cache_image, 1),
    DWARFS_OPT("no_cache_image", cache_image, 0),
    DWARFS_OPT("cache_files", cache_files, 1),
    DWARFS_OPT("no_cache_files", cache_files, 0),
    FUSE_OPT_END};

options s_opts;
stream_logger s_lgr(std::cerr);
std::shared_ptr<filesystem_v2> s_fs;
struct fuse_session* s_session;

template <typename LoggerPolicy>
void op_init(void* /*userdata*/, struct fuse_conn_info* /*conn*/) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  try {
    auto ti = LOG_TIMED_INFO;

    filesystem_options fsopts;
    fsopts.lock_mode = s_opts.lock_mode;
    fsopts.block_cache.max_bytes = s_opts.cachesize;
    fsopts.block_cache.num_workers = s_opts.workers;
    fsopts.block_cache.decompress_ratio = s_opts.decompress_ratio;
    fsopts.block_cache.mm_release = !s_opts.cache_image;
    fsopts.metadata.enable_nlink = bool(s_opts.enable_nlink);
    fsopts.metadata.readonly = bool(s_opts.readonly);
    s_fs = std::make_shared<filesystem_v2>(
        s_lgr, std::make_shared<mmap>(s_opts.fsimage), fsopts,
        &s_opts.stat_defaults, FUSE_ROOT_ID);

    ti << "file system initialized";
  } catch (std::exception const& e) {
    LOG_ERROR << "error initializing file system: " << e.what();
    fuse_session_exit(s_session);
  }
}

template <typename LoggerPolicy>
void op_destroy(void* /*userdata*/) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  s_fs.reset();
}

template <typename LoggerPolicy>
void op_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto entry = s_fs->find(parent, name);

    if (entry) {
      struct ::fuse_entry_param e;

      err = s_fs->getattr(*entry, &e.attr);

      if (err == 0) {
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

template <typename LoggerPolicy>
void op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info*) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto entry = s_fs->find(ino);

    if (entry) {
      struct ::stat stbuf;

      err = s_fs->getattr(*entry, &stbuf);

      if (err == 0) {
        fuse_reply_attr(req, &stbuf, std::numeric_limits<double>::max());

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

template <typename LoggerPolicy>
void op_access(fuse_req_t req, fuse_ino_t ino, int mode) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto entry = s_fs->find(ino);

    if (entry) {
      auto ctx = fuse_req_ctx(req);
      err = s_fs->access(*entry, mode, ctx->uid, ctx->gid);
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

template <typename LoggerPolicy>
void op_readlink(fuse_req_t req, fuse_ino_t ino) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto entry = s_fs->find(ino);

    if (entry) {
      std::string str;

      err = s_fs->readlink(*entry, &str);

      if (err == 0) {
        fuse_reply_readlink(req, str.c_str());

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

template <typename LoggerPolicy>
void op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto entry = s_fs->find(ino);

    if (entry) {
      if (S_ISDIR(entry->mode())) {
        err = EISDIR;
      } else if (fi->flags & (O_APPEND | O_CREAT | O_TRUNC)) {
        err = EACCES;
      } else {
        fi->fh = FUSE_ROOT_ID + entry->inode();
        fi->direct_io = !s_opts.cache_files;
        fi->keep_cache = s_opts.cache_files;
        fuse_reply_open(req, fi);
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

template <typename LoggerPolicy>
void op_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
             struct fuse_file_info* fi) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    if (fi->fh == ino) {
      iovec_read_buf buf;
      ssize_t rv = s_fs->readv(ino, buf, size, off);

      // std::cerr << ">>> " << rv << std::endl;

      if (rv >= 0) {
        fuse_reply_iov(req, buf.buf.empty() ? nullptr : &buf.buf[0],
                       buf.buf.size());

        return;
      }

      err = -rv;
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
} // namespace dwarfs

template <typename LoggerPolicy>
void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                struct fuse_file_info* /*fi*/) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto dirent = s_fs->find(ino);

    if (dirent) {
      auto dir = s_fs->opendir(*dirent);

      if (dir) {
        off_t lastoff = s_fs->dirsize(*dir);
        struct stat stbuf;
        std::vector<char> buf(size);
        size_t written = 0;

        while (off < lastoff) {
          auto res = s_fs->readdir(*dir, off);
          assert(res);

          auto [entry, name_view] = *res;
          std::string name(name_view);

          s_fs->getattr(entry, &stbuf);

          size_t needed =
              fuse_add_direntry(req, &buf[written], buf.size() - written,
                                name.c_str(), &stbuf, off + 1);

          if (written + needed > size) {
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

template <typename LoggerPolicy>
void op_statfs(fuse_req_t req, fuse_ino_t /*ino*/) {
  LOG_PROXY(LoggerPolicy, s_lgr);

  LOG_DEBUG << __func__;

  int err = EIO;

  try {
    struct ::statvfs buf;

    err = s_fs->statvfs(&buf);

    if (err == 0) {
      fuse_reply_statfs(req, &buf);

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

void usage(const char* progname) {
  std::cerr << "dwarfs (" << PRJ_GIT_ID << ", fuse version " << FUSE_USE_VERSION
            << ")\n\n"
            << "usage: " << progname << " image mountpoint [options]\n\n"
            << "DWARFS options:\n"
            << "    -o cachesize=SIZE      set size of block cache (512M)\n"
            << "    -o workers=NUM         number of worker threads (2)\n"
            << "    -o mlock=NAME          mlock mode: (none), try, must\n"
            << "    -o decratio=NUM        ratio for full decompression (0.8)\n"
            << "    -o enable_nlink        show correct hardlink numbers\n"
            << "    -o readonly            show read-only file system\n"
            << "    -o (no_)cache_image    (don't) keep image in kernel cache\n"
            << "    -o (no_)cache_files    (don't) keep files in kernel cache\n"
            << "    -o debuglevel=NAME     error, warn, (info), debug, trace\n"
            << std::endl;

#if FUSE_USE_VERSION >= 30
  fuse_cmdline_help();
#else
  struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, progname);
  fuse_opt_add_arg(&args, "-ho");
  struct fuse_operations fsops;
  ::memset(&fsops, 0, sizeof(fsops));
  fuse_main(args.argc, args.argv, &fsops, nullptr);
  fuse_opt_free_args(&args);
#endif

  ::exit(1);
}

int option_hdl(void* data, const char* arg, int key,
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

    opts->fsimage = arg;

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

template <typename LoggerPolicy>
void init_lowlevel_ops(struct fuse_lowlevel_ops& ops) {
  ops.init = &op_init<LoggerPolicy>;
  ops.destroy = &op_destroy<LoggerPolicy>;
  ops.lookup = &op_lookup<LoggerPolicy>;
  ops.getattr = &op_getattr<LoggerPolicy>;
  ops.access = &op_access<LoggerPolicy>;
  ops.readlink = &op_readlink<LoggerPolicy>;
  ops.open = &op_open<LoggerPolicy>;
  ops.read = &op_read<LoggerPolicy>;
  ops.readdir = &op_readdir<LoggerPolicy>;
  ops.statfs = &op_statfs<LoggerPolicy>;
  // ops.getxattr = &op_getxattr<LoggerPolicy>;
  // ops.listxattr = &op_listxattr<LoggerPolicy>;
}

#if FUSE_USE_VERSION > 30

int run_fuse(struct fuse_args& args,
             struct fuse_cmdline_opts const& fuse_opts) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  if (s_opts.debuglevel >= logger::DEBUG) {
    init_lowlevel_ops<debug_logger_policy>(fsops);
  } else {
    init_lowlevel_ops<prod_logger_policy>(fsops);
  }

  s_session = fuse_session_new(&args, &fsops, sizeof(fsops), nullptr);
  int err = 1;

  if (s_session) {
    if (fuse_set_signal_handlers(s_session) == 0) {
      if (fuse_session_mount(s_session, fuse_opts.mountpoint) == 0) {
        if (fuse_daemonize(fuse_opts.foreground) == 0) {
          if (fuse_opts.singlethread) {
            err = fuse_session_loop(s_session);
          } else {
            struct fuse_loop_config config;
            config.clone_fd = fuse_opts.clone_fd;
            config.max_idle_threads = fuse_opts.max_idle_threads;
            err = fuse_session_loop_mt(s_session, &config);
          }
        }
        fuse_session_unmount(s_session);
      }
      fuse_remove_signal_handlers(s_session);
    }
    fuse_session_destroy(s_session);
  }

  ::free(fuse_opts.mountpoint);
  fuse_opt_free_args(&args);

  return err;
}

#else

int run_fuse(struct fuse_args& args, char* mountpoint, int mt, int fg) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  if (s_opts.debuglevel >= logger::DEBUG) {
    init_lowlevel_ops<debug_logger_policy>(fsops);
  } else {
    init_lowlevel_ops<prod_logger_policy>(fsops);
  }

  int err = 1;

  if (auto ch = fuse_mount(mountpoint, &args)) {
    if (auto se = fuse_lowlevel_new(&args, &fsops, sizeof(fsops), nullptr)) {
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

int run_dwarfs(int argc, char* argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  s_opts.progname = argv[0];
  s_opts.cache_image = 0;
  s_opts.cache_files = 1;

  fuse_opt_parse(&args, &s_opts, dwarfs_opts, option_hdl);

#if FUSE_USE_VERSION >= 30
  struct fuse_cmdline_opts fuse_opts;

  if (fuse_parse_cmdline(&args, &fuse_opts) == -1 || !fuse_opts.mountpoint) {
    usage(s_opts.progname);
  }

  if (fuse_opts.foreground) {
    folly::symbolizer::installFatalSignalHandler();
  }
#else
  char* mountpoint = nullptr;
  int mt, fg;

  if (fuse_parse_cmdline(&args, &mountpoint, &mt, &fg) == -1 || !mountpoint) {
    usage(s_opts.progname);
  }

  if (fg) {
    folly::symbolizer::installFatalSignalHandler();
  }
#endif

  try {
    // TODO: foreground mode, stderr vs. syslog?

    s_opts.fsimage = std::filesystem::canonical(s_opts.fsimage).native();

    s_opts.debuglevel = s_opts.debuglevel_str
                            ? logger::parse_level(s_opts.debuglevel_str)
                            : logger::INFO;

    s_lgr.set_threshold(s_opts.debuglevel);
    s_lgr.set_with_context(s_opts.debuglevel >= logger::DEBUG);

    s_opts.cachesize = s_opts.cachesize_str
                           ? parse_size_with_unit(s_opts.cachesize_str)
                           : (static_cast<size_t>(512) << 20);
    s_opts.workers =
        s_opts.workers_str ? folly::to<size_t>(s_opts.workers_str) : 2;
    s_opts.lock_mode = s_opts.mlock_str ? parse_mlock_mode(s_opts.mlock_str)
                                        : mlock_mode::NONE;
    s_opts.decompress_ratio =
        s_opts.decompress_ratio_str
            ? folly::to<double>(s_opts.decompress_ratio_str)
            : 0.8;
  } catch (runtime_error const& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  } catch (std::filesystem::filesystem_error const& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  if (s_opts.decompress_ratio < 0.0 || s_opts.decompress_ratio > 1.0) {
    std::cerr << "error: decratio must be between 0.0 and 1.0" << std::endl;
    return 1;
  }

  if (!s_opts.seen_mountpoint) {
    usage(s_opts.progname);
  }

  LOG_PROXY(debug_logger_policy, s_lgr);

  LOG_INFO << "dwarfs (" << PRJ_GIT_ID << ", fuse version " << FUSE_USE_VERSION
           << ")";

  metadata_v2::get_stat_defaults(&s_opts.stat_defaults);

#if FUSE_USE_VERSION >= 30
  return run_fuse(args, fuse_opts);
#else
  return run_fuse(args, mountpoint, mt, fg);
#endif
}

} // namespace dwarfs

int main(int argc, char* argv[]) {
  return dwarfs::safe_main([&] { return dwarfs::run_dwarfs(argc, argv); });
}

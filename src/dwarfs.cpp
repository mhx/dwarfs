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

#include <fuse3/fuse_lowlevel.h>

#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/util.h"

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

// #define DEBUG_FUNC(x) std::cerr << __func__ << "(" << x << ")" << std::endl;
#define DEBUG_FUNC(x)

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

void op_init(void* /*userdata*/, struct fuse_conn_info* /*conn*/) {
  DEBUG_FUNC("")

  log_proxy<debug_logger_policy> log(s_lgr);

  try {
    auto ti = log.timed_info();

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
    log.error() << "error initializing file system: " << e.what();
    fuse_session_exit(s_session);
  }
}

void op_destroy(void* /*userdata*/) {
  DEBUG_FUNC("")
  s_fs.reset();
}

void op_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  DEBUG_FUNC(parent << ", " << name)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info*) {
  DEBUG_FUNC(ino)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_access(fuse_req_t req, fuse_ino_t ino, int mode) {
  DEBUG_FUNC(ino)

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto entry = s_fs->find(ino);

    if (entry) {
      auto ctx = fuse_req_ctx(req);
      err = s_fs->access(*entry, mode, ctx->uid, ctx->gid);
    }
  } catch (dwarfs::system_error const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_readlink(fuse_req_t req, fuse_ino_t ino) {
  DEBUG_FUNC(ino)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  DEBUG_FUNC(ino)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
             struct fuse_file_info* fi) {
  DEBUG_FUNC(ino << ", " << size << ", " << off)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
} // namespace dwarfs

void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                struct fuse_file_info* /*fi*/) {
  DEBUG_FUNC(ino << ", " << size << ", " << off)

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
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_statfs(fuse_req_t req, fuse_ino_t /*ino*/) {
  DEBUG_FUNC("")

  int err = EIO;

  try {
    struct ::statvfs buf;

    err = s_fs->statvfs(&buf);

    if (err == 0) {
      fuse_reply_statfs(req, &buf);

      return;
    }
  } catch (dwarfs::system_error const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void usage(const char* progname) {
  std::cerr << "dwarfs (c) Marcus Holland-Moritz\n\n"
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

  fuse_cmdline_help();

  ::exit(1);
}

int option_hdl(void* data, const char* arg, int key,
               struct fuse_args* /*outargs*/) {
  options* opts = reinterpret_cast<options*>(data);

  switch (key) {
  case FUSE_OPT_KEY_NONOPT:
    if (opts->seen_mountpoint) {
      return -1;
    }

    if (!opts->fsimage.empty()) {
      opts->seen_mountpoint = 1;
      return 1;
    }

    opts->fsimage = std::filesystem::canonical(arg).native();

    return 0;

  case FUSE_OPT_KEY_OPT:
    if (::strncmp(arg, "-h", 2) == 0 || ::strncmp(arg, "--help", 6) == 0) {
      usage(opts->progname);
    }
    break;
  }

  return 1;
}

int run_fuse(struct fuse_args& args,
             struct fuse_cmdline_opts const& fuse_opts) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  fsops.init = op_init;
  fsops.destroy = op_destroy;
  fsops.lookup = op_lookup;
  fsops.getattr = op_getattr;
  fsops.access = op_access;
  fsops.readlink = op_readlink;
  fsops.open = op_open;
  fsops.read = op_read;
  fsops.readdir = op_readdir;
  fsops.statfs = op_statfs;
  // fsops.getxattr = op_getxattr;
  // fsops.listxattr = op_listxattr;

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

} // namespace dwarfs

int main(int argc, char* argv[]) {
  using namespace dwarfs;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  s_opts.progname = argv[0];
  s_opts.cache_image = 0;
  s_opts.cache_files = 1;

  fuse_opt_parse(&args, &s_opts, dwarfs_opts, option_hdl);

  struct fuse_cmdline_opts fuse_opts;

  if (fuse_parse_cmdline(&args, &fuse_opts) == -1 || !fuse_opts.mountpoint) {
    usage(s_opts.progname);
  }

  if (fuse_opts.foreground) {
    folly::symbolizer::installFatalSignalHandler();
  }

  // TODO: foreground mode, stderr vs. syslog?
  s_opts.debuglevel = s_opts.debuglevel_str
                          ? logger::parse_level(s_opts.debuglevel_str)
                          : logger::INFO;

  s_lgr.set_threshold(s_opts.debuglevel);
  log_proxy<debug_logger_policy> log(s_lgr);

  s_opts.cachesize = s_opts.cachesize_str
                         ? parse_size_with_unit(s_opts.cachesize_str)
                         : (static_cast<size_t>(512) << 20);
  s_opts.workers =
      s_opts.workers_str ? folly::to<size_t>(s_opts.workers_str) : 2;
  s_opts.lock_mode =
      s_opts.mlock_str ? parse_mlock_mode(s_opts.mlock_str) : mlock_mode::NONE;
  s_opts.decompress_ratio = s_opts.decompress_ratio_str
                                ? folly::to<double>(s_opts.decompress_ratio_str)
                                : 0.8;

  log.info() << "dwarfs (" << DWARFS_VERSION << ")";

  if (!s_opts.seen_mountpoint) {
    usage(s_opts.progname);
  }

  metadata_v2::get_stat_defaults(&s_opts.stat_defaults);

  return run_fuse(args, fuse_opts);
}

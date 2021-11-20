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
  const char* progname{nullptr};
  std::string fsimage;
  int seen_mountpoint{0};
  const char* cachesize_str{nullptr};           // TODO: const?? -> use string?
  const char* debuglevel_str{nullptr};          // TODO: const?? -> use string?
  const char* workers_str{nullptr};             // TODO: const?? -> use string?
  const char* mlock_str{nullptr};               // TODO: const?? -> use string?
  const char* decompress_ratio_str{nullptr};    // TODO: const?? -> use string?
  const char* image_offset_str{nullptr};        // TODO: const?? -> use string?
  const char* cache_tidy_strategy_str{nullptr}; // TODO: const?? -> use string?
  const char* cache_tidy_interval_str{nullptr}; // TODO: const?? -> use string?
  const char* cache_tidy_max_age_str{nullptr};  // TODO: const?? -> use string?
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
    FUSE_OPT_END};

std::unordered_map<std::string_view, cache_tidy_strategy> const
    cache_tidy_strategy_map{
        {"none", cache_tidy_strategy::NONE},
        {"time", cache_tidy_strategy::EXPIRY_TIME},
        {"swap", cache_tidy_strategy::BLOCK_SWAPPED_OUT},
    };

#define dUSERDATA                                                              \
  auto userdata = reinterpret_cast<dwarfs_userdata*>(fuse_req_userdata(req))

template <typename LoggerPolicy>
void op_init(void* data, struct fuse_conn_info* /*conn*/) {
  auto userdata = reinterpret_cast<dwarfs_userdata*>(data);
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

template <typename LoggerPolicy>
void op_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << parent << ", " << name << ")";

  int err = ENOENT;

  try {
    auto entry = userdata->fs.find(parent, name);

    if (entry) {
      struct ::fuse_entry_param e;

      err = userdata->fs.getattr(*entry, &e.attr);

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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ")";

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto entry = userdata->fs.find(ino);

    if (entry) {
      struct ::stat stbuf;

      err = userdata->fs.getattr(*entry, &stbuf);

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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto entry = userdata->fs.find(ino);

    if (entry) {
      auto ctx = fuse_req_ctx(req);
      err = userdata->fs.access(*entry, mode, ctx->uid, ctx->gid);
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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto entry = userdata->fs.find(ino);

    if (entry) {
      std::string str;

      err = userdata->fs.readlink(*entry, &str);

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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto entry = userdata->fs.find(ino);

    if (entry) {
      if (S_ISDIR(entry->mode())) {
        err = EISDIR;
      } else if (fi->flags & (O_APPEND | O_CREAT | O_TRUNC)) {
        err = EACCES;
      } else {
        fi->fh = FUSE_ROOT_ID + entry->inode_num();
        fi->direct_io = !userdata->opts.cache_files;
        fi->keep_cache = userdata->opts.cache_files;
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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    if (fi->fh == ino) {
      iovec_read_buf buf;
      ssize_t rv = userdata->fs.readv(ino, buf, size, off);

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
}

template <typename LoggerPolicy>
void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                struct fuse_file_info* /*fi*/) {
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = ENOENT;

  try {
    auto dirent = userdata->fs.find(ino);

    if (dirent) {
      auto dir = userdata->fs.opendir(*dirent);

      if (dir) {
        off_t lastoff = userdata->fs.dirsize(*dir);
        struct stat stbuf;
        std::vector<char> buf(size);
        size_t written = 0;

        while (off < lastoff) {
          auto res = userdata->fs.readdir(*dir, off);
          assert(res);

          auto [entry, name_view] = *res;
          std::string name(name_view);

          userdata->fs.getattr(entry, &stbuf);

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
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__;

  int err = EIO;

  try {
    struct ::statvfs buf;

    err = userdata->fs.statvfs(&buf);

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

template <typename LoggerPolicy>
void op_getxattr(fuse_req_t req, fuse_ino_t ino, char const* name,
                 size_t size) {
  dUSERDATA;
  LOG_PROXY(LoggerPolicy, userdata->lgr);

  LOG_DEBUG << __func__ << "(" << ino << ", " << name << ", " << size << ")";

  static constexpr std::string_view pid_xattr{"user.dwarfs.driver.pid"};
  int err = ENODATA;

  try {
    if (ino == FUSE_ROOT_ID && name == pid_xattr) {
      auto pidstr = std::to_string(::getpid());
      if (size > 0) {
        fuse_reply_buf(req, pidstr.data(), pidstr.size());
      } else {
        fuse_reply_xattr(req, pidstr.size());
      }
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
  std::cerr
      << "dwarfs (" << PRJ_GIT_ID << ", fuse version " << FUSE_USE_VERSION
      << ")\n\n"
      << "usage: " << progname << " image mountpoint [options]\n\n"
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
      << "    -o debuglevel=NAME     error, warn, (info), debug, trace\n"
      << "    -o tidy_strategy=NAME  (none)|time|swap\n"
      << "    -o tidy_interval=TIME  interval for cache tidying (5m)\n"
      << "    -o tidy_max_age=TIME   tidy blocks after this time (10m)\n"
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
  ops.lookup = &op_lookup<LoggerPolicy>;
  ops.getattr = &op_getattr<LoggerPolicy>;
  ops.access = &op_access<LoggerPolicy>;
  ops.readlink = &op_readlink<LoggerPolicy>;
  ops.open = &op_open<LoggerPolicy>;
  ops.read = &op_read<LoggerPolicy>;
  ops.readdir = &op_readdir<LoggerPolicy>;
  ops.statfs = &op_statfs<LoggerPolicy>;
  ops.getxattr = &op_getxattr<LoggerPolicy>;
  // ops.listxattr = &op_listxattr<LoggerPolicy>;
}

#if FUSE_USE_VERSION > 30

int run_fuse(struct fuse_args& args, struct fuse_cmdline_opts const& fuse_opts,
             dwarfs_userdata& userdata) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  if (userdata.opts.debuglevel >= logger::DEBUG) {
    init_lowlevel_ops<debug_logger_policy>(fsops);
  } else {
    init_lowlevel_ops<prod_logger_policy>(fsops);
  }

  int err = 1;

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
  fuse_opt_free_args(&args);

  return err;
}

#else

int run_fuse(struct fuse_args& args, char* mountpoint, int mt, int fg,
             dwarfs_userdata& userdata) {
  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  if (userdata.opts.debuglevel >= logger::DEBUG) {
    init_lowlevel_ops<debug_logger_policy>(fsops);
  } else {
    init_lowlevel_ops<prod_logger_policy>(fsops);
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
                                : folly::to<off_t>(image_offset);
    } catch (...) {
      DWARFS_THROW(runtime_error, "failed to parse offset: " + image_offset);
    }
  }

  userdata.fs = filesystem_v2(
      userdata.lgr, std::make_shared<mmap>(opts.fsimage), fsopts, FUSE_ROOT_ID);

  ti << "file system initialized";
}

int run_dwarfs(int argc, char** argv) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  dwarfs_userdata userdata(std::cerr);
  auto& opts = userdata.opts;

  opts.progname = argv[0];
  opts.cache_image = 0;
  opts.cache_files = 1;

  fuse_opt_parse(&args, &opts, dwarfs_opts, option_hdl);

#if FUSE_USE_VERSION >= 30
  struct fuse_cmdline_opts fuse_opts;

  if (fuse_parse_cmdline(&args, &fuse_opts) == -1 || !fuse_opts.mountpoint) {
    usage(opts.progname);
  }

  if (fuse_opts.foreground) {
    folly::symbolizer::installFatalSignalHandler();
  }
#else
  char* mountpoint = nullptr;
  int mt, fg;

  if (fuse_parse_cmdline(&args, &mountpoint, &mt, &fg) == -1 || !mountpoint) {
    usage(opts.progname);
  }

  if (fg) {
    folly::symbolizer::installFatalSignalHandler();
  }
#endif

  try {
    // TODO: foreground mode, stderr vs. syslog?

    opts.fsimage = std::filesystem::canonical(opts.fsimage).native();

    opts.debuglevel = opts.debuglevel_str
                          ? logger::parse_level(opts.debuglevel_str)
                          : logger::INFO;

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
                  << opts.cache_tidy_strategy_str << std::endl;
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
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  } catch (std::filesystem::filesystem_error const& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  if (opts.decompress_ratio < 0.0 || opts.decompress_ratio > 1.0) {
    std::cerr << "error: decratio must be between 0.0 and 1.0" << std::endl;
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

#if FUSE_USE_VERSION >= 30
  return run_fuse(args, fuse_opts, userdata);
#else
  return run_fuse(args, mountpoint, mt, fg, userdata);
#endif
}

} // namespace dwarfs

int main(int argc, char** argv) {
  return dwarfs::safe_main([&] { return dwarfs::run_dwarfs(argc, argv); });
}

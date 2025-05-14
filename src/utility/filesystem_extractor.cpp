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

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

// This is required to avoid Windows.h being pulled in by libarchive
// and polluting our environment with all sorts of shit.
#ifdef _WIN32
#include <folly/portability/Windows.h>
#endif

#include <archive.h>
#include <archive_entry.h>

#include <folly/ExceptionString.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Unistd.h>
#include <folly/system/ThreadName.h>

#include <dwarfs/config.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/glob_matcher.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/filesystem_extractor.h>
#include <dwarfs/vfs_stat.h>

#include <dwarfs/internal/worker_group.h>

namespace dwarfs::utility {

namespace internal {

using namespace dwarfs::internal;

namespace {

class cache_semaphore {
 public:
  void post(int64_t n) {
    {
      std::lock_guard lock(mx_);
      size_ += n;
      ++count_;
    }
    condition_.notify_one();
  }

  void wait(int64_t n) {
    std::unique_lock lock(mx_);
    while (size_ < n && count_ <= 0) {
      condition_.wait(lock);
    }
    size_ -= n;
    --count_;
  }

 private:
  std::mutex mx_;
  std::condition_variable condition_;
  int64_t count_{0};
  int64_t size_{0};
};

class archive_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

} // namespace

template <typename LoggerPolicy>
class filesystem_extractor_ final : public filesystem_extractor::impl {
 public:
  explicit filesystem_extractor_(logger& lgr, os_access const& os)
      : LOG_PROXY_INIT(lgr)
      , os_{os} {}

  ~filesystem_extractor_() override {
    try {
      close();
    } catch (std::exception const& e) {
      LOG_ERROR << "close() failed in destructor: "
                << error_cp_to_utf8(e.what());
    } catch (...) {
      LOG_ERROR << "close() failed in destructor";
    }
  }

  void open_archive(std::filesystem::path const& output [[maybe_unused]],
                    std::string const& format [[maybe_unused]]) override {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    DWARFS_THROW(runtime_error, "open_archive() not supported in this build");
#else
    LOG_DEBUG << "opening archive file in " << format << " format";

    a_ = ::archive_write_new();

    check_result(::archive_write_set_format_by_name(a_, format.c_str()));
    check_result(::archive_write_set_bytes_in_last_block(a_, 1));

#ifdef _WIN32
    check_result(::archive_write_open_filename_w(
        a_, output.empty() ? nullptr : output.wstring().c_str()));
#else
    check_result(::archive_write_open_filename(
        a_, output.empty() ? nullptr : output.string().c_str()));
#endif
#endif
  }

  void open_stream(std::ostream& os [[maybe_unused]],
                   std::string const& format [[maybe_unused]]) override {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    DWARFS_THROW(runtime_error, "open_stream() not supported in this build");
#else
#ifdef _WIN32
    if (::_pipe(pipefd_.data(), 8192, _O_BINARY) != 0) {
      DWARFS_THROW(system_error, "_pipe()");
    }
#else
    if (::pipe(pipefd_.data()) != 0) {
      DWARFS_THROW(system_error, "pipe()");
    }
#endif

    iot_ = std::make_unique<std::thread>(
        [this, &os, fd = pipefd_[0]] { pump(os, fd); });

    LOG_DEBUG << "opening archive stream in " << format << " format";

    a_ = ::archive_write_new();

    check_result(::archive_write_set_format_by_name(a_, format.c_str()));
    check_result(::archive_write_set_bytes_in_last_block(a_, 1));
    check_result(::archive_write_open_fd(a_, pipefd_[1]));
#endif
  }

  void open_disk(std::filesystem::path const& output) override {
    if (!output.empty()) {
      std::filesystem::current_path(output);
    }

    a_ = ::archive_write_disk_new();

    check_result(::archive_write_disk_set_options(
        a_,
        ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME |
            ARCHIVE_EXTRACT_UNLINK | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS |
            ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS));
  }

  void close() override {
    if (a_) {
      LOG_DEBUG << "closing archive";
      check_result(::archive_write_close(a_));
      LOG_TRACE << "freeing archive";
      ::archive_write_free(a_);
      a_ = nullptr;
    }

    if (iot_) {
      LOG_TRACE << "closing pipe[1]";
      closefd(pipefd_[1]);

      LOG_TRACE << "joining I/O thread";
      iot_->join();
      iot_.reset();

      LOG_TRACE << "closing pipe[0]";
      closefd(pipefd_[0]);
    }
  }

  bool
  extract(reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
          filesystem_extractor_options const& opts) override;

 private:
  void closefd(int& fd) {
    if (fd >= 0) {
      if (::close(fd) != 0) {
        DWARFS_THROW(system_error, "close()");
      }
      fd = -1;
    }
  }

  void pump(std::ostream& os, int fd) {
    folly::setThreadName("pump");

    std::array<char, 1024> buf;

    for (;;) {
      // This is fine, we're simply reusing the buffer.
      // Flawfinder: ignore
      auto rv = ::read(fd, buf.data(), buf.size());

      LOG_TRACE << "read() returned " << rv;

      if (rv <= 0) {
        if (rv < 0) {
          LOG_ERROR << "read(): " << ::strerror(errno);
        }

        break;
      }

      os.write(buf.data(), rv);
    }
  }

  void check_result(int res) {
    switch (res) {
    case ARCHIVE_OK:
    case ARCHIVE_EOF:
    default:
      break;
    case ARCHIVE_WARN:
      LOG_WARN << std::string(archive_error_string(a_));
      break;
    case ARCHIVE_RETRY:
    case ARCHIVE_FAILED:
    case ARCHIVE_FATAL:
      throw archive_error(std::string(archive_error_string(a_)));
    }
  }

  LOG_PROXY_DECL(debug_logger_policy);
  os_access const& os_;
  struct ::archive* a_{nullptr};
  std::array<int, 2> pipefd_{-1, -1};
  std::unique_ptr<std::thread> iot_;
};

template <typename LoggerPolicy>
bool filesystem_extractor_<LoggerPolicy>::extract(
    reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
    filesystem_extractor_options const& opts) {
  DWARFS_CHECK(a_, "filesystem not opened");

  auto lr = ::archive_entry_linkresolver_new();

  scope_exit free_resolver{[&] { ::archive_entry_linkresolver_free(lr); }};

  if (auto fmt = ::archive_format(a_)) {
    ::archive_entry_linkresolver_set_strategy(lr, fmt);
  }

  ::archive_entry* sparse = nullptr;

  worker_group archiver(LOG_GET_LOGGER, os_, "archiver", 1);
  cache_semaphore sem;

  LOG_DEBUG << "extractor semaphore size: " << opts.max_queued_bytes
            << " bytes";

  sem.post(opts.max_queued_bytes);

  vfs_stat vfs;
  fs.statvfs(&vfs);

  std::atomic<size_t> hard_error{0};
  std::atomic<size_t> soft_error{0};
  std::atomic<uint64_t> bytes_written{0};
  uint64_t const bytes_total{vfs.blocks};

  auto do_archive =
      [&](std::shared_ptr<::archive_entry> const& ae,
          reader::inode_view const& entry) { // TODO: inode vs. entry
        if (auto size = ::archive_entry_size(ae.get());
            entry.is_regular_file() && size > 0) {
          auto fd = fs.open(entry);
          std::string_view path{::archive_entry_pathname(ae.get())};
          size_t pos = 0;
          size_t remain = size;

          while (remain > 0 && hard_error == 0) {
            size_t bs =
                remain < opts.max_queued_bytes ? remain : opts.max_queued_bytes;

            sem.wait(bs);

            std::error_code ec;
            auto ranges = fs.readv(fd, bs, pos, ec);

            if (!ec) {
              archiver.add_job([this, &sem, &hard_error, &soft_error, &opts,
                                ranges = std::move(ranges), ae, pos, bs, size,
                                path, &bytes_written, bytes_total]() mutable {
                try {
                  if (pos == 0) {
                    LOG_DEBUG << "extracting " << path << " (" << size
                              << " bytes)";
                    check_result(::archive_write_header(a_, ae.get()));
                  }
                  for (auto& r : ranges) {
                    auto br = r.get();
                    LOG_TRACE << "[" << pos << "] writing " << br.size()
                              << " bytes for " << path;
                    check_result(
                        ::archive_write_data(a_, br.data(), br.size()));
                    if (opts.progress) {
                      bytes_written += br.size();
                      opts.progress(path, bytes_written, bytes_total);
                    }
                  }
                  sem.post(bs);
                } catch (archive_error const& e) {
                  LOG_ERROR << exception_str(e);
                  ++hard_error;
                } catch (...) {
                  if (opts.continue_on_error) {
                    LOG_WARN << exception_str(std::current_exception());
                    ++soft_error;
                  } else {
                    LOG_ERROR << exception_str(std::current_exception());
                    ++hard_error;
                  }
                }
              });
            } else {
              LOG_ERROR << "error reading " << bs << " bytes at offset " << pos
                        << " from  inode [" << fd << "]: " << ec.message();
              ++hard_error;
              break;
            }

            pos += bs;
            remain -= bs;
          }
        } else {
          archiver.add_job([this, ae, &hard_error] {
            try {
              check_result(::archive_write_header(a_, ae.get()));
            } catch (...) {
              LOG_ERROR << exception_str(std::current_exception());
              ++hard_error;
            }
          });
        }
      };

  // Don't use an unordered_set<std::filesystem::path> here, this will break
  // on macOS 13 due to clang not knowing how to hash std::filesystem::path.
  std::unordered_set<std::string> matched_dirs;

  if (matcher) {
    // Collect all directories that contain matching files to make sure
    // we descend into them during the extraction walk below.
    fs.walk([&](auto entry) {
      if (!entry.inode().is_directory()) {
        if (matcher->match(entry.unix_path())) {
          while (auto parent = entry.parent()) {
            if (!matched_dirs.insert(parent->unix_path()).second) {
              break;
            }
            entry = *parent;
          }
        }
      }
    });
  }

  // Workaround for weird MSVC bug...
  using struct_stat = struct stat;

  fs.walk_data_order([&](auto const& entry) {
    // TODO: we can surely early abort walk() somehow
    if (entry.is_root() || hard_error) {
      return;
    }

    auto inode = entry.inode();

    if (matcher) {
      auto const unix_path = entry.unix_path();
      LOG_TRACE << "checking " << unix_path;
      if (inode.is_directory()) {
        if (!matched_dirs.contains(unix_path)) {
          LOG_TRACE << "skipping directory " << unix_path;
          // no need to extract this directory
          return;
        }
      } else {
        if (!matcher->match(unix_path)) {
          LOG_TRACE << "skipping " << unix_path;
          // no match, skip this entry
          return;
        }
      }
    }

    auto ae = ::archive_entry_new();
    auto stbuf = fs.getattr(inode);

    struct_stat st{};

#ifdef _WIN32
    stbuf.copy_to_without_block_info(&st);
#else
    stbuf.copy_to(&st);
#endif

#ifdef _WIN32
    ::archive_entry_copy_pathname_w(ae, entry.wpath().c_str());
#else
    ::archive_entry_copy_pathname(ae, entry.path().c_str());
#endif
    ::archive_entry_copy_stat(ae, &st);

    if (inode.is_symlink()) {
      std::error_code ec;
      auto link = fs.readlink(inode, ec);
      if (ec) {
        LOG_ERROR << "readlink() failed: " << ec.message();
      }
      if (opts.progress) {
        bytes_written += link.size();
      }
#ifdef _WIN32
      std::filesystem::path linkpath(string_to_u8string(link));
      ::archive_entry_copy_symlink_w(ae, linkpath.wstring().c_str());
#else
      ::archive_entry_copy_symlink(ae, link.c_str());
#endif
    }

    ::archive_entry_linkify(lr, &ae, &sparse);

    auto shared_entry_ptr = [](::archive_entry* e) {
      return std::shared_ptr<::archive_entry>(e, ::archive_entry_free);
    };

    if (ae) {
      do_archive(shared_entry_ptr(ae), inode);
    }

    if (sparse) {
      auto ev = fs.find(::archive_entry_ino(sparse));
      if (!ev) {
        LOG_ERROR << "find() failed";
      }
      LOG_INFO << "archiving sparse entry " << ::archive_entry_pathname(sparse);
      do_archive(shared_entry_ptr(sparse), *ev);
    }
  });

  archiver.wait();

  if (hard_error) {
    DWARFS_THROW(runtime_error, "extraction aborted");
  }

  // As we're visiting *all* hardlinks, we should never see any deferred
  // entries.
  ::archive_entry* ae = nullptr;
  ::archive_entry_linkify(lr, &ae, &sparse);
  if (ae) {
    ::archive_entry_free(ae);
    DWARFS_THROW(runtime_error, "unexpected deferred entry");
  }

  if (soft_error > 0) {
    LOG_ERROR << "extraction finished with " << soft_error << " error(s)";
    return false;
  }

  LOG_INFO << "extraction finished without errors";

  return true;
}

} // namespace internal

filesystem_extractor::filesystem_extractor(logger& lgr, os_access const& os)
    : impl_(make_unique_logging_object<filesystem_extractor::impl,
                                       internal::filesystem_extractor_,
                                       logger_policies>(lgr, os)) {}

void filesystem_extractor::add_library_dependencies(
    library_dependencies& deps) {
  deps.add_library(::archive_version_string());
}

} // namespace dwarfs::utility

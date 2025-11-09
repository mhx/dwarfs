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

#include <fmt/format.h>

#include <folly/ExceptionString.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Unistd.h>
#include <folly/system/ThreadName.h>

#include <dwarfs/config.h>
#include <dwarfs/counting_semaphore.h>
#include <dwarfs/file_access.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/glob_matcher.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/filesystem_extractor.h>
#include <dwarfs/utility/filesystem_extractor_archive_format.h>
#include <dwarfs/vfs_stat.h>

#include <dwarfs/internal/worker_group.h>

namespace dwarfs::utility {

namespace internal {

using namespace dwarfs::internal;

namespace {

class archive_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class sparse_file_mode {
  auto_detect,
  no_sparse,
  sparse_archive,
  sparse_disk,
};

sparse_file_mode get_sparse_file_mode_for_format(int format) {
  switch (format) {
  case ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE:
  case ARCHIVE_FORMAT_TAR_PAX_RESTRICTED:
    // I *think* these are the only formats that support sparse files.
    return sparse_file_mode::sparse_archive;

  default:
    break;
  }

  return sparse_file_mode::no_sparse;
}

la_ssize_t write_range_data(sparse_file_mode mode, struct archive* a,
                            void const* data, size_t size, la_int64_t offset) {
  if (mode == sparse_file_mode::sparse_disk) {
    auto const rv = ::archive_write_data_block(a, data, size, offset);
    if (rv == ARCHIVE_OK) {
      return static_cast<la_ssize_t>(size);
    }
    return rv;
  }

  return ::archive_write_data(a, data, size);
}

} // namespace

template <typename LoggerPolicy>
class filesystem_extractor_ final : public filesystem_extractor::impl {
 public:
  explicit filesystem_extractor_(logger& lgr, os_access const& os,
                                 std::shared_ptr<file_access const> fa)
      : LOG_PROXY_INIT(lgr)
      , os_{os}
      , fa_{std::move(fa)} {}

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
                    filesystem_extractor_archive_format const& format
                    [[maybe_unused]]) override {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    DWARFS_THROW(runtime_error, "open_archive() not supported in this build");
#else
    LOG_DEBUG << "opening archive file in " << format.description();

    a_ = ::archive_write_new();

    configure_format(format, &output);

    if (output.empty()) {
      check_result(::archive_write_open_filename(a_, nullptr));
    } else {
      out_ = fa_->open_output_binary(output);
      check_result(::archive_write_open2(a_, this, nullptr, on_stream_write,
                                         on_stream_close, on_stream_free));
    }
#endif
  }

  void open_stream(std::ostream& os [[maybe_unused]],
                   filesystem_extractor_archive_format const& format
                   [[maybe_unused]]) override {
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

    LOG_DEBUG << "opening archive stream in " << format.description();

    a_ = ::archive_write_new();

    configure_format(format);

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

    sparse_mode_ = sparse_file_mode::sparse_disk;
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
  static la_ssize_t on_stream_write(struct archive* /*a*/, void* client_data,
                                    void const* buffer, size_t length) {
    auto self = static_cast<filesystem_extractor_*>(client_data);
    auto& os = self->out_->os();
    os.write(static_cast<char const*>(buffer), length);
    return os.good() ? static_cast<la_ssize_t>(length) : -1;
  }

  static int on_stream_close(struct archive* /*a*/, void* client_data) {
    auto self = static_cast<filesystem_extractor_*>(client_data);
    self->out_->close();
    return ARCHIVE_OK;
  }

  static int on_stream_free(struct archive* /*a*/, void* client_data) {
    auto self = static_cast<filesystem_extractor_*>(client_data);
    self->out_.reset();
    return ARCHIVE_OK;
  }

  void configure_format(filesystem_extractor_archive_format const& format
                        [[maybe_unused]],
                        std::filesystem::path const* output
                        [[maybe_unused]] = nullptr) {
#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
    if (format.name == "auto") {
      if (!output || output->empty()) {
        DWARFS_THROW(runtime_error, "auto format requires output path");
      }

      if (!format.filters.empty()) {
        DWARFS_THROW(runtime_error, "auto format does not support filters");
      }

      auto fn = output->filename().string();

      LOG_DEBUG << "setting archive format by extension for " << fn;
      check_result(::archive_write_set_format_filter_by_ext(a_, fn.c_str()));
    } else {
      check_result(::archive_write_set_format_by_name(a_, format.name.c_str()));

      for (auto const& filter : format.filters) {
        check_result(::archive_write_add_filter_by_name(a_, filter.c_str()));
      }
    }

    check_result(::archive_write_set_options(a_, format.options.c_str()));
    check_result(::archive_write_set_bytes_in_last_block(a_, 1));
#endif
  }

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
  std::shared_ptr<file_access const> fa_;
  struct ::archive* a_{nullptr};
  std::unique_ptr<output_stream> out_;
  std::array<int, 2> pipefd_{-1, -1};
  std::unique_ptr<std::thread> iot_;
  sparse_file_mode sparse_mode_{sparse_file_mode::auto_detect};
};

template <typename LoggerPolicy>
bool filesystem_extractor_<LoggerPolicy>::extract(
    reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
    filesystem_extractor_options const& opts) {
  DWARFS_CHECK(a_, "filesystem not opened");

  auto sparse_mode = sparse_mode_;

  auto lr = ::archive_entry_linkresolver_new();

  scope_exit free_resolver{[&] { ::archive_entry_linkresolver_free(lr); }};

  if (auto fmt = ::archive_format(a_)) {
    ::archive_entry_linkresolver_set_strategy(lr, fmt);

    if (sparse_mode == sparse_file_mode::auto_detect) {
      sparse_mode = get_sparse_file_mode_for_format(fmt);
    }
  }

  ::archive_entry* sparse = nullptr;

  LOG_DEBUG << "extractor semaphore size: " << opts.max_queued_bytes
            << " bytes";

  counting_semaphore sem;
  sem.post(opts.max_queued_bytes);

  worker_group archiver(LOG_GET_LOGGER, os_, "archiver", 1);

  vfs_stat vfs;
  fs.statvfs(&vfs);

  std::atomic<size_t> hard_error{0};
  std::atomic<size_t> soft_error{0};
  std::atomic<uint64_t> bytes_written{0};
  uint64_t const bytes_total{vfs.blocks};

  auto do_archive =
      [&](std::shared_ptr<::archive_entry> ae,
          reader::inode_view const& entry) { // TODO: inode vs. entry
        bool added{false};

        if (auto const size = ::archive_entry_size(ae.get());
            entry.is_regular_file() && size > 0) {
          reader::detail::file_reader fr(fs, entry);

          auto extents = fr.extents();
          std::vector<file_range> data_ranges;

          for (auto const& e : extents) {
            if (sparse_mode != sparse_file_mode::sparse_disk ||
                e.kind == dwarfs::extent_kind::data) {
              data_ranges.push_back(e.range);
            }
          }

          archiver.add_job([this, &hard_error, &soft_error, &opts,
                            extents = std::move(extents),
                            ranges = fr.read_sequential(data_ranges, sem,
                                                        opts.max_queued_bytes),
                            ae = std::move(ae), size, &sparse_mode,
                            &bytes_written, bytes_total]() mutable {
            try {
              assert(ae);

              auto const path = ::archive_entry_pathname(ae.get());

              LOG_DEBUG << "extracting " << path << " (" << size << " bytes)";

              bool const hole_only =
                  extents.size() == 1 && extents[0].kind == extent_kind::hole;

              if (hole_only || extents.size() > 1) {
                LOG_DEBUG << "sparse file " << path << " with "
                          << extents.size() << " extents";

                if (sparse_mode != sparse_file_mode::no_sparse) {
                  if (hole_only) {
                    LOG_DEBUG << "no data extents found for sparse file, "
                                 "adding dummy sparse entry";
                    ::archive_entry_sparse_add_entry(ae.get(), size, 0);
                  } else {
                    for (auto const& e : extents) {
                      if (e.kind == dwarfs::extent_kind::data) {
                        LOG_DEBUG << "  data offset=" << e.range.offset()
                                  << ", size=" << e.range.size();
                        ::archive_entry_sparse_add_entry(
                            ae.get(), e.range.offset(), e.range.size());
                      }
                    }
                  }
                }
              }

              check_result(::archive_write_header(a_, ae.get()));

              if (sparse_mode == sparse_file_mode::sparse_disk) {
                extents.erase(std::remove_if(extents.begin(), extents.end(),
                                             [](auto const& e) {
                                               return e.kind ==
                                                      dwarfs::extent_kind::hole;
                                             }),
                              extents.end());
              }

              for (auto const& r : ranges) {
                assert(!extents.empty());
                auto& ext = extents.front();
                assert(!ext.range.empty());

                LOG_TRACE << "writing " << r.size() << " bytes at offset "
                          << ext.range.offset() << " in " << ext.kind
                          << " extent for " << path;

                auto const rv = write_range_data(sparse_mode, a_, r.data(),
                                                 r.size(), ext.range.offset());

                check_result(rv);

                if (std::cmp_not_equal(rv, static_cast<la_ssize_t>(r.size()))) {
                  throw archive_error(
                      fmt::format("short write: {} != {}", rv, r.size()));
                }

                assert(rv <= static_cast<la_ssize_t>(ext.range.size()));

                ext.range.advance(rv);

                if (ext.range.empty()) {
                  extents.erase(extents.begin());
                }

                if (opts.progress) {
                  bytes_written += r.size();
                  opts.progress(path, bytes_written, bytes_total);
                }
              }

              assert(extents.empty());
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

          added = true;
        }

        if (!added) {
          archiver.add_job([this, ae = std::move(ae), &hard_error] {
            try {
              check_result(::archive_write_header(a_, ae.get()));
            } catch (...) {
              LOG_ERROR << exception_str(std::current_exception());
              ++hard_error;
            }
          });
        }
      };

  auto do_archive_entry = [&](auto const& entry) {
    auto ae = ::archive_entry_new();
    auto inode = entry.inode();
    auto stat = fs.getattr(inode);

#ifdef _WIN32
    ::archive_entry_copy_pathname_w(ae, entry.wpath().c_str());
#else
    ::archive_entry_copy_pathname(ae, entry.path().c_str());
#endif

    stat.ensure_valid(file_stat::all_valid);

    ::archive_entry_set_atime(ae, stat.atime_unchecked(),
                              stat.atime_nsec_unchecked());
    ::archive_entry_set_ctime(ae, stat.ctime_unchecked(),
                              stat.ctime_nsec_unchecked());
    ::archive_entry_set_mtime(ae, stat.mtime_unchecked(),
                              stat.mtime_nsec_unchecked());
    ::archive_entry_unset_birthtime(ae);
    ::archive_entry_set_dev(ae, stat.dev_unchecked());
    ::archive_entry_set_gid(ae, stat.gid_unchecked());
    ::archive_entry_set_uid(ae, stat.uid_unchecked());
    ::archive_entry_set_ino(ae, stat.ino_unchecked());
    ::archive_entry_set_nlink(ae, stat.nlink_unchecked());
    ::archive_entry_set_rdev(ae, stat.rdev_unchecked());
    ::archive_entry_set_size(ae, stat.size_unchecked());
    ::archive_entry_set_mode(ae, stat.mode_unchecked());

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

  for (auto const& entry : fs.entries_in_data_order()) {
    if (hard_error) {
      break;
    }

    if (entry.is_root()) {
      continue;
    }

    if (matcher) {
      auto const unix_path = entry.unix_path();
      LOG_TRACE << "checking " << unix_path;
      if (entry.inode().is_directory()) {
        if (!matched_dirs.contains(unix_path)) {
          LOG_TRACE << "skipping directory " << unix_path;
          // no need to extract this directory
          continue;
        }
      } else {
        if (!matcher->match(unix_path)) {
          LOG_TRACE << "skipping " << unix_path;
          // no match, skip this entry
          continue;
        }
      }
    }

    do_archive_entry(entry);
  }

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

filesystem_extractor::filesystem_extractor(
    logger& lgr, os_access const& os, std::shared_ptr<file_access const> fa)
    : impl_(make_unique_logging_object<filesystem_extractor::impl,
                                       internal::filesystem_extractor_,
                                       logger_policies>(lgr, os,
                                                        std::move(fa))) {}

void filesystem_extractor::add_library_dependencies(
    library_dependencies& deps) {
  deps.add_library(::archive_version_string());
}

} // namespace dwarfs::utility

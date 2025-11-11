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

bool format_supports_hardlinks(int const format) {
  switch (format) {
  case ARCHIVE_FORMAT_CPIO_SVR4_NOCRC:
  case ARCHIVE_FORMAT_CPIO_SVR4_CRC:
    return true;

  default:
    break;
  }

  int const fmtbase = format & ARCHIVE_FORMAT_BASE_MASK;

  switch (fmtbase) {
  case ARCHIVE_FORMAT_ISO9660:
  case ARCHIVE_FORMAT_SHAR:
  case ARCHIVE_FORMAT_TAR:
  case ARCHIVE_FORMAT_XAR:
    return true;

  default:
    break;
  }

  return false;
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
  using archive_ptr = std::shared_ptr<struct ::archive>;

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

    // return std::shared_ptr<::archive_entry>(e, ::archive_entry_free);
    a_.reset(::archive_write_new(), ::archive_write_free);

    configure_format(format, &output);

    if (output.empty()) {
      check_result(a_, ::archive_write_open_filename(a_.get(), nullptr));
    } else {
      out_ = fa_->open_output_binary(output);
      check_result(a_, ::archive_write_open2(a_.get(), this, nullptr,
                                             on_stream_write, on_stream_close,
                                             on_stream_free));
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

    a_.reset(::archive_write_new(), ::archive_write_free);

    configure_format(format);

    check_result(a_, ::archive_write_open_fd(a_.get(), pipefd_[1]));
#endif
  }

  void open_disk(std::filesystem::path const& output,
                 size_t num_data_writers) override {
    if (!output.empty()) {
      std::filesystem::current_path(output);
    }

    a_.reset(::archive_write_disk_new(), ::archive_write_free);

    check_result(
        a_, ::archive_write_disk_set_options(
                a_.get(), ARCHIVE_EXTRACT_NO_AUTODIR | ARCHIVE_EXTRACT_OWNER |
                              ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME |
                              ARCHIVE_EXTRACT_UNLINK |
                              ARCHIVE_EXTRACT_SECURE_SYMLINKS));

    for (size_t i = 0; i < num_data_writers; ++i) {
      auto ar = archive_ptr{::archive_write_disk_new(), ::archive_write_free};
      check_result(
          ar, ::archive_write_disk_set_options(
                  ar.get(), ARCHIVE_EXTRACT_NO_AUTODIR | ARCHIVE_EXTRACT_OWNER |
                                ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME |
                                ARCHIVE_EXTRACT_UNLINK));
      a_reg_.push_back(std::move(ar));
    }

    sparse_mode_ = sparse_file_mode::sparse_disk;
  }

  void close() override {
    if (!a_reg_.empty()) {
      for (auto& ar : a_reg_) {
        LOG_DEBUG << "closing regular file disk archive";
        check_result(ar, ::archive_write_close(ar.get()));
      }
      LOG_TRACE << "freeing regular file disk archives";
      a_reg_.clear();
    }

    if (a_) {
      LOG_DEBUG << "closing archive";
      check_result(a_, ::archive_write_close(a_.get()));
      LOG_TRACE << "freeing archive";
      a_.reset();
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

  filesystem_extractor::progress_info get_progress() const override {
    filesystem_extractor::progress_info pi;

    {
      std::lock_guard lock(bytes_total_mx_);
      if (bytes_total_) {
        pi.total_bytes = *bytes_total_;
      }
    }

    pi.extracted_bytes = bytes_written_.load();

    return pi;
  }

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
      check_result(
          a_, ::archive_write_set_format_filter_by_ext(a_.get(), fn.c_str()));
    } else {
      check_result(a_, ::archive_write_set_format_by_name(a_.get(),
                                                          format.name.c_str()));

      for (auto const& filter : format.filters) {
        check_result(
            a_, ::archive_write_add_filter_by_name(a_.get(), filter.c_str()));
      }
    }

    check_result(a_,
                 ::archive_write_set_options(a_.get(), format.options.c_str()));
    check_result(a_, ::archive_write_set_bytes_in_last_block(a_.get(), 1));
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

  void check_result(struct archive* a, int res) {
    switch (res) {
    case ARCHIVE_OK:
    case ARCHIVE_EOF:
    default:
      break;
    case ARCHIVE_WARN:
      LOG_WARN << std::string(::archive_error_string(a));
      break;
    case ARCHIVE_RETRY:
    case ARCHIVE_FAILED:
    case ARCHIVE_FATAL:
      throw archive_error(std::string(::archive_error_string(a)));
    }
  }

  void check_result(archive_ptr const& a, int res) {
    check_result(a.get(), res);
  }

  LOG_PROXY_DECL(debug_logger_policy);
  os_access const& os_;
  std::shared_ptr<file_access const> fa_;
  archive_ptr a_;
  std::vector<archive_ptr> a_reg_;
  std::unique_ptr<output_stream> out_;
  std::array<int, 2> pipefd_{-1, -1};
  std::unique_ptr<std::thread> iot_;
  sparse_file_mode sparse_mode_{sparse_file_mode::auto_detect};
  std::atomic<uint64_t> bytes_written_{0};
  std::mutex mutable bytes_total_mx_;
  std::optional<uint64_t> bytes_total_{};
};

template <typename LoggerPolicy>
bool filesystem_extractor_<LoggerPolicy>::extract(
    reader::filesystem_v2_lite const& fs, glob_matcher const* matcher,
    filesystem_extractor_options const& opts) {
  DWARFS_CHECK(a_, "filesystem not opened");

  auto sparse_mode = sparse_mode_;
  bool supports_hardlinks{true};

  auto lr = ::archive_entry_linkresolver_new();

  scope_exit free_resolver{[&] { ::archive_entry_linkresolver_free(lr); }};

  if (auto fmt = ::archive_format(a_.get())) {
    LOG_DEBUG << "setting link resolver strategy for format " << fmt;

    ::archive_entry_linkresolver_set_strategy(lr, fmt);

    if (sparse_mode == sparse_file_mode::auto_detect) {
      sparse_mode = get_sparse_file_mode_for_format(fmt);
    }

    supports_hardlinks = format_supports_hardlinks(fmt);
  }

  ::archive_entry* spare = nullptr;

  LOG_DEBUG << "extractor semaphore size: " << opts.max_queued_bytes
            << " bytes";

  counting_semaphore sem;
  sem.post(opts.max_queued_bytes);

  using worker_ptr = std::shared_ptr<worker_group>;

  worker_ptr archiver = std::make_shared<worker_group>(
      LOG_GET_LOGGER, os_, "archiver", 1, [this](size_t) {
        return std::make_unique<basic_thread_state<struct archive*>>(a_.get());
      });
  worker_ptr reg_archiver;

  if (a_reg_.empty()) {
    reg_archiver = archiver;
  } else {
    reg_archiver = std::make_shared<worker_group>(
        LOG_GET_LOGGER, os_, "arch-reg", a_reg_.size(), [this](size_t idx) {
          return std::make_unique<basic_thread_state<struct archive*>>(
              a_reg_[idx].get());
        });
  }

  std::atomic<size_t> hard_error{0};
  std::atomic<size_t> soft_error{0};

  auto do_archive = [&](worker_ptr aptr, std::shared_ptr<::archive_entry> ae,
                        reader::inode_view const& entry) {
    // hard links will have size 0
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

      aptr->add_job<struct archive*>(
          [this, &hard_error, &soft_error, &opts, extents = std::move(extents),
           ranges = fr.read_sequential(data_ranges, sem, opts.max_queued_bytes),
           ae = std::move(ae), size, &sparse_mode](struct archive* a) mutable {
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

              check_result(a, ::archive_write_header(a, ae.get()));

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

                auto const rv = write_range_data(sparse_mode, a, r.data(),
                                                 r.size(), ext.range.offset());

                check_result(a, rv);

                if (std::cmp_not_equal(rv, static_cast<la_ssize_t>(r.size()))) {
                  throw archive_error(
                      fmt::format("short write: {} != {}", rv, r.size()));
                }

                assert(rv <= static_cast<la_ssize_t>(ext.range.size()));

                ext.range.advance(rv);

                if (ext.range.empty()) {
                  extents.erase(extents.begin());
                }

                if (opts.enable_progress) {
                  bytes_written_ += r.size();
                  LOG_TRACE << "progress: " << bytes_written_.load()
                            << " bytes written";
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
    } else {
      aptr->add_job<struct archive*>(
          [this, ae = std::move(ae), &hard_error](struct archive* a) {
            try {
              check_result(a, ::archive_write_header(a, ae.get()));
            } catch (...) {
              LOG_ERROR << exception_str(std::current_exception());
              ++hard_error;
            }
          });
    }
  };

  // Asynchronously prepare walking all entries in data order
  auto ordered_entries = std::async(
      std::launch::async, [&fs] { return fs.entries_in_data_order(); });

  // Don't use an unordered_set<std::filesystem::path> here, this will break
  // on macOS 13 due to clang not knowing how to hash std::filesystem::path.
  std::unordered_set<std::string> matched_dirs;

  if (matcher) {
    std::unordered_set<uint32_t> seen_hardlinks;
    uint64_t match_count{0};
    uint64_t data_size{0};

    // Collect all directories that contain matching files to make sure
    // we descend into them during the extraction walk below.
    fs.walk([&](auto entry) {
      auto inode = entry.inode();
      if (!inode.is_directory()) {
        if (matcher->match(entry.unix_path())) {
          ++match_count;

          if (opts.enable_progress) {
            auto stat = fs.getattr(inode);
            if (!supports_hardlinks || stat.nlink() == 1 ||
                seen_hardlinks.insert(inode.inode_num()).second) {
              data_size += sparse_mode == sparse_file_mode::sparse_disk
                               ? stat.allocated_size()
                               : stat.size();
            }
          }

          while (auto parent = entry.parent()) {
            if (!matched_dirs.insert(parent->unix_path()).second) {
              break;
            }
            entry = *parent;
          }
        }
      }
    });

    if (opts.enable_progress) {
      std::lock_guard lock(bytes_total_mx_);
      bytes_total_.emplace(data_size);
    }

    if (match_count == 0) {
      LOG_WARN << "no matching files found";
      return true;
    }

    LOG_VERBOSE << "found " << match_count << " matching files";
  } else if (opts.enable_progress) {
    vfs_stat vfs;
    fs.statvfs(&vfs);

    uint64_t data_size;

    if (sparse_mode == sparse_file_mode::sparse_disk) {
      data_size = vfs.total_allocated_fs_size;
    } else if (supports_hardlinks) {
      data_size = vfs.total_fs_size;
    } else {
      data_size = vfs.total_fs_size + vfs.total_hardlink_size;
    }

    std::lock_guard lock(bytes_total_mx_);
    bytes_total_.emplace(data_size);
  }

  if (opts.enable_progress) {
    LOG_DEBUG << "progress: " << bytes_total_.value()
              << " total bytes to extract";
  }

  auto shared_entry_ptr = [](::archive_entry* e) {
    return std::shared_ptr<::archive_entry>(e, ::archive_entry_free);
  };

  auto do_archive_entry = [&](auto const& entry) {
    if (entry.is_root()) {
      // skip root entry
      return;
    }

    auto inode = entry.inode();

    switch (inode.type()) {
    case posix_file_type::block:
    case posix_file_type::character:
      if (opts.skip_devices) {
        LOG_TRACE << "skipping device " << entry.unix_path();
        return;
      }
      break;

    case posix_file_type::socket:
    case posix_file_type::fifo:
      if (opts.skip_specials) {
        LOG_TRACE << "skipping special file " << entry.unix_path();
        return;
      }
      break;

    default:
      break;
    }

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
      if (opts.enable_progress) {
        bytes_written_ += link.size();
        LOG_TRACE << "progress: " << bytes_written_.load() << " bytes written";
      }
#ifdef _WIN32
      std::filesystem::path linkpath(string_to_u8string(link));
      ::archive_entry_copy_symlink_w(ae, linkpath.wstring().c_str());
#else
      ::archive_entry_copy_symlink(ae, link.c_str());
#endif
    }

    ::archive_entry_linkify(lr, &ae, &spare);

    if (ae) {
      do_archive(inode.is_regular_file() && stat.nlink_unchecked() == 1
                     ? reg_archiver
                     : archiver,
                 shared_entry_ptr(ae), inode);
    }

    if (spare) {
      auto ev = fs.find(::archive_entry_ino(spare));
      DWARFS_CHECK(ev, "find() failed for spare hard link entry");
      LOG_DEBUG << "archiving spare entry " << ::archive_entry_pathname(spare);
      do_archive(archiver, shared_entry_ptr(spare), *ev);
    }
  };

  for (auto const& entry : fs.directory_entries()) {
    if (hard_error) {
      break;
    }

    do_archive_entry(entry);
  }

  archiver->wait();

  for (auto const& entry : ordered_entries.get()) {
    if (hard_error) {
      break;
    }

    if (entry.inode().is_directory()) {
      // directories have already been processed above
      continue;
    }

    do_archive_entry(entry);
  }

  archiver->wait();
  reg_archiver->wait();

  if (hard_error) {
    DWARFS_THROW(runtime_error, "extraction aborted");
  }

  // process any deferred hard link entries
  {
    ::archive_entry* ae = nullptr;
    ::archive_entry_linkify(lr, &ae, &spare);

    if (ae) {
      do {
        auto ev = fs.find(::archive_entry_ino(ae));

        DWARFS_CHECK(ev, "find() failed for deferred hard link entry");

        LOG_DEBUG << "archiving deferred entry "
                  << ::archive_entry_pathname(ae);

        do_archive(archiver, shared_entry_ptr(ae), *ev);

        ae = nullptr;
        ::archive_entry_linkify(lr, &ae, &spare);
      } while (ae);

      archiver->wait();
    }
  }

  if (opts.enable_progress) {
    DWARFS_CHECK(bytes_written_.load() == bytes_total_.value(),
                 fmt::format("progress mismatch: {} (written) != {} (total)",
                             bytes_written_.load(), bytes_total_.value()));

    LOG_DEBUG << "progress: " << bytes_written_.load() << "/"
              << bytes_total_.value() << " bytes written";
  }

  if (soft_error > 0) {
    LOG_ERROR << "extraction finished with " << soft_error << " error(s)";
    return false;
  }

  LOG_VERBOSE << "extraction finished without errors";

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

bool filesystem_extractor::supports_format(
    filesystem_extractor_archive_format const& format [[maybe_unused]]) {
#ifdef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
  return false;
#else
  std::unique_ptr<struct ::archive, decltype(&::archive_write_free)> ap(
      ::archive_write_new(), ::archive_write_free);

  auto supported = [&](auto fn, std::string const& name,
                       bool accept_warn = false) {
    auto const res = fn(ap.get(), name.c_str());
    return res == ARCHIVE_OK || (accept_warn && res == ARCHIVE_WARN);
  };

  return supported(&::archive_write_set_format_by_name, format.name) &&
         std::ranges::all_of(format.filters,
                             [&](auto const& filter) {
                               return supported(
                                   &::archive_write_add_filter_by_name, filter,
                                   true);
                             }) &&
         supported(&::archive_write_set_options, format.options);
#endif
}

} // namespace dwarfs::utility

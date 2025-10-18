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

#include <folly/portability/Windows.h>
#include <winioctl.h>

#include <dwarfs/detail/file_extent_info.h>
#include <dwarfs/error.h>
#include <dwarfs/scope_exit.h>

#include <dwarfs/internal/io_ops.h>

namespace dwarfs::internal {

using dwarfs::detail::file_extent_info;

namespace {

uint64_t get_alloc_granularity() {
  static uint64_t const granularity = [] {
    ::SYSTEM_INFO info;
    ::GetSystemInfo(&info);
    return info.dwAllocationGranularity;
  }();
  return granularity;
}

size_t
win_pread(HANDLE h, void* buf, size_t n, uint64_t offset, std::error_code& ec) {
  auto out = static_cast<uint8_t*>(buf);
  size_t total = 0;

  while (total < n) {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFFu);

    HANDLE evt = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!evt) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return 0;
    }

    scope_exit close_evt([evt] { ::CloseHandle(evt); });

    ov.hEvent = evt;

    auto to_read = static_cast<DWORD>(
        std::min<size_t>(n - total, std::numeric_limits<DWORD>::max()));
    DWORD done = 0;

    if (!::ReadFile(h, out + total, to_read, nullptr, &ov)) {
      auto const err = ::GetLastError();
      switch (err) {
      case ERROR_HANDLE_EOF:
        done = 0;
        break;
      case ERROR_IO_PENDING:
        break;
      default:
        ec = std::error_code(err, std::system_category());
        return 0;
      }
    } else if (!GetOverlappedResult(h, &ov, &done, TRUE)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return 0;
    }

    total += done;
    offset += done;

    if (done < to_read) {
      break;
    }
  }

  return total;
}

std::vector<file_extent_info>
get_file_extents(HANDLE h, uint64_t size, std::error_code& ec) {
  std::vector<file_extent_info> extents;

  ec.clear();

  if (size == 0) {
    return extents;
  }

  std::vector<FILE_ALLOCATED_RANGE_BUFFER> ranges;
  FILE_ALLOCATED_RANGE_BUFFER in{};
  uint64_t next_start{0};

  constexpr size_t kMaxRangesPerCall{256};

  while (next_start < size) {
    in.FileOffset.QuadPart = static_cast<LONGLONG>(next_start);
    in.Length.QuadPart = static_cast<LONGLONG>(size - next_start);

    size_t const old_count = ranges.size();
    DWORD bytes{0};

    ranges.resize(old_count + kMaxRangesPerCall);

    BOOL ok = ::DeviceIoControl(
        h, FSCTL_QUERY_ALLOCATED_RANGES, &in, sizeof(in), &ranges[old_count],
        static_cast<DWORD>(kMaxRangesPerCall *
                           sizeof(FILE_ALLOCATED_RANGE_BUFFER)),
        &bytes, nullptr);

    size_t const count = bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
    ranges.resize(old_count + count);

    if (!ok) {
      if (auto const err = ::GetLastError(); err != ERROR_MORE_DATA) {
        ec = std::error_code(err, std::system_category());
        ranges.clear();
        break;
      }
    }

    if (count == 0) {
      break;
    }

    next_start =
        ranges.back().FileOffset.QuadPart + ranges.back().Length.QuadPart;
  }

  file_off_t last_end = 0;

  if (!ranges.empty()) {
    // coalesce adjacent ranges
    for (size_t i = 1; i < ranges.size(); ++i) {
      auto& prev = ranges[i - 1];
      auto& curr = ranges[i];

      if (prev.FileOffset.QuadPart + prev.Length.QuadPart ==
          curr.FileOffset.QuadPart) {
        prev.Length.QuadPart += curr.Length.QuadPart;
        curr.Length.QuadPart = 0;
      }
    }

    // remove empty ranges
    ranges.erase(
        std::remove_if(ranges.begin(), ranges.end(),
                       [](auto const& r) { return r.Length.QuadPart == 0; }),
        ranges.end());

    // convert to file_extent_info and add holes
    for (size_t i = 0; i < ranges.size(); ++i) {
      auto const& r = ranges[i];
      file_off_t const offset = static_cast<file_off_t>(r.FileOffset.QuadPart);
      file_size_t const length = static_cast<file_size_t>(r.Length.QuadPart);

      if (offset > last_end) {
        extents.emplace_back(extent_kind::hole,
                             file_range{last_end, offset - last_end});
      }

      extents.emplace_back(extent_kind::data, file_range{offset, length});
      last_end = offset + length;
    }
  }

  if (last_end < size) {
    extents.emplace_back(extent_kind::hole,
                         file_range{last_end, size - last_end});
  }

  return extents;
}

class io_ops_win : public io_ops {
 public:
  struct win_handle {
    HANDLE file{INVALID_HANDLE_VALUE};
    HANDLE mapping{nullptr};
    uint64_t size{0};
  };

  std::any
  open(std::filesystem::path const& path, std::error_code& ec) const override {
    ec.clear();

    HANDLE file =
        ::CreateFileW(path.c_str(), GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    LARGE_INTEGER size_li{};
    if (!::GetFileSizeEx(file, &size_li)) {
      ::CloseHandle(file);
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    DWARFS_CHECK(size_li.QuadPart > 0, "attempt to open an empty file");

    HANDLE mapping =
        ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

    if (!mapping) {
      ::CloseHandle(file);
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    return win_handle{file, mapping, static_cast<uint64_t>(size_li.QuadPart)};
  }

  void close(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      if (h->mapping && !::CloseHandle(h->mapping)) {
        ec = std::error_code(::GetLastError(), std::system_category());
      }
      if (h->file != INVALID_HANDLE_VALUE && !::CloseHandle(h->file)) {
        ec = std::error_code(::GetLastError(), std::system_category());
      }
    }
  }

  file_size_t size(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      return h->size;
    }

    return 0;
  }

  size_t granularity() const override {
    return static_cast<size_t>(get_alloc_granularity());
  }

  std::vector<file_extent_info>
  get_extents(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      return get_file_extents(h->file, h->size, ec);
    }
    return {};
  }

  size_t pread(std::any const& handle, void* buf, size_t size,
               file_off_t offset, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      return win_pread(h->file, buf, size, offset, ec);
    }
    return 0;
  }

  void* virtual_alloc(size_t size, memory_access access,
                      std::error_code& ec) const override {
    DWORD const prot =
        access == memory_access::readonly ? PAGE_READONLY : PAGE_READWRITE;
    auto const addr =
        ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, prot);

    if (!addr) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    return addr;
  }

  void
  virtual_free(void* addr, size_t size, std::error_code& ec) const override {
    if (!::VirtualFree(addr, 0, MEM_RELEASE)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

  void* map(std::any const& handle, file_off_t offset, size_t size,
            std::error_code& ec) const override {
    if (offset < 0) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return nullptr;
    }

    if (auto const* h = get_handle(handle, ec)) {
      auto const map_offset = static_cast<uint64_t>(offset);
      auto const off_low = static_cast<DWORD>(map_offset & 0xFFFFFFFF);
      auto const off_high = static_cast<DWORD>((map_offset >> 32) & 0xFFFFFFFF);

      auto const addr =
          ::MapViewOfFile(h->mapping, FILE_MAP_READ, off_high, off_low, size);

      if (addr == nullptr) {
        ec = std::error_code(::GetLastError(), std::system_category());
      }

      return addr;
    }

    return nullptr;
  }

  void unmap(void* addr, size_t /*size*/, std::error_code& ec) const override {
    if (!::UnmapViewOfFile(addr)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

  void advise(void* /*addr*/, size_t /*size*/, io_advice /*advice*/,
              std::error_code& /*ec*/) const override {
    // TODO: implement Windows equivalent of madvise
  }

  void lock(void* addr, size_t size, std::error_code& ec) const override {
    if (!::VirtualLock(addr, size)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

 private:
  win_handle const*
  get_handle(std::any const& handle, std::error_code& ec) const {
    auto const* h = std::any_cast<win_handle>(&handle);

    if (!h) {
      ec = std::make_error_code(std::errc::bad_file_descriptor);
    }

    return h;
  }
};

} // namespace

io_ops const& get_native_memory_mapping_ops() {
  static io_ops_win const ops;
  return ops;
}

} // namespace dwarfs::internal

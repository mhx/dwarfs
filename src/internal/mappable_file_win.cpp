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

#include <cassert>

#include <folly/portability/Windows.h>
#include <winioctl.h>

#include <dwarfs/detail/file_extent_info.h>
#include <dwarfs/scope_exit.h>

#include <dwarfs/internal/mappable_file.h>

namespace dwarfs::internal {

using dwarfs::detail::file_extent_info;
using dwarfs::internal::detail::memory_mapping_impl;

namespace {

uint64_t get_alloc_granularity() {
  static uint64_t const granularity = [] {
    ::SYSTEM_INFO info;
    ::GetSystemInfo(&info);
    return info.dwAllocationGranularity;
  }();
  return granularity;
}

void handle_error(char const* what, std::error_code* ec,
                  std::error_code const& error) {
  if (ec) {
    *ec = error;
  } else {
    throw std::system_error{error, what};
  }
}

void handle_error(char const* what, std::error_code* ec) {
  handle_error(what, ec,
               std::error_code(::GetLastError(), std::system_category()));
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

struct virtual_alloc_tag {};
constexpr virtual_alloc_tag virtual_alloc{};

class memory_mapping_win final : public memory_mapping_impl {
 public:
  memory_mapping_win(void* addr, size_t offset, size_t size, file_range range,
                     bool readonly)
      : addr_{addr}
      , offset_{offset}
      , size_{size}
      , range_{range}
      , readonly_{readonly} {}

  memory_mapping_win(virtual_alloc_tag, void* addr, size_t size, bool readonly)
      : addr_{addr}
      , offset_{0}
      , size_{size}
      , range_{0, size}
      , readonly_{readonly}
      , is_virtual_{true} {}

  memory_mapping_win() = default;
  memory_mapping_win(memory_mapping_win&&) = delete;
  memory_mapping_win& operator=(memory_mapping_win&&) = delete;
  memory_mapping_win(memory_mapping_win const&) = delete;
  memory_mapping_win& operator=(memory_mapping_win const&) = delete;

  ~memory_mapping_win() override {
    if (addr_ != nullptr) {
      deallocate(addr_);
    }
  }

  file_range range() const override { return range_; }

  std::span<std::byte> mutable_span() override {
    if (readonly_) {
      throw std::runtime_error("memory mapping is read-only");
    }
    return {reinterpret_cast<std::byte*>(addr_) + offset_, size_};
  }

  std::span<std::byte const> const_span() const override {
    return {reinterpret_cast<std::byte const*>(addr_) + offset_, size_};
  }

  void advise(io_advice, size_t, size_t, std::error_code*) const override {
    // TODO: Implement Windows equivalent of madvise
  }

  void lock(size_t offset, size_t size, std::error_code* ec) const override {
    auto const addr = reinterpret_cast<char*>(addr_) + offset_ + offset;

    if (::VirtualLock(addr, size) == 0) {
      handle_error("VirtualLock", ec);
    }
  }

 private:
  void deallocate(void* addr) const {
    if (is_virtual_) {
      auto const rv [[maybe_unused]] = ::VirtualFree(addr, 0, MEM_RELEASE);
      assert(rv);
    } else {
      auto const rv [[maybe_unused]] = ::UnmapViewOfFile(addr);
      assert(rv);
    }
  }

  void* addr_{nullptr};
  size_t offset_{0};
  size_t size_{0};
  file_range range_{};
  bool readonly_{true};
  bool is_virtual_{false};
};

class mappable_file_win final : public mappable_file::impl {
 public:
  mappable_file_win(HANDLE file, HANDLE mapping, file_size_t size)
      : file_{file}
      , mapping_{mapping}
      , size_{size} {}

  mappable_file_win() = default;
  mappable_file_win(mappable_file_win&& other) = delete;
  mappable_file_win& operator=(mappable_file_win&& other) = delete;
  mappable_file_win(mappable_file_win const&) = delete;
  mappable_file_win& operator=(mappable_file_win const&) = delete;

  ~mappable_file_win() override {
    if (mapping_) {
      ::CloseHandle(mapping_);
    }
    if (file_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(file_);
    }
  }

  file_size_t size(std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }
    return size_;
  }

  std::vector<file_extent_info>
  get_extents(std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    std::error_code local_ec;
    auto extents = get_file_extents(file_, size_, local_ec);

    if (local_ec) {
      handle_error("get_file_extents", ec, local_ec);
      return {};
    }

    return extents;
  }

  readonly_memory_mapping map_readonly(std::optional<file_range> range,
                                       std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    file_off_t offset = 0;
    file_size_t size = size_;

    if (range) {
      offset = range->offset();
      size = range->size();
    }

    auto const granularity = get_alloc_granularity();
    auto const misalign = offset % granularity;
    auto const map_offset = offset - misalign;
    auto const map_size = size + misalign;

    auto const off_low = static_cast<DWORD>(map_offset & 0xFFFFFFFF);
    auto const off_high = static_cast<DWORD>((map_offset >> 32) & 0xFFFFFFFF);

    auto const addr =
        ::MapViewOfFile(mapping_, FILE_MAP_READ, off_high, off_low, map_size);

    if (addr == nullptr) {
      handle_error("MapViewOfFile", ec);
      return {};
    }

    return readonly_memory_mapping{std::make_unique<memory_mapping_win>(
        addr, static_cast<size_t>(misalign), static_cast<size_t>(size),
        file_range{offset, size}, true)};
  }

  size_t read(std::span<std::byte> buffer, std::optional<file_range> range,
              std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    file_off_t offset = 0;
    file_size_t size = size_;

    if (range) {
      offset = range->offset();
      size = range->size();
    }

    size = std::min(size, static_cast<file_size_t>(buffer.size()));

    std::error_code read_ec;
    auto const n = win_pread(file_, buffer.data(), size, offset, read_ec);

    if (read_ec) {
      handle_error("win_pread", ec, read_ec);
      return 0;
    }

    return n;
  }

 private:
  HANDLE file_{INVALID_HANDLE_VALUE};
  HANDLE mapping_{nullptr};
  file_size_t size_{0};
};

std::unique_ptr<memory_mapping_win>
create_empty_mapping(size_t size, bool readonly, std::error_code& ec) {
  DWORD const prot = readonly ? PAGE_READONLY : PAGE_READWRITE;
  auto const addr =
      ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, prot);

  if (!addr) {
    ec = std::error_code(::GetLastError(), std::system_category());
    return {};
  }

  return std::make_unique<memory_mapping_win>(virtual_alloc, addr, size,
                                              readonly);
}

} // namespace

readonly_memory_mapping
mappable_file::map_empty_readonly(size_t size, std::error_code& ec) {
  return readonly_memory_mapping{create_empty_mapping(size, true, ec)};
}

readonly_memory_mapping mappable_file::map_empty_readonly(size_t size) {
  std::error_code ec;
  auto mapping = map_empty_readonly(size, ec);
  if (ec) {
    throw std::system_error{ec, "map_empty_readonly"};
  }
  return mapping;
}

memory_mapping mappable_file::map_empty(size_t size, std::error_code& ec) {
  return memory_mapping{create_empty_mapping(size, false, ec)};
}

memory_mapping mappable_file::map_empty(size_t size) {
  std::error_code ec;
  auto mapping = map_empty(size, ec);
  if (ec) {
    throw std::system_error{ec, "map_empty"};
  }
  return mapping;
}

mappable_file
mappable_file::create(std::filesystem::path const& path, std::error_code& ec) {
  ec.clear();

  HANDLE file = ::CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);

  if (file == INVALID_HANDLE_VALUE) {
    ec = std::error_code(::GetLastError(), std::system_category());
    return {};
  }

  LARGE_INTEGER size_li{};
  if (!GetFileSizeEx(file, &size_li)) {
    ::CloseHandle(file);
    ec = std::error_code(::GetLastError(), std::system_category());
    return {};
  }

  HANDLE mapping =
      CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

  if (!mapping) {
    ::CloseHandle(file);
    ec = std::error_code(::GetLastError(), std::system_category());
    return {};
  }

  return mappable_file{std::make_unique<mappable_file_win>(
      file, mapping, static_cast<file_size_t>(size_li.QuadPart))};
}

mappable_file mappable_file::create(std::filesystem::path const& path) {
  std::error_code ec;
  auto file = create(path, ec);
  if (ec) {
    throw std::system_error{ec, "create"};
  }
  return file;
}

} // namespace dwarfs::internal

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
#include <cerrno>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <dwarfs/error.h>

#include <dwarfs/internal/mappable_file.h>

namespace dwarfs::internal {

namespace {

uint64_t get_page_size() {
  static uint64_t const page_size = ::sysconf(_SC_PAGESIZE);
  return page_size;
}

int posix_advice(io_advice adv) {
  switch (adv) {
  case io_advice::normal:
    return MADV_NORMAL;
  case io_advice::random:
    return MADV_RANDOM;
  case io_advice::sequential:
    return MADV_SEQUENTIAL;
  case io_advice::willneed:
    return MADV_WILLNEED;
  case io_advice::dontneed:
    return MADV_DONTNEED;
  }

  DWARFS_PANIC("invalid advice");

  return MADV_NORMAL;
}

void handle_error(char const* what, std::error_code* ec) {
  if (ec) {
    ec->assign(errno, std::generic_category());
  } else {
    throw std::system_error{errno, std::generic_category(), what};
  }
}

class memory_mapping_posix final : public detail::memory_mapping_impl {
 public:
  memory_mapping_posix(void* addr, size_t offset, size_t size, file_range range,
                       bool readonly, size_t page_size)
      : addr_{addr}
      , offset_{offset}
      , size_{size}
      , range_{range}
      , readonly_{readonly}
      , page_size_{page_size} {}

  memory_mapping_posix() = default;
  memory_mapping_posix(memory_mapping_posix&&) = delete;
  memory_mapping_posix& operator=(memory_mapping_posix&&) = delete;
  memory_mapping_posix(memory_mapping_posix const&) = delete;
  memory_mapping_posix& operator=(memory_mapping_posix const&) = delete;

  ~memory_mapping_posix() override {
    if (addr_ != nullptr) {
      deallocate(addr_, offset_ + size_);
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

  void advise(io_advice advice, size_t offset, size_t size,
              std::error_code* ec) const override {
    offset += offset_;
    if (auto const misalign = offset % page_size_; misalign != 0) {
      offset -= misalign;
      size += misalign;
    }

    if (auto const misalign = size % page_size_; misalign != 0) {
      size += page_size_ - misalign;
    }

    auto const addr = reinterpret_cast<std::byte*>(addr_) + offset;
    auto const native_advice = posix_advice(advice);

    if (::madvise(addr, size, native_advice) != 0) {
      handle_error("madvise", ec);
    }
  }

  void lock(size_t offset, size_t size, std::error_code* ec) const override {
    offset += offset_;

    auto const addr = reinterpret_cast<std::byte*>(addr_) + offset;

    if (::mlock(addr, size) != 0) {
      handle_error("mlock", ec);
    }
  }

 private:
  static void deallocate(void* addr, size_t size) {
    auto const rv [[maybe_unused]] = ::munmap(addr, size);
    assert(rv == 0);
  }

  void* addr_{nullptr};
  size_t offset_{0};
  size_t size_{0};
  file_range range_{};
  bool readonly_{true};
  size_t page_size_{0};
};

class mappable_file_posix final : public mappable_file::impl {
 public:
  mappable_file_posix(int fd, file_size_t size)
      : fd_{fd}
      , size_{size} {}

  mappable_file_posix() = default;
  mappable_file_posix(mappable_file_posix&& other) = delete;
  mappable_file_posix& operator=(mappable_file_posix&& other) = delete;
  mappable_file_posix(mappable_file_posix const&) = delete;
  mappable_file_posix& operator=(mappable_file_posix const&) = delete;

  ~mappable_file_posix() override {
    if (fd_ != -1) {
      auto const rv [[maybe_unused]] = ::close(fd_);
      assert(rv == 0);
    }
  }

  file_size_t size(std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }
    return size_;
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

    auto const page_size = get_page_size();
    auto const misalign = offset % page_size;
    auto const map_offset = offset - misalign;
    auto const map_size = size + misalign;

    auto const addr =
        ::mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE, fd_, map_offset);

    if (addr == MAP_FAILED) {
      handle_error("mmap", ec);
      return {};
    }

    return readonly_memory_mapping{std::make_unique<memory_mapping_posix>(
        addr, static_cast<size_t>(misalign), static_cast<size_t>(size),
        file_range{offset, size}, true, page_size)};
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

    auto const rv = ::pread(fd_, buffer.data(), size, offset);

    if (rv == -1) {
      handle_error("pread", ec);
      return 0;
    }

    return static_cast<size_t>(rv);
  }

 private:
  int fd_{-1};
  file_size_t size_{0};
};

std::unique_ptr<memory_mapping_posix>
create_empty_mapping(size_t size, bool readonly, std::error_code& ec) {
  int const prot = readonly ? PROT_READ : PROT_READ | PROT_WRITE;
  auto const addr =
      ::mmap(nullptr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (addr == MAP_FAILED) {
    ec = std::error_code{errno, std::generic_category()};
    return {};
  }

  return std::make_unique<memory_mapping_posix>(
      addr, 0, size, file_range{0, size}, readonly, get_page_size());
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

  // NOLINTNEXTLINE: cppcoreguidelines-pro-type-vararg
  int fd = ::open(path.c_str(), O_RDONLY);

  if (fd == -1) {
    ec = std::error_code{errno, std::generic_category()};
    return {};
  }

  auto const size = ::lseek(fd, 0, SEEK_END);

  return mappable_file{std::make_unique<mappable_file_posix>(
      fd, static_cast<file_size_t>(size))};
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

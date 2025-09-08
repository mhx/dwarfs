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
#include <memory>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/mman.h>
#endif

#include <dwarfs/zero_memory.h>

namespace dwarfs {

namespace {

class zero_memory_impl final : public byte_buffer_interface {
 public:
  zero_memory_impl(size_t size)
      : data_{allocate(size)}
      , size_{size} {}

  ~zero_memory_impl() override { deallocate(data_, size_); }

  size_t size() const override { return size_; }

  size_t capacity() const override { return size_; }

  uint8_t const* data() const override {
    return static_cast<uint8_t const*>(data_);
  }

  std::span<uint8_t const> span() const override {
    return {this->data(), size_};
  }

 private:
  static void* allocate(size_t size) {
#ifdef _WIN32
    void* data =
        ::VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY);
    if (!data) {
      std::error_code ec(::GetLastError(), std::system_category());
      throw std::runtime_error("VirtualAlloc failed: " + ec.message());
    }
#else
    void* data =
        ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
      throw std::runtime_error("mmap failed");
    }
#endif
    return data;
  }

  static void deallocate(void* data, size_t size [[maybe_unused]]) {
#ifdef _WIN32
    auto rv [[maybe_unused]] = ::VirtualFree(data, 0, MEM_RELEASE);
    assert(rv);
#else
    auto rv [[maybe_unused]] = ::munmap(data, size);
    assert(rv == 0);
#endif
  }

  void* data_{nullptr};
  size_t size_{0};
};

} // namespace

shared_byte_buffer zero_memory::create(size_t size) {
  return shared_byte_buffer{std::make_shared<zero_memory_impl>(size)};
}

} // namespace dwarfs

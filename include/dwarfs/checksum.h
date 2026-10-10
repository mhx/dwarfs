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

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dwarfs {

class checksum {
 public:
  class digest_data {
   public:
    static constexpr inline std::size_t kMaxSize = 64;

    digest_data() = default;
    explicit digest_data(std::span<std::uint8_t const> data);

    bool has_value() const { return size_ > 0; }
    explicit operator bool() const { return has_value(); }

    void reset() { size_ = 0; }

    std::size_t size() const { return size_; }

    std::string hex() const;

    std::string_view view() const {
      assert(size_ <= kMaxSize);
      return {data_.data(), size_};
    }

    std::span<std::uint8_t const> span() const {
      assert(size_ <= kMaxSize);
      return {reinterpret_cast<std::uint8_t const*>(data_.data()), size_};
    }

   private:
    friend class checksum;

    std::array<char, kMaxSize> data_;
    std::size_t size_{0};
  };

  static bool is_available(std::string const& algo);
  static std::vector<std::string> available_algorithms();

  struct xxh3_64_tag {};
  struct sha2_512_256_tag {};
  struct blake3_256_tag {};

  static constexpr xxh3_64_tag xxh3_64{};
  static constexpr sha2_512_256_tag sha2_512_256{};
  static constexpr blake3_256_tag blake3_256{};

  static bool verify(xxh3_64_tag, void const* data, size_t size,
                     void const* digest, size_t digest_size);
  static bool verify(sha2_512_256_tag, void const* data, size_t size,
                     void const* digest, size_t digest_size);
  static bool verify(blake3_256_tag, void const* data, size_t size,
                     void const* digest, size_t digest_size);
  static bool verify(std::string const& alg, void const* data, size_t size,
                     void const* digest, size_t digest_size);

  explicit checksum(xxh3_64_tag);
  explicit checksum(sha2_512_256_tag);
  explicit checksum(blake3_256_tag);
  explicit checksum(std::string const& alg);

  checksum& update(void const* data, size_t size) {
    impl_->update(data, size);
    return *this;
  }

  checksum& update(std::span<std::byte const> data) {
    impl_->update(data.data(), data.size());
    return *this;
  }

  checksum& update(std::string_view data) {
    impl_->update(data.data(), data.size());
    return *this;
  }

  void reset() { impl_->reset(); }

  bool finalize(void* digest) const { return impl_->finalize(digest); }
  digest_data finalize() const;

  size_t digest_size() const { return impl_->digest_size(); }

  std::string digest() const { return impl_->digest(); }
  std::string hexdigest() const { return impl_->hexdigest(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void update(void const* data, size_t size) = 0;
    virtual void reset() = 0;
    virtual bool finalize(void* digest) = 0;
    virtual size_t digest_size() = 0;
    virtual std::string digest() = 0;
    virtual std::string hexdigest() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs

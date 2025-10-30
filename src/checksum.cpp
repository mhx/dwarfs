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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <optional>
#include <ostream>
#include <ranges>
#include <string_view>

#include <openssl/evp.h>

#include <xxhash.h>

#include <blake3.h>

#include <boost/algorithm/hex.hpp>

#include <fmt/format.h>

#include <dwarfs/checksum.h>
#include <dwarfs/error.h>

namespace dwarfs {

namespace {

using namespace std::string_view_literals;

constexpr std::array supported_algorithms{
    "xxh3-64"sv,
    "xxh3-128"sv,
    "blake3-256"sv,
};

constexpr std::array unsupported_algorithms{
    "shake128"sv,
    "shake256"sv,
};

std::string make_hexdigest(checksum::impl& cs) {
  std::array<char, EVP_MAX_MD_SIZE> tmp;
  auto dig_size = cs.digest_size();
  assert(dig_size <= tmp.size());
  if (!cs.finalize(tmp.data())) {
    throw std::runtime_error("failed to finalize digest");
  }
  std::string result;
  result.resize(dig_size * 2);
  boost::algorithm::hex_lower(tmp.begin(), tmp.begin() + dig_size,
                              result.begin());
  return result;
}

class checksum_evp : public checksum::impl {
 public:
  explicit checksum_evp(::EVP_MD const* evp)
      : context_{::EVP_MD_CTX_new(), &::EVP_MD_CTX_free}
      , dig_size_(::EVP_MD_size(evp)) {
    DWARFS_CHECK(::EVP_DigestInit(context_.get(), evp),
                 "EVP_DigestInit() failed");
  }

  void update(void const* data, size_t size) override {
    assert(context_);
    DWARFS_CHECK(::EVP_DigestUpdate(context_.get(), data, size),
                 "EVP_DigestUpdate() failed");
  }

  bool finalize(void* digest) override {
    if (!context_) {
      return false;
    }

    unsigned int dig_size = 0;
    bool rv = ::EVP_DigestFinal_ex(
        context_.get(), reinterpret_cast<unsigned char*>(digest), &dig_size);

    context_.reset();

    if (rv) {
      DWARFS_CHECK(
          dig_size_ == dig_size,
          fmt::format("digest size mismatch: {0} != {1}", dig_size_, dig_size));
    }

    return rv;
  }

  std::string hexdigest() override { return make_hexdigest(*this); }

  static std::vector<std::string> available_algorithms() {
    std::vector<std::string> available;
    ::EVP_MD_do_all(
        [](::EVP_MD const*, char const* from, char const* to, void* vec) {
          // TODO: C++23: use std::ranges::contains
          if (!to && std::ranges::find(unsupported_algorithms, from) ==
                         unsupported_algorithms.end()) {
            reinterpret_cast<std::vector<std::string>*>(vec)->emplace_back(
                from);
          }
        },
        &available);
    // NOLINTNEXTLINE(modernize-use-ranges)
    available.erase(std::remove_if(available.begin(), available.end(),
                                   std::not_fn(is_available)),
                    available.end());
    return available;
  }

  static bool is_available(std::string const& algo) {
    if (auto md = ::EVP_get_digestbyname(algo.c_str())) {
      ::EVP_MD_CTX* cx = ::EVP_MD_CTX_new();
      bool success = ::EVP_DigestInit(cx, md);
      ::EVP_MD_CTX_free(cx);
      return success;
    }
    return false;
  }

  size_t digest_size() override { return dig_size_; }

 private:
  std::unique_ptr<::EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)> context_;
  size_t const dig_size_;
};

struct xxh3_64_policy {
  using result_type = XXH64_hash_t;
  using canonical_type = XXH64_canonical_t;
  static constexpr auto reset = XXH3_64bits_reset;
  static constexpr auto update = XXH3_64bits_update;
  static constexpr auto digest = XXH3_64bits_digest;
  static constexpr auto canonical = XXH64_canonicalFromHash;
};

struct xxh3_128_policy {
  using result_type = XXH128_hash_t;
  using canonical_type = XXH128_canonical_t;
  static constexpr auto reset = XXH3_128bits_reset;
  static constexpr auto update = XXH3_128bits_update;
  static constexpr auto digest = XXH3_128bits_digest;
  static constexpr auto canonical = XXH128_canonicalFromHash;
};

template <typename Policy>
class checksum_xxh3 : public checksum::impl {
 public:
  checksum_xxh3()
      : state_{XXH3_createState(), &XXH3_freeState} {
    DWARFS_CHECK(Policy::reset(state_.get()) == XXH_OK, "XXH3 reset failed");
  }

  void update(void const* data, size_t size) override {
    assert(state_);
    auto err = Policy::update(state_.get(), data, size);
    DWARFS_CHECK(err == XXH_OK,
                 fmt::format("XXH3 update failed: {}", static_cast<int>(err)));
  }

  bool finalize(void* digest) override {
    if (!state_) {
      return false;
    }
    typename Policy::canonical_type canonical;
    Policy::canonical(&canonical, Policy::digest(state_.get()));
    state_.reset();
    // compat: we always store the digest in little-endian order :/
    std::ranges::reverse(canonical.digest);
    ::memcpy(digest, &canonical, sizeof(canonical));
    return true;
  }

  std::string hexdigest() override { return make_hexdigest(*this); }

  size_t digest_size() override {
    static_assert(
        sizeof(typename Policy::result_type) ==
        sizeof(typename decltype(std::function{Policy::digest})::result_type));
    return sizeof(typename Policy::result_type);
  }

 private:
  std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state_;
};

template <size_t DigestBytes>
class checksum_blake3 : public checksum::impl {
 public:
  explicit checksum_blake3() {
    hasher_.emplace();
    ::blake3_hasher_init(&hasher_.value());
  }

  void update(void const* data, size_t size) override {
    ::blake3_hasher_update(&hasher_.value(), data, size);
  }

  bool finalize(void* digest) override {
    if (!hasher_.has_value()) {
      return false;
    }
    ::blake3_hasher_finalize(&hasher_.value(),
                             reinterpret_cast<uint8_t*>(digest), DigestBytes);
    hasher_.reset();
    return true;
  }

  std::string hexdigest() override { return make_hexdigest(*this); }

  size_t digest_size() override { return DigestBytes; }

 private:
  std::optional<blake3_hasher> hasher_;
};

using checksum_xxh3_64 = checksum_xxh3<xxh3_64_policy>;
using checksum_xxh3_128 = checksum_xxh3<xxh3_128_policy>;
using checksum_blake3_256 = checksum_blake3<256 / 8>;

template <typename T>
bool verify_impl(T&& alg, void const* data, size_t size, void const* digest,
                 size_t digest_size) {
  std::array<char, EVP_MAX_MD_SIZE> tmp;
  checksum cs(std::forward<T>(alg));
  DWARFS_CHECK(digest_size == cs.digest_size(), "digest size mismatch");
  cs.update(data, size);
  return cs.finalize(tmp.data()) &&
         ::memcmp(digest, tmp.data(), digest_size) == 0;
}

} // namespace

bool checksum::is_available(std::string const& algo) {
  // TODO: C++23: use std::ranges::contains
  return std::ranges::find(supported_algorithms, algo) !=
             supported_algorithms.end() ||
         checksum_evp::is_available(algo);
}

std::vector<std::string> checksum::available_algorithms() {
  auto available_evp = checksum_evp::available_algorithms();
  std::vector<std::string> available;
  available.insert(available.end(), supported_algorithms.begin(),
                   supported_algorithms.end());
  available.insert(available.end(), available_evp.begin(), available_evp.end());
  std::ranges::sort(available);
  return available;
}

bool checksum::verify(xxh3_64_tag, void const* data, size_t size,
                      void const* digest, size_t digest_size) {
  return verify_impl(xxh3_64, data, size, digest, digest_size);
}

bool checksum::verify(sha2_512_256_tag, void const* data, size_t size,
                      void const* digest, size_t digest_size) {
  return verify_impl(sha2_512_256, data, size, digest, digest_size);
}

bool checksum::verify(blake3_256_tag, void const* data, size_t size,
                      void const* digest, size_t digest_size) {
  return verify_impl(blake3_256, data, size, digest, digest_size);
}

bool checksum::verify(std::string const& alg, void const* data, size_t size,
                      void const* digest, size_t digest_size) {
  return verify_impl(alg, data, size, digest, digest_size);
}

checksum::checksum(xxh3_64_tag)
    : impl_(std::make_unique<checksum_xxh3_64>()) {}

checksum::checksum(sha2_512_256_tag)
    : impl_(std::make_unique<checksum_evp>(::EVP_sha512_256())) {}

checksum::checksum(blake3_256_tag)
    : impl_(std::make_unique<checksum_blake3_256>()) {}

checksum::checksum(std::string const& alg) {
  if (alg == "xxh3-64") {
    impl_ = std::make_unique<checksum_xxh3_64>();
  } else if (alg == "xxh3-128") {
    impl_ = std::make_unique<checksum_xxh3_128>();
  } else if (alg == "blake3-256") {
    impl_ = std::make_unique<checksum_blake3_256>();
  } else if (auto md = ::EVP_get_digestbyname(alg.c_str())) {
    impl_ = std::make_unique<checksum_evp>(md);
  } else {
    DWARFS_CHECK(false, "unknown algorithm");
  }
}

} // namespace dwarfs

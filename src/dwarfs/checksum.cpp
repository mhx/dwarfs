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

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <ostream>
#include <unordered_set>

#include <openssl/evp.h>

#include <xxhash.h>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/error.h"

namespace dwarfs {

namespace {

std::unordered_set<std::string> supported_algorithms{
    "xxh3-64",
    "xxh3-128",
};

class checksum_evp : public checksum::impl {
 public:
  explicit checksum_evp(::EVP_MD const* evp)
      : context_(::EVP_MD_CTX_new())
      , dig_size_(::EVP_MD_size(evp)) {
    DWARFS_CHECK(::EVP_DigestInit(context_, evp), "EVP_DigestInit() failed");
  }

  ~checksum_evp() override { ::EVP_MD_CTX_destroy(context_); }

  void update(void const* data, size_t size) override {
    DWARFS_CHECK(::EVP_DigestUpdate(context_, data, size),
                 "EVP_DigestUpdate() failed");
  }

  bool finalize(void* digest) override {
    unsigned int dig_size = 0;
    bool rv = ::EVP_DigestFinal_ex(
        context_, reinterpret_cast<unsigned char*>(digest), &dig_size);

    if (rv) {
      DWARFS_CHECK(
          dig_size_ == dig_size,
          fmt::format("digest size mismatch: {0} != {1}", dig_size_, dig_size));
    }

    return rv;
  }

  static std::vector<std::string> available_algorithms() {
    std::vector<std::string> available;
    ::EVP_MD_do_all(
        [](const ::EVP_MD*, const char* from, const char* to, void* vec) {
          if (!to) {
            reinterpret_cast<std::vector<std::string>*>(vec)->emplace_back(
                from);
          }
        },
        &available);
    available.erase(std::remove_if(available.begin(), available.end(),
                                   std::not_fn(is_available)),
                    available.end());
    return available;
  }

  static bool is_available(std::string const& algo) {
    if (auto md = ::EVP_get_digestbyname(algo.c_str())) {
      ::EVP_MD_CTX* cx = ::EVP_MD_CTX_new();
      bool success = ::EVP_DigestInit(cx, md);
      ::EVP_MD_CTX_destroy(cx);
      return success;
    }
    return false;
  }

  size_t digest_size() override { return dig_size_; }

 private:
  ::EVP_MD_CTX* context_;
  size_t const dig_size_;
};

class checksum_xxh3_64 : public checksum::impl {
 public:
  checksum_xxh3_64()
      : state_(XXH3_createState()) {
    DWARFS_CHECK(XXH3_64bits_reset(state_) == XXH_OK,
                 "XXH3_64bits_reset() failed");
  }

  ~checksum_xxh3_64() override { XXH3_freeState(state_); }

  void update(void const* data, size_t size) override {
    auto err = XXH3_64bits_update(state_, data, size);
    DWARFS_CHECK(err == XXH_OK, fmt::format("XXH3_64bits_update() failed: {}",
                                            static_cast<int>(err)));
  }

  bool finalize(void* digest) override {
    auto hash = XXH3_64bits_digest(state_);
    ::memcpy(digest, &hash, sizeof(hash));
    return true;
  }

  size_t digest_size() override {
    return sizeof(decltype(std::function{XXH3_64bits_digest})::result_type);
  }

 private:
  XXH3_state_t* state_;
};

class checksum_xxh3_128 : public checksum::impl {
 public:
  checksum_xxh3_128()
      : state_(XXH3_createState()) {
    DWARFS_CHECK(XXH3_128bits_reset(state_) == XXH_OK,
                 "XXH3_128bits_reset() failed");
  }

  ~checksum_xxh3_128() override { XXH3_freeState(state_); }

  void update(void const* data, size_t size) override {
    auto err = XXH3_128bits_update(state_, data, size);
    DWARFS_CHECK(err == XXH_OK, fmt::format("XXH3_128bits_update() failed: {}",
                                            static_cast<int>(err)));
  }

  bool finalize(void* digest) override {
    auto hash = XXH3_128bits_digest(state_);
    ::memcpy(digest, &hash, sizeof(hash));
    return true;
  }

  size_t digest_size() override {
    return sizeof(decltype(std::function{XXH3_128bits_digest})::result_type);
  }

 private:
  XXH3_state_t* state_;
};

template <typename T>
bool verify_impl(T&& alg, void const* data, size_t size, const void* digest,
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
  return supported_algorithms.count(algo) or checksum_evp::is_available(algo);
}

std::vector<std::string> checksum::available_algorithms() {
  auto available_evp = checksum_evp::available_algorithms();
  std::vector<std::string> available;
  available.insert(available.end(), supported_algorithms.begin(),
                   supported_algorithms.end());
  available.insert(available.end(), available_evp.begin(), available_evp.end());
  std::sort(available.begin(), available.end());
  return available;
}

bool checksum::verify(algorithm alg, void const* data, size_t size,
                      const void* digest, size_t digest_size) {
  return verify_impl(alg, data, size, digest, digest_size);
}

bool checksum::verify(std::string const& alg, void const* data, size_t size,
                      const void* digest, size_t digest_size) {
  return verify_impl(alg, data, size, digest, digest_size);
}

checksum::checksum(algorithm alg) {
  switch (alg) {
  case algorithm::SHA2_512_256:
    impl_ = std::make_unique<checksum_evp>(::EVP_sha512_256());
    break;
  case algorithm::XXH3_64:
    impl_ = std::make_unique<checksum_xxh3_64>();
    break;
  case algorithm::XXH3_128:
    impl_ = std::make_unique<checksum_xxh3_128>();
    break;
  default:
    DWARFS_CHECK(false, "unknown algorithm");
    break;
  }
}

checksum::checksum(std::string const& alg) {
  if (alg == "xxh3-64") {
    impl_ = std::make_unique<checksum_xxh3_64>();
  } else if (alg == "xxh3-128") {
    impl_ = std::make_unique<checksum_xxh3_128>();
  } else if (auto md = ::EVP_get_digestbyname(alg.c_str())) {
    impl_ = std::make_unique<checksum_evp>(md);
  } else {
    DWARFS_CHECK(false, "unknown algorithm");
  }
}

std::ostream& operator<<(std::ostream& os, checksum::algorithm alg) {
  switch (alg) {
  case checksum::algorithm::SHA2_512_256:
    os << "SHA2_512_256";
    break;
  case checksum::algorithm::XXH3_64:
    os << "XXH3_64";
    break;
  case checksum::algorithm::XXH3_128:
    os << "XXH3_128";
    break;
  default:
    os << "<unknown>";
    break;
  }
  return os;
}

} // namespace dwarfs

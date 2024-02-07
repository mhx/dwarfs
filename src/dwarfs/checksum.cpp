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
#include <cassert>
#include <cstring>
#include <functional>
#include <ostream>
#include <unordered_set>

#include <openssl/evp.h>

#include <xxhash.h>

#include <boost/algorithm/hex.hpp>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/error.h"

namespace dwarfs {

namespace {

std::unordered_set<std::string> supported_algorithms{
    "xxh3-64",
    "xxh3-128",
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
  static constexpr auto reset = XXH3_64bits_reset;
  static constexpr auto update = XXH3_64bits_update;
  static constexpr auto digest = XXH3_64bits_digest;
};

struct xxh3_128_policy {
  using result_type = XXH128_hash_t;
  static constexpr auto reset = XXH3_128bits_reset;
  static constexpr auto update = XXH3_128bits_update;
  static constexpr auto digest = XXH3_128bits_digest;
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
    auto hash = Policy::digest(state_.get());
    state_.reset();
    ::memcpy(digest, &hash, sizeof(hash));
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

using checksum_xxh3_64 = checksum_xxh3<xxh3_64_policy>;
using checksum_xxh3_128 = checksum_xxh3<xxh3_128_policy>;

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

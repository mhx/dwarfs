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

#include <cstring>

#include <openssl/evp.h>

#include <xxhash.h>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/error.h"

namespace dwarfs {

namespace {

bool compute_evp(const EVP_MD* algorithm, void const* data, size_t size,
                 void* digest, unsigned int* digest_size) {
  return EVP_Digest(data, size, reinterpret_cast<unsigned char*>(digest),
                    digest_size, algorithm, nullptr);
}

bool compute_xxh3_64(void const* data, size_t size, void* digest) {
  auto hash = XXH3_64bits(data, size);
  static_assert(checksum::digest_size(checksum::algorithm::XXH3_64) ==
                sizeof(hash));
  ::memcpy(digest, &hash, sizeof(hash));
  return true;
}

class checksum_evp : public checksum::impl {
 public:
  checksum_evp(EVP_MD const* evp, checksum::algorithm alg)
      : context_(EVP_MD_CTX_new())
      , dig_size_(checksum::digest_size(alg)) {
    EVP_DigestInit_ex(context_, evp, nullptr);
  }

  ~checksum_evp() override { EVP_MD_CTX_destroy(context_); }

  void update(void const* data, size_t size) override {
    DWARFS_CHECK(EVP_DigestUpdate(context_, data, size),
                 "EVP_DigestUpdate() failed");
  }

  bool finalize(void* digest) override {
    unsigned int dig_size = 0;
    bool rv = EVP_DigestFinal_ex(
        context_, reinterpret_cast<unsigned char*>(digest), &dig_size);

    if (rv) {
      DWARFS_CHECK(
          dig_size_ == dig_size,
          fmt::format("digest size mismatch: {0} != {1}", dig_size_, dig_size));
    }

    return rv;
  }

 private:
  EVP_MD_CTX* context_;
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

 private:
  XXH3_state_t* state_;
};

} // namespace

bool checksum::compute(algorithm alg, void const* data, size_t size,
                       void* digest) {
  bool rv = false;
  unsigned int dig_size = 0;

  switch (alg) {
  case algorithm::SHA1:
    rv = compute_evp(EVP_sha1(), data, size, digest, &dig_size);
    break;
  case algorithm::SHA2_512_256:
    rv = compute_evp(EVP_sha512_256(), data, size, digest, &dig_size);
    break;
  case algorithm::XXH3_64:
    rv = compute_xxh3_64(data, size, digest);
    break;
  }

  if (rv && dig_size > 0) {
    DWARFS_CHECK(digest_size(alg) == dig_size,
                 fmt::format("digest size mismatch: {0} != {1} [{2}]",
                             digest_size(alg), dig_size,
                             static_cast<int>(alg)));
  }

  return rv;
}

bool checksum::verify(algorithm alg, void const* data, size_t size,
                      const void* digest) {
  char tmp[EVP_MAX_MD_SIZE];
  return compute(alg, data, size, tmp) &&
         ::memcmp(digest, tmp, digest_size(alg)) == 0;
}

checksum::checksum(algorithm alg)
    : alg_(alg) {
  switch (alg) {
  case algorithm::SHA1:
    impl_ = std::make_unique<checksum_evp>(EVP_sha1(), alg);
    break;
  case algorithm::SHA2_512_256:
    impl_ = std::make_unique<checksum_evp>(EVP_sha512_256(), alg);
    break;
  case algorithm::XXH3_64:
    impl_ = std::make_unique<checksum_xxh3_64>();
    break;
  }
  DWARFS_CHECK(false, "unknown algorithm");
}

bool checksum::verify(void const* digest) const {
  char tmp[EVP_MAX_MD_SIZE];
  return impl_->finalize(tmp) && ::memcmp(digest, tmp, digest_size(alg_)) == 0;
}

} // namespace dwarfs

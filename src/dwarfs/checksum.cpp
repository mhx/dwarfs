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
                 void* result, unsigned int* digest_size) {
  return EVP_Digest(data, size, reinterpret_cast<unsigned char*>(result),
                    digest_size, algorithm, nullptr);
}

bool compute_xxh3_64(void const* data, size_t size, void* result) {
  auto checksum = XXH3_64bits(data, size);
  static_assert(checksum_size(checksum_algorithm::XXH3_64) == sizeof(checksum));
  ::memcpy(result, &checksum, sizeof(checksum));
  return true;
}

} // namespace

bool compute_checksum(checksum_algorithm alg, void const* data, size_t size,
                      void* result) {
  bool rv = false;
  unsigned int digest_size = 0;

  switch (alg) {
  case checksum_algorithm::SHA1:
    rv = compute_evp(EVP_sha1(), data, size, result, &digest_size);
    break;
  case checksum_algorithm::SHA2_512_256:
    rv = compute_evp(EVP_sha512_256(), data, size, result, &digest_size);
    break;
  case checksum_algorithm::XXH3_64:
    rv = compute_xxh3_64(data, size, result);
    break;
  }

  if (rv && digest_size > 0) {
    DWARFS_CHECK(checksum_size(alg) == digest_size,
                 fmt::format("digest size mismatch: {0} != {1} [{2}]",
                             checksum_size(alg), digest_size,
                             static_cast<int>(alg)));
  }

  return rv;
}

bool verify_checksum(checksum_algorithm alg, void const* data, size_t size,
                     const void* checksum) {
  char tmp[EVP_MAX_MD_SIZE];
  return compute_checksum(alg, data, size, tmp) &&
         ::memcmp(checksum, tmp, checksum_size(alg)) == 0;
}

} // namespace dwarfs

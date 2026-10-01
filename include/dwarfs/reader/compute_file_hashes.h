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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

#include <dwarfs/reader/metadata_types.h>

namespace dwarfs {

class logger;
class os_access;

namespace reader {

class filesystem_v2;

enum class compute_hash_result_key {
  unix_path,
  entry_view,
  content_id,
};

enum class compute_hash_digest_format {
  raw,
  hex,
};

struct compute_hash_config {
  std::string hash_algorithm{"blake3-256"};
  std::size_t max_queued_bytes{64 * 1024 * 1024}; // 64 MiB
  std::size_t num_worker_threads{4};
  compute_hash_result_key result_key{compute_hash_result_key::unix_path};
  compute_hash_digest_format digest_format{compute_hash_digest_format::hex};
};

struct compute_hash_result {
  using key_type =
      std::variant<std::uint32_t, std::string_view, dir_entry_view>;
  key_type key;
  std::string_view digest;
};

using compute_hash_callback = std::function<void(compute_hash_result const&)>;

void compute_file_hashes(logger& lgr, os_access const& os,
                         filesystem_v2 const& fs,
                         compute_hash_config const& config,
                         compute_hash_callback callback);

} // namespace reader
} // namespace dwarfs

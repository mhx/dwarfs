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
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dwarfs/checksum.h>
#include <dwarfs/counting_semaphore.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/compute_file_hashes.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_v2.h>

#include <dwarfs/internal/worker_group.h>

namespace dwarfs::reader {

void compute_file_hashes(logger& lgr, os_access const& os,
                         filesystem_v2 const& fs,
                         compute_hash_config const& config,
                         compute_hash_callback callback) {
  auto const& algo = config.hash_algorithm;
  auto const max_queued_bytes = config.max_queued_bytes;
  bool const unique_only =
      config.result_key == compute_hash_result_key::content_id;

  using queued_type = std::variant<std::string, dir_entry_view>;

  struct cache_entry {
    explicit cache_entry(reader::duplication_info const& dup_info)
        : remaining{dup_info.duplication_count} {}

    std::size_t remaining;
    std::string digest;
    std::vector<queued_type> queued;
  };

  std::mutex mx;
  std::unordered_map<uint32_t, cache_entry> hash_cache;
  counting_semaphore sem;
  sem.post(static_cast<int64_t>(max_queued_bytes));

  dwarfs::internal::worker_group wg{
      lgr, os, "checksum", {.num_workers = config.num_worker_threads}};

  size_t const max_queued_per_worker =
      max_queued_bytes / config.num_worker_threads;

  auto hash_ranges = [&algo,
                      format = config.digest_format](auto const& ranges) {
    thread_local checksum cs(algo);

    cs.reset();

    for (auto const& r : ranges) {
      cs.update(r.data(), r.size());
    }

    return format == compute_hash_digest_format::hex ? cs.hexdigest()
                                                     : cs.digest();
  };

  auto make_key =
      [&config](
          queued_type const& q,
          duplication_info const& dup_info) -> compute_hash_result::key_type {
    switch (config.result_key) {
      using enum compute_hash_result_key;
    case content_id:
      return dup_info.unique_content_id;
    case unix_path:
      return std::get<std::string>(q);
    case entry_view:
      return std::get<dir_entry_view>(q);
    default:
      throw std::logic_error("unexpected result key type");
    }
  };

  auto make_queued = [&config](dir_entry_view const& de) -> queued_type {
    switch (config.result_key) {
      using enum compute_hash_result_key;
    case unix_path:
      return de.unix_path();
    case entry_view:
      return de;
    default:
      throw std::logic_error("unexpected result key type");
    }
  };

  auto run_callback = [&](std::string_view digest, queued_type const& de,
                          duplication_info const& dup_info) {
    compute_hash_result result{
        .key = make_key(de, dup_info),
        .digest = digest,
    };

    callback(result);
  };

  for (auto const& de : fs.entries_in_data_order()) {
    auto iv = de.inode();

    if (iv.is_regular_file()) {
      auto const dup_info = fs.get_duplication_info(iv);

      if (dup_info.duplication_count > 1) {
        std::lock_guard lock(mx);

        auto it = hash_cache.find(dup_info.unique_content_id);

        if (it != hash_cache.end()) {
          bool const has_digest = !it->second.digest.empty();

          if (has_digest) {
            run_callback(it->second.digest, make_queued(de), dup_info);
          } else if (!unique_only) {
            it->second.queued.push_back(make_queued(de));
          }

          if (has_digest || unique_only) {
            assert(it->second.queued.empty());
            if (--it->second.remaining == 0) {
              hash_cache.erase(it);
            }
          }

          continue;
        }

        auto const r [[maybe_unused]] = hash_cache.emplace(
            dup_info.unique_content_id, cache_entry{dup_info});

        assert(r.second);
      }

      reader::detail::file_reader fr(fs, iv);

      wg.add_job(
          [&lgr, &hash_ranges, &hash_cache, &mx, &run_callback, &make_queued,
           de, dup_info, unique_only,
           ranges = fr.read_sequential(sem, max_queued_per_worker)] mutable {
            try {
              std::string digest = hash_ranges(ranges);
              auto const queued = make_queued(de);

              {
                std::lock_guard lock(mx);
                run_callback(digest, queued, dup_info);

                if (!unique_only && dup_info.duplication_count > 1) {
                  auto it = hash_cache.find(dup_info.unique_content_id);

                  assert(it != hash_cache.end());

                  for (auto const& q : it->second.queued) {
                    run_callback(digest, q, dup_info);
                  }

                  assert(std::cmp_greater(it->second.remaining,
                                          it->second.queued.size()));

                  it->second.remaining -= it->second.queued.size();
                  it->second.queued.clear();

                  if (--it->second.remaining == 0) {
                    hash_cache.erase(it);
                  } else {
                    it->second.digest = std::move(digest);
                  }
                }
              }
            } catch (std::exception const& e) {
              LOG_PROXY(debug_logger_policy, lgr);
              LOG_ERROR << "error processing inode for " << de.unix_path()
                        << ": " << e.what();
            }
          });
    }
  }

  wg.wait();

#ifdef NDEBUG
  {
    std::lock_guard lock(mx);
    assert(hash_cache.empty());
  }
#endif
}

} // namespace dwarfs::reader

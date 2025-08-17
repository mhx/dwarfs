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
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace dwarfs::test {

struct lz_params {
  // Probability of choosing a "copy from the past" step vs. emitting a literal
  double copy_prob = 0.70;

  // Max distance for backreferences (typical LZ77 windows are 32–64 KiB)
  std::size_t window = 1u << 15; // 32 KiB

  // Copy lengths ~ truncated geometric around this mean (controls
  // repetitiveness)
  std::size_t min_match = 4;
  std::size_t max_match = 128;
  double target_match_mean = 20.0; // average copy length

  // Geometric distribution for distance (smaller distances more likely)
  double distance_mean = 128.0;

  // Chance each character in a copy mutates into a random literal (adds
  // “noise”)
  double mutation_rate = 0.005;

  // If true, literals look like English-ish text; if false, literals are 0–255
  // bytes
  bool text_mode = true;

  // RNG seed for reproducibility
  std::uint64_t seed = 0x1234'5678'9abc'def0ULL;
};

class lz_synthetic_generator {
 public:
  explicit lz_synthetic_generator(lz_params p = {})
      : p_{p}
      , rng_{p.seed} {
    if (p_.text_mode) {
      init_text_alphabet();
    } else {
      init_binary_alphabet();
    }

    // geometric_distribution parameterization: mean of failures = (1-p)/p
    // We want E[min_match + failures] ≈ target_match_mean  =>  E[failures] ≈
    // target - min
    double mean_fail =
        std::max(1.0, p_.target_match_mean - static_cast<double>(p_.min_match));
    double p_len = 1.0 / (mean_fail + 1.0);
    geo_len_ = std::geometric_distribution<int>(p_len);

    double mean_dist_fail = std::max(1.0, p_.distance_mean);
    double p_dist = 1.0 / (mean_dist_fail + 1.0);
    geo_dist_ = std::geometric_distribution<int>(p_dist);

    bern_copy_ = std::bernoulli_distribution(p_.copy_prob);
    bern_mut_ = std::bernoulli_distribution(p_.mutation_rate);
  }

  std::string generate(std::size_t n_bytes) {
    std::string out;
    out.reserve(n_bytes);

    while (out.size() < n_bytes) {
      bool const can_copy = out.size() >= p_.min_match;
      if (can_copy && bern_copy_(rng_)) {
        emit_copy(out, n_bytes);
      } else {
        out.push_back(sample_literal());
      }
    }
    return out;
  }

 private:
  void init_text_alphabet() {
    // Rough English-ish frequencies via "etaoin shrdlu..." ranking.
    // Higher rank => higher weight. We include space/newline/punct/digits.
    static constexpr std::string_view freq_rank =
        " etaoinshrdlucmfwypvbgkqjxz"; // space first (most frequent)
    // Map ranks to weights (largest for rank 0).
    std::array<int, 256> weights{};
    for (int i = 0; i < 256; ++i) {
      weights[i] = 1;
    }

    auto apply_rank = [&](char c, size_t rank_base) {
      int r = std::max(1, static_cast<int>(freq_rank.size()) -
                              static_cast<int>(rank_base));
      weights[static_cast<unsigned char>(c)] += r;
    };

    for (size_t i = 0; i < freq_rank.size(); ++i) {
      char c = freq_rank[i];
      apply_rank(c, i);
      if (c >= 'a' && c <= 'z') {
        apply_rank(char(c - 'a' + 'A'), i + 6); // uppercase similar but rarer
      }
    }

    // Common punctuation and digits
    std::string const punct = ".,;:-()[]{}!?\"'";
    for (char c : punct) {
      weights[static_cast<unsigned char>(c)] += 8;
    }
    for (char c = '0'; c <= '9'; ++c) {
      weights[static_cast<unsigned char>(c)] += 4;
    }

    // Newlines and tabs, for “document” feel
    weights['\n'] += 6;
    weights['\t'] += 2;

    // Build alphabet and weight vector for std::discrete_distribution
    for (int i = 0; i < 256; ++i) {
      if (weights[i] > 0) {
        text_alphabet_.push_back(static_cast<unsigned char>(i));
        text_weights_.push_back(weights[i]);
      }
    }
    text_dist_ = std::discrete_distribution<int>(text_weights_.begin(),
                                                 text_weights_.end());
  }

  void init_binary_alphabet() {
    binary_dist_ = std::uniform_int_distribution<int>(0, 255);
  }

  char sample_literal() {
    if (p_.text_mode) {
      int idx = text_dist_(rng_);
      return static_cast<char>(text_alphabet_[static_cast<std::size_t>(idx)]);
    }
    return static_cast<char>(binary_dist_(rng_));
  }

  void emit_copy(std::string& out, std::size_t n_bytes) {
    // Distance: 1 + geometric, truncated to current size and window
    std::size_t max_dist = std::min<std::size_t>(p_.window, out.size());
    if (max_dist == 0) {
      out.push_back(sample_literal());
      return;
    }

    std::size_t dist = 1u + static_cast<std::size_t>(geo_dist_(rng_));
    if (dist > max_dist)
      dist = 1u + (dist % max_dist); // ensure in-range

    // Length: min_match + geometric, truncated by end and max_match
    std::size_t max_len =
        std::min<std::size_t>(p_.max_match, n_bytes - out.size());
    if (max_len < p_.min_match) {
      out.push_back(sample_literal());
      return;
    }

    std::size_t len = p_.min_match + static_cast<std::size_t>(geo_len_(rng_));
    if (len > max_len)
      len = max_len;

    std::size_t start = out.size() - dist;
    for (std::size_t i = 0; i < len && out.size() < n_bytes; ++i) {
      unsigned char c = static_cast<unsigned char>(out[start + i]);
      if (bern_mut_(rng_)) {
        c = static_cast<unsigned char>(sample_literal());
      }
      out.push_back(static_cast<char>(c));
    }
  }

  lz_params p_;
  std::mt19937_64 rng_;

  std::vector<unsigned char> text_alphabet_;
  std::vector<int> text_weights_;
  std::discrete_distribution<int> text_dist_;

  std::uniform_int_distribution<int> binary_dist_{0, 255};

  std::bernoulli_distribution bern_copy_;
  std::bernoulli_distribution bern_mut_;
  std::geometric_distribution<int> geo_len_;
  std::geometric_distribution<int> geo_dist_;
};

} // namespace dwarfs::test

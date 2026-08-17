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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Tests for fsst_create_indexed(), which builds a symbol table from input that
// is not stored as contiguous pointer and length arrays.
//
// There is exactly one property to check: for the same input, it must produce
// the same encoder as fsst_create(). Every assertion here is a comparison
// against fsst_create(), so nothing depends on which symbol table the library
// happens to pick. That keeps these tests independent of both the library's
// internal PRNG and the endianness of the target.
//
// The corpora cover the two paths in makeSample(): inputs below
// FSST_SAMPLETARGET (16 KiB in total), where the sample aliases the input, and
// inputs above it, where the sample is copied into a scratch buffer.

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <numeric>
#include <ostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fsst.h>

namespace {

// A deliberately non-contiguous string container.
class chunked_strings {
 public:
  static constexpr std::size_t chunk_elements{7};

  void push_back(std::string s) {
    if (chunks_.empty() || chunks_.back().size() == chunk_elements) {
      chunks_.emplace_back();
      chunks_.back().reserve(chunk_elements);
    }
    chunks_.back().push_back(std::move(s));
    ++size_;
  }

  std::string const& operator[](std::size_t i) const {
    return chunks_[i / chunk_elements][i % chunk_elements];
  }

  std::size_t size() const { return size_; }

  static std::size_t length_cb(void* ctx, std::size_t i) {
    return static_cast<chunked_strings const*>(ctx)->operator[](i).size();
  }

  static unsigned char const* data_cb(void* ctx, std::size_t i) {
    return reinterpret_cast<unsigned char const*>(
        static_cast<chunked_strings const*>(ctx)->operator[](i).data());
  }

 private:
  std::deque<std::vector<std::string>> chunks_;
  std::size_t size_{0};
};

chunked_strings to_chunked(std::vector<std::string> const& v) {
  chunked_strings result;
  for (auto const& s : v) {
    result.push_back(s);
  }
  return result;
}

using encoder_ptr = std::unique_ptr<fsst_encoder_t, decltype(&::fsst_destroy)>;

encoder_ptr make_encoder(fsst_encoder_t* enc) {
  return encoder_ptr{enc, &::fsst_destroy};
}

// The contiguous pointer and length arrays that fsst_create() requires.
struct array_view {
  std::vector<std::size_t> len;
  std::vector<unsigned char const*> ptr;

  explicit array_view(std::vector<std::string> const& v) {
    len.reserve(v.size());
    ptr.reserve(v.size());
    for (auto const& s : v) {
      len.emplace_back(s.size());
      ptr.emplace_back(reinterpret_cast<unsigned char const*>(s.data()));
    }
  }
};

encoder_ptr
create_from_arrays(std::vector<std::string> const& v, int zero_terminated) {
  array_view av{v};
  return make_encoder(
      ::fsst_create(v.size(), av.len.data(), av.ptr.data(), zero_terminated));
}

fsst_input_t
make_input(chunked_strings const& src, std::size_t const* lengths_hint) {
  fsst_input_t in;
  // the callbacks only read from the source, but the C API takes void*
  in.context = const_cast<chunked_strings*>(&src);
  in.length = &chunked_strings::length_cb;
  in.data = &chunked_strings::data_cb;
  in.lengths = lengths_hint;
  return in;
}

std::string export_symbol_table(fsst_encoder_t* enc) {
  std::string buf;
  buf.resize(FSST_MAXHEADER);
  auto const n =
      ::fsst_export(enc, reinterpret_cast<unsigned char*>(buf.data()));
  buf.resize(n);
  return buf;
}

// Compress the whole batch and return the concatenated compressed bytes.
//
// The output buffer is sized from the bound in compressBulk(): every 511-byte
// piece of input needs at most 2 * piece + 7 bytes of output space.
std::string
compress_all(fsst_encoder_t* enc, std::vector<std::string> const& v) {
  array_view av{v};

  auto const total = std::accumulate(
      v.begin(), v.end(), std::size_t{0},
      [](std::size_t n, auto const& s) { return n + s.size(); });

  std::size_t pieces = 0;
  for (auto const& s : v) {
    pieces += 1 + s.size() / 511;
  }

  std::string out;
  out.resize(2 * total + 7 * pieces + 64);

  std::vector<std::size_t> out_len(v.size());

  auto const num = ::fsst_compress(
      enc, v.size(), av.len.data(), av.ptr.data(), out.size(),
      reinterpret_cast<unsigned char*>(out.data()), out_len.data(), nullptr);

  EXPECT_EQ(v.size(), num) << "output buffer was too small";

  out.resize(std::accumulate(out_len.begin(), out_len.end(), std::size_t{0}));

  return out;
}

enum class corpus_kind {
  pathlike,
  with_empty_strings,
  long_strings,
};

std::vector<std::string>
make_corpus(corpus_kind kind, std::size_t count, unsigned seed) {
  static constexpr std::array<std::string_view, 15> words{
      "index", "html",   "wiki", "Special:", "Category",
      "de",    "en",     "fr",   "2019",     "article",
      "_-_",   "%C3%A4", "png",  "thumb",    "revision"};

  std::mt19937 rng{seed};
  std::vector<std::string> result;
  result.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    std::string s;

    switch (kind) {
    case corpus_kind::pathlike:
      for (int k = 0, n = 1 + static_cast<int>(rng() % 4); k < n; ++k) {
        s += words[rng() % words.size()];
        s += '/';
      }
      break;

    case corpus_kind::with_empty_strings:
      // makeSample() has to skip empty lines when picking a sample
      if (rng() % 3 != 0) {
        s = words[rng() % words.size()];
      }
      break;

    case corpus_kind::long_strings:
      // long enough to cross the 512-byte FSST_SAMPLELINE boundary, so that
      // makeSample() samples a chunk from the middle of a string
      for (int k = 0, n = 60 + static_cast<int>(rng() % 80); k < n; ++k) {
        s += words[rng() % words.size()];
      }
      break;
    }

    result.emplace_back(std::move(s));
  }

  return result;
}

struct test_case {
  std::string_view name;
  corpus_kind kind;
  std::size_t count;
  int zero_terminated;
};

std::ostream& operator<<(std::ostream& os, test_case const& c) {
  return os << c.name;
}

std::vector<test_case> const& test_cases() {
  static std::vector<test_case> const cases{
      // degenerate input
      {"single_string", corpus_kind::pathlike, 1, 0},
      // below FSST_SAMPLETARGET: the sample aliases the input
      {"below_sample_target", corpus_kind::pathlike, 100, 0},
      // above FSST_SAMPLETARGET: the sample is copied out
      {"above_sample_target", corpus_kind::pathlike, 20000, 0},
      // above FSST_SAMPLETARGET, with empty lines to skip over
      {"with_empty_strings", corpus_kind::with_empty_strings, 40000, 0},
      // strings longer than FSST_SAMPLELINE
      {"long_strings", corpus_kind::long_strings, 500, 0},
      // zeroTerminated is passed straight through
      {"zero_terminated", corpus_kind::pathlike, 20000, 1},
  };
  return cases;
}

std::vector<std::string> corpus_for(test_case const& c) {
  return make_corpus(c.kind, c.count, 1234u + static_cast<unsigned>(c.kind));
}

std::vector<std::size_t> lengths_of(std::vector<std::string> const& v) {
  std::vector<std::size_t> result;
  result.reserve(v.size());
  for (auto const& s : v) {
    result.emplace_back(s.size());
  }
  return result;
}

} // namespace

class fsst_library_test : public ::testing::TestWithParam<test_case> {};

// The property fsst_create_indexed() has to satisfy: a symbol table built from
// a non-contiguous source is the same as one built from arrays, and encodes
// identically.
TEST_P(fsst_library_test, indexed_matches_array_based) {
  auto const& c = GetParam();
  auto const strings = corpus_for(c);

  auto const from_arrays = create_from_arrays(strings, c.zero_terminated);

  auto const src = to_chunked(strings);
  auto const in = make_input(src, nullptr);
  auto const from_indexed = make_encoder(
      ::fsst_create_indexed(strings.size(), &in, c.zero_terminated));

  EXPECT_EQ(export_symbol_table(from_arrays.get()),
            export_symbol_table(from_indexed.get()));
  EXPECT_EQ(compress_all(from_arrays.get(), strings),
            compress_all(from_indexed.get(), strings));
}

// The optional `lengths` hint only lets the library skip materializing a length
// array; it must not be able to change the result. Passing it also exercises
// the other branch of the cleanup in fsst_create_indexed().
TEST_P(fsst_library_test, contiguous_lengths_hint_is_transparent) {
  auto const& c = GetParam();
  auto const strings = corpus_for(c);
  auto const src = to_chunked(strings);
  auto const lengths = lengths_of(strings);

  auto const without = make_input(src, nullptr);
  auto const with = make_input(src, lengths.data());

  auto const a = make_encoder(
      ::fsst_create_indexed(strings.size(), &without, c.zero_terminated));
  auto const b = make_encoder(
      ::fsst_create_indexed(strings.size(), &with, c.zero_terminated));

  EXPECT_EQ(export_symbol_table(a.get()), export_symbol_table(b.get()));
}

// The encoder must not retain anything belonging to the input. This is what
// lets a caller build the dictionary first and then release the source data
// incrementally while compressing.
TEST_P(fsst_library_test, encoder_remains_valid_after_input_is_destroyed) {
  auto const& c = GetParam();
  auto const strings = corpus_for(c);

  auto enc = encoder_ptr{nullptr, &::fsst_destroy};

  {
    auto const src = to_chunked(strings);
    auto const lengths = lengths_of(strings);
    auto const in = make_input(src, lengths.data());
    enc = make_encoder(
        ::fsst_create_indexed(strings.size(), &in, c.zero_terminated));
  }

  auto const reference = create_from_arrays(strings, c.zero_terminated);

  EXPECT_EQ(export_symbol_table(reference.get()),
            export_symbol_table(enc.get()));
  EXPECT_EQ(compress_all(reference.get(), strings),
            compress_all(enc.get(), strings));
}

INSTANTIATE_TEST_SUITE_P(dwarfs, fsst_library_test,
                         ::testing::ValuesIn(test_cases()),
                         [](::testing::TestParamInfo<test_case> const& info) {
                           return std::string{info.param.name};
                         });

// Randomized sweep over sizes, to cover input sizes near FSST_SAMPLETARGET
// that the fixed corpora above miss.
TEST(fsst_library_random_test, indexed_matches_array_based) {
#ifdef DWARFS_TEST_CROSS_COMPILE
  static constexpr int num_random_tests = 50;
#else
  static constexpr int num_random_tests = 300;
#endif

  std::mt19937 rng{42};

  for (int i = 0; i < num_random_tests; ++i) {
    auto const count = 1 + static_cast<std::size_t>(rng() % 3000);
    auto const kind = static_cast<corpus_kind>(rng() % 3);
    auto const zero_terminated = static_cast<int>(rng() % 2);
    auto const strings = make_corpus(kind, count, static_cast<unsigned>(rng()));

    auto const from_arrays = create_from_arrays(strings, zero_terminated);

    auto const src = to_chunked(strings);
    auto const in = make_input(src, nullptr);
    auto const from_indexed = make_encoder(
        ::fsst_create_indexed(strings.size(), &in, zero_terminated));

    ASSERT_EQ(export_symbol_table(from_arrays.get()),
              export_symbol_table(from_indexed.get()))
        << "count=" << count << ", kind=" << static_cast<int>(kind)
        << ", zero_terminated=" << zero_terminated;
  }
}

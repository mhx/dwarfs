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

#include <array>

#include <benchmark/benchmark.h>

#include <boost/convert.hpp>
#include <boost/convert/lexical_cast.hpp>
#include <boost/convert/spirit.hpp>
#include <boost/convert/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <folly/Conv.h>

// bug in boost: this should include make_default.hpp, but doesn't
#include <boost/convert/charconv.hpp>

namespace {

constexpr std::array<std::string_view, 5> integer_strings = {
    "0", "4711", "42", "1337", "1234567890",
};

constexpr std::array<std::string_view, 5> integer_strings_invalid = {
    "a", "4711a", "42a", "1337a", "1234567890a",
};

constexpr std::array<int, 5> integer_values = {0, 4711, 42, 1337, 1234567890};

void int_to_string_folly_to(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v = folly::to<std::string>(i);
      benchmark::DoNotOptimize(v);
    }
  }
}

void int_to_string_lexical_cast(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v = boost::lexical_cast<std::string>(i);
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_folly_to(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = folly::to<int>(s);
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_lexical_cast(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = boost::lexical_cast<int>(s);
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_folly_to_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      auto v = folly::tryTo<int>(s);
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_lexical_cast_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      try {
        auto v = boost::lexical_cast<int>(s);
        benchmark::DoNotOptimize(v);
      } catch (boost::bad_lexical_cast const&) {
      }
    }
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

void int_to_string_convert_lexical_cast(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v =
          boost::convert<std::string>(i, boost::cnv::lexical_cast()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void int_to_string_convert_cstream(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v = boost::convert<std::string>(i, boost::cnv::cstream()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void int_to_string_convert_spirit(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v = boost::convert<std::string>(i, boost::cnv::spirit()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void int_to_string_convert_charconv(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto i : integer_values) {
      auto v = boost::convert<std::string>(i, boost::cnv::charconv()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_lexical_cast(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = boost::convert<int>(s, boost::cnv::lexical_cast()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_cstream(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = boost::convert<int>(s, boost::cnv::cstream()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_spirit(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = boost::convert<int>(s, boost::cnv::spirit()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_charconv(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings) {
      auto v = boost::convert<int>(s, boost::cnv::charconv()).value();
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_lexical_cast_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      auto v = boost::convert<int>(s, boost::cnv::lexical_cast());
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_cstream_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      auto v = boost::convert<int>(s, boost::cnv::cstream());
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_spirit_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      auto v = boost::convert<int>(s, boost::cnv::spirit());
      benchmark::DoNotOptimize(v);
    }
  }
}

void string_to_int_convert_charconv_invalid(::benchmark::State& state) {
  for (auto _ : state) {
    for (auto s : integer_strings_invalid) {
      auto v = boost::convert<int>(s, boost::cnv::charconv());
      benchmark::DoNotOptimize(v);
    }
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace

BENCHMARK(int_to_string_folly_to);
BENCHMARK(int_to_string_lexical_cast);
BENCHMARK(int_to_string_convert_lexical_cast);
BENCHMARK(int_to_string_convert_cstream);
BENCHMARK(int_to_string_convert_spirit);
BENCHMARK(int_to_string_convert_charconv);

BENCHMARK(string_to_int_folly_to);
BENCHMARK(string_to_int_lexical_cast);
BENCHMARK(string_to_int_convert_lexical_cast);
BENCHMARK(string_to_int_convert_cstream);
BENCHMARK(string_to_int_convert_spirit);
BENCHMARK(string_to_int_convert_charconv);

BENCHMARK(string_to_int_folly_to_invalid);
BENCHMARK(string_to_int_lexical_cast_invalid);
BENCHMARK(string_to_int_convert_lexical_cast_invalid);
BENCHMARK(string_to_int_convert_cstream_invalid);
BENCHMARK(string_to_int_convert_spirit_invalid);
BENCHMARK(string_to_int_convert_charconv_invalid);

BENCHMARK_MAIN();

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
#include <bit>
#include <cassert>
#include <charconv>
#include <clocale>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/format.h>

#ifdef _WIN32
#define PSAPI_VERSION 1
#endif

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define DWARFS_UTIL_HAVE_CXXABI_H 1
#endif

#if __has_include(<date/date.h>)
#include <date/date.h>
#define DWARFS_USE_HH_DATE 1
#else
#define DWARFS_USE_HH_DATE 0
#endif

#include <range/v3/view/chunk.hpp>

#include <dwarfs/config.h>

#include <dwarfs/portability/windows.h>

#include <fcntl.h>

#ifdef _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <sys/stat.h>
#endif

#if !defined(_WIN32) || defined(DWARFS_STACKTRACE_ENABLED)
#define DWARFS_INSTALL_FATAL_SIGNAL_HANDLERS
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdio>
#endif

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/cpptrace.hpp>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__)
#include <sched.h>
#endif

#include <dwarfs/compiler.h>
#include <dwarfs/conv.h>
#include <dwarfs/error.h>
#include <dwarfs/os_access.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/string.h>
#include <dwarfs/thrift_lite/demangle.h>
#include <dwarfs/util.h>

extern "C" int dwarfs_wcwidth(int ucs);

namespace dwarfs {

namespace {

auto hardware_concurrency_impl() noexcept -> unsigned int {
#if defined(__linux__) || defined(__FreeBSD__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  if (sched_getaffinity(0, sizeof(cpuset), &cpuset) == 0) {
    auto const count = CPU_COUNT(&cpuset);
    if (count > 0) {
      return static_cast<unsigned int>(count);
    }
  }
#endif
  return std::thread::hardware_concurrency();
}

#ifdef __linux__
void get_self_memory_usage_linux(memory_usage_mode const mode,
                                 memory_usage& usage) {
  static constexpr auto kStatusPath{"/proc/self/status"};
  static constexpr auto kSmapsPath{"/proc/self/smaps_rollup"};
  bool const kAccurate = mode == memory_usage_mode::accurate;
  auto const kSourcePath = kAccurate ? kSmapsPath : kStatusPath;
  std::string_view const kTotalPrefix = kAccurate ? "Pss:" : "VmRSS:";
  std::string_view const kAnonPrefix = kAccurate ? "Pss_Anon:" : "RssAnon:";
  std::string_view const kFilePrefix = kAccurate ? "Pss_File:" : "RssFile:";
  std::string_view const kShmemPrefix = kAccurate ? "Pss_Shmem:" : "RssShmem:";

  struct fclose_deleter {
    // NOLINTNEXTLINE(cert-err33-c,cppcoreguidelines-owning-memory)
    void operator()(std::FILE* fh) { std::fclose(fh); }
  };
  using file_ptr = std::unique_ptr<std::FILE, fclose_deleter>;

  if (auto fh = file_ptr(std::fopen(kSourcePath, "r"))) {
    auto try_parse_line = [](std::string_view line, std::string_view prefix,
                             std::optional<size_t>& field) {
      if (!field.has_value() && line.starts_with(prefix)) {
        std::string_view size_str(line);
        auto const pos = size_str.find_first_not_of(" \t", prefix.size());
        if (pos == std::string_view::npos) {
          return;
        }
        size_str.remove_prefix(pos);
        size_t size;
        auto [endp, ec] = std::from_chars(
            size_str.data(), size_str.data() + size_str.size(), size);
        if (ec == std::errc() && std::string_view(endp).starts_with(" kB")) {
          field = size * 1024;
        }
      }
    };

    char* linebuf{nullptr};
    size_t linebuf_size{0};

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    scope_exit free_linebuf([&linebuf]() { std::free(linebuf); });

    for (;;) {
      auto const read = getline(&linebuf, &linebuf_size, fh.get());

      if (read == -1) {
        break;
      }

      auto line = std::string_view(linebuf, static_cast<size_t>(read));

      try_parse_line(line, kTotalPrefix, usage.total);
      try_parse_line(line, kAnonPrefix, usage.anon);
      try_parse_line(line, kFilePrefix, usage.file);
      try_parse_line(line, kShmemPrefix, usage.shmem);

      if (usage.total && usage.anon && usage.file && usage.shmem) {
        break;
      }
    }
  }
}
#endif

struct rounded_decimal {
  // value = digits * 10^exp10
  // digits has no leading zeros and no trailing zeros, except "0"
  std::string digits;
  int exp10{0};
};

struct exact_digits {
  // First N significant decimal digits of the exact value, without decimal
  // point. scientific exponent of the first digit:
  //   value = 0.digits... * 10^(sci_exp + 1)
  //         = digits[0].digits[1]... * 10^sci_exp
  std::string digits;
  int sci_exp{0};
};

void increment_decimal_string(std::string& s) {
  for (std::size_t i = s.size(); i-- > 0;) {
    if (s[i] != '9') {
      ++s[i];
      return;
    }
    s[i] = '0';
  }
  s.insert(s.begin(), '1');
}

// Computes:
//   q = floor(rem * 10 / den)
//   r = (rem * 10) % den
std::pair<unsigned, std::uint64_t>
mul10_div(std::uint64_t rem, std::uint64_t den) {
  unsigned q = 0;
  std::uint64_t acc = 0;

  assert(den != 0);
  assert(rem < den);

  for (int i = 0; i < 10; ++i) {
    // Since acc < den and rem < den, this computes:
    if (acc >= den - rem) {
      acc = acc - (den - rem);
      ++q;
    } else {
      acc += rem;
    }
  }

  return {q, acc};
}

exact_digits
extract_significant_digits(std::uint64_t num, std::uint64_t den, int count) {
  assert(num > 0);
  assert(den > 0);
  assert(count > 0);

  std::string digits;
  digits.reserve(static_cast<std::size_t>(count));

  int sci_exp;
  std::uint64_t rem;

  auto append_next_digit = [&] {
    auto const [d, next_rem] = mul10_div(rem, den);
    rem = next_rem;
    digits.push_back(static_cast<char>('0' + d));
    return d;
  };

  if (num >= den) {
    auto const integer_part = num / den;
    rem = num % den;

    digits = std::to_string(integer_part);
    sci_exp = static_cast<int>(digits.size()) - 1;

    if (std::cmp_greater(digits.size(), count)) {
      digits.resize(static_cast<std::size_t>(count));
    }
  } else {
    rem = num;
    sci_exp = -1;

    while (append_next_digit() == 0) {
      digits.pop_back();
      --sci_exp;
    }
  }

  while (std::cmp_less(digits.size(), count)) {
    append_next_digit();
  }

  return {std::move(digits), sci_exp};
}

rounded_decimal
round_to_significant(exact_digits const& x, int precision, int shift10) {
  assert(precision > 0);
  assert(x.digits != "0");
  assert(std::cmp_greater(x.digits.size(), precision));

  auto sig = x.digits.substr(0, precision);
  int const guard = x.digits[static_cast<std::size_t>(precision)] - '0';

  if (guard >= 5) {
    increment_decimal_string(sig);
  }

  // keep the scale corresponding to exactly `precision` significant digits
  int exp10 = x.sci_exp + shift10 - (precision - 1);

  while (sig.size() > 1 && sig.back() == '0') {
    sig.pop_back();
    ++exp10;
  }

  return {std::move(sig), exp10};
}

int scientific_exponent(rounded_decimal const& x) {
  assert(x.digits != "0");
  return x.exp10 + static_cast<int>(x.digits.size()) - 1;
}

bool in_range_for_percent(rounded_decimal const& x) {
  int const e = scientific_exponent(x);
  return e >= -1 && e < 2; // [1e-1, 1e2)
}

bool in_range_for_ppm_or_ppb(rounded_decimal const& x) {
  int const e = scientific_exponent(x);
  return e >= 0 && e < 3; // [1, 1000)
}

std::string to_fixed_string(rounded_decimal const& x) {
  assert(x.digits != "0");

  std::string s = x.digits;

  if (x.exp10 >= 0) {
    s.append(x.exp10, '0');
  } else {
    auto const point = std::ssize(s) + x.exp10;

    if (point > 0) {
      s.insert(point, 1, '.');
    } else {
      s.insert(0, "0.");
      s.insert(2, -point, '0');
    }
  }

  return s;
}

std::string to_scientific_string(rounded_decimal const& x) {
  assert(x.digits != "0");

  auto const e = scientific_exponent(x);

  if (x.digits.size() == 1) {
    return x.digits + "e" + std::to_string(e);
  }

  return x.digits.substr(0, 1) + "." + x.digits.substr(1) + "e" +
         std::to_string(e);
}

bool less_than_one_thousandth(rounded_decimal const& x) {
  return scientific_exponent(x) < -3;
}

} // namespace

std::string size_with_unit(file_size_t const size) {
  static constexpr std::array units{"B",   "KiB", "MiB", "GiB",
                                    "TiB", "PiB", "EiB"};
  auto const mag =
      (std::bit_width(
           static_cast<std::make_unsigned_t<file_size_t>>(size | 1)) -
       1) /
      10;
  return fmt::format("{:.4g} {}",
                     static_cast<double>(size) / (1ULL << (mag * 10)),
                     units[mag]);
}

std::string time_with_unit(double const sec) {
  return time_with_unit(
      std::chrono::nanoseconds(static_cast<int64_t>(sec * 1e9)));
}

std::string time_with_unit(std::chrono::nanoseconds const ns) {
  using namespace std::string_view_literals;
  using namespace std::chrono_literals;

  static constexpr int kPrecision = 4;

  auto const total_ns = ns.count();

  assert(total_ns >= 0);

  auto num_digits = [](int64_t n) {
    int digits = 0;
    while (n > 0) {
      n /= 10;
      ++digits;
    }
    return digits;
  };

  auto pow10 = [](int n) -> uint64_t {
    uint64_t v = 1;
    while (n-- > 0) {
      v *= 10;
    }
    return v;
  };

  auto format_decimal = [&](uint64_t whole, uint64_t frac, int frac_digits,
                            std::string_view suffix) {
    std::string out = fmt::format("{}", whole);

    if (frac_digits > 0 && frac > 0) {
      std::string frac_str(frac_digits, '0');
      for (int i = 0; i < frac_digits; ++i) {
        frac_str[frac_digits - 1 - i] = '0' + (frac % 10);
        frac /= 10;
      }

      while (!frac_str.empty() && frac_str.back() == '0') {
        frac_str.pop_back();
      }

      if (!frac_str.empty()) {
        out += '.';
        out += frac_str;
      }
    }

    out += suffix;
    return out;
  };

  auto format_truncated = [&](uint64_t value, uint64_t scale, int frac_digits,
                              std::string_view suffix) {
    auto const whole = value / scale;
    if (frac_digits == 0) {
      return fmt::format("{}{}", whole, suffix);
    }

    auto const factor = pow10(frac_digits);
    auto const frac = (value % scale) * factor / scale; // truncation
    return format_decimal(whole, frac, frac_digits, suffix);
  };

  std::string result = "0s";

  if (total_ns == 0) {
    return result;
  }

  // Sub-minute formatting: choose one unit and show up to 4 significant digits,
  // truncating rather than rounding.
  if (ns < std::chrono::minutes(1)) {
    struct short_unit_spec {
      constexpr short_unit_spec(std::chrono::nanoseconds scale,
                                std::string_view suffix)
          : scale_ns{scale.count()}
          , suffix{suffix} {}

      int64_t scale_ns;
      std::string_view suffix;
    };

    static constexpr std::array short_units{
        short_unit_spec{1s, "s"sv},
        short_unit_spec{1ms, "ms"sv},
        short_unit_spec{1us, "us"sv},
        short_unit_spec{1ns, "ns"sv},
    };

    for (auto const& [scale_ns, suffix] : short_units) {
      if (total_ns >= scale_ns) {
        auto const whole = total_ns / scale_ns;
        auto const digits = num_digits(whole);
        auto const frac_digits = std::max(0, kPrecision - digits);
        result = format_truncated(total_ns, scale_ns, frac_digits, suffix);
        break;
      }
    }

    return result;
  }

  // Minute-and-up formatting: spend a 4-digit budget across d/h/m, then show
  // seconds with truncated decimals if there is budget left.
  struct long_unit_spec {
    constexpr long_unit_spec(std::chrono::nanoseconds scale,
                             std::string_view suffix)
        : scale_ns{scale.count()}
        , suffix{suffix} {}

    int64_t scale_ns;
    std::string_view suffix;
  };

  static constexpr std::array long_units{
      long_unit_spec{24h, "d"sv},
      long_unit_spec{1h, "h"sv},
      long_unit_spec{1min, "m"sv},
  };

  int64_t remainder = total_ns;
  int rem_digits = kPrecision;

  result.clear();

  for (auto const& [scale_ns, suffix] : long_units) {
    auto const value = remainder / scale_ns;
    auto const digits = result.empty() ? num_digits(value) : 2;

    if (value > 0) {
      if (!result.empty()) {
        result += ' ';
      }
      fmt::format_to(std::back_inserter(result), "{}{}", value, suffix);
    }

    rem_digits -= digits;
    remainder -= value * scale_ns;

    if (rem_digits <= 0) {
      break;
    }
  }

  if (rem_digits > 0) {
    auto const frac_digits = std::max(0, rem_digits - 2);
    auto const seconds =
        format_truncated(remainder, 1'000'000'000ULL, frac_digits, "s");

    if (seconds != "0s") {
      if (!result.empty()) {
        result += ' ';
      }
      result += seconds;
    }
  }

  assert(!result.empty());

  return result;
}

file_size_t parse_size_with_unit(std::string const& str) {
  file_size_t value;
  auto [ptr, ec]{std::from_chars(str.data(), str.data() + str.size(), value)};

  if (ec != std::errc()) {
    DWARFS_THROW(runtime_error,
                 fmt::format("cannot parse size value: {}: {}", str,
                             std::make_error_code(ec).message()));
  }

  if (ptr[0] == '\0') {
    return value;
  }

  if (ptr[1] == '\0') {
    switch (ptr[0]) {
    case 't':
    case 'T':
      value <<= 10;
      [[fallthrough]];
    case 'g':
    case 'G':
      value <<= 10;
      [[fallthrough]];
    case 'm':
    case 'M':
      value <<= 10;
      [[fallthrough]];
    case 'k':
    case 'K':
      value <<= 10;
      return value;
    default:
      break;
    }
  }

  DWARFS_THROW(runtime_error, fmt::format("unsupported size suffix: {}", ptr));
}

std::string ratio_to_string(std::uint64_t const num, std::uint64_t const den,
                            int const precision) {
  assert(precision > 0);

  if (den == 0) {
    return "N/A";
  }

  if (num == 0) {
    return "0x";
  }

  // We only ever need the first `precision + 1` significant digits of the
  // exact value. Multiplying by %, ppm, or ppb just shifts the decimal point.
  exact_digits const base = extract_significant_digits(num, den, precision + 1);

  if (auto const percent = round_to_significant(base, precision, 2);
      in_range_for_percent(percent)) {
    return to_fixed_string(percent) + "%";
  }

  if (auto const ppm = round_to_significant(base, precision, 6);
      in_range_for_ppm_or_ppb(ppm)) {
    return to_fixed_string(ppm) + "ppm";
  }

  if (auto const ppb = round_to_significant(base, precision, 9);
      in_range_for_ppm_or_ppb(ppb)) {
    return to_fixed_string(ppb) + "ppb";
  }

  auto const plain = round_to_significant(base, precision, 0);

  if (less_than_one_thousandth(plain)) {
    return to_scientific_string(plain) + "x";
  }

  return to_fixed_string(plain) + "x";
}

std::chrono::nanoseconds parse_time_with_unit(std::string const& str) {
  uint64_t value;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

  if (ec != std::errc()) {
    if (ec == std::errc::invalid_argument && ptr == str.data()) {
      value = 1;
    } else {
      DWARFS_THROW(runtime_error,
                   fmt::format("cannot parse time {}: {}", str,
                               std::make_error_code(ec).message()));
    }
  }

  while (*ptr == ' ') {
    ++ptr;
  }

  std::string_view suffix(ptr, str.data() + str.size() - ptr);

  switch (ptr[0]) {
  case 'd':
    if (ptr[1] == '\0' || suffix == "day") {
      return std::chrono::days(value);
    }
    break;

  case 'h':
    if (ptr[1] == '\0' || suffix == "hour") {
      return std::chrono::hours(value);
    }
    break;

  case 'm':
    if (ptr[1] == '\0' || suffix == "min") {
      return std::chrono::minutes(value);
    } else if (suffix == "ms" || suffix == "msec") {
      return std::chrono::milliseconds(value);
    }
    break;

  case 'u':
    if (suffix == "us" || suffix == "usec") {
      return std::chrono::microseconds(value);
    }
    break;

  case 'n':
    if (suffix == "ns" || suffix == "nsec") {
      return std::chrono::nanoseconds(value);
    }
    break;

  case 's':
    if (ptr[1] != '\0' && suffix != "sec") {
      break;
    }
    [[fallthrough]];
  case '\0':
    return std::chrono::seconds(value);

  default:
    break;
  }

  DWARFS_THROW(runtime_error,
               fmt::format("unsupported time suffix: {}", suffix));
}

std::chrono::system_clock::time_point parse_time_point(std::string const& str) {
  static constexpr std::array<char const*, 9> formats{
      "%Y%m%dT%H%M%S", "%Y%m%dT%H%M", "%Y%m%dT", "%F %T", "%FT%T",
      "%F %R",         "%FT%R",       "%FT",     "%F"};

  for (auto const& fmt : formats) {
    std::istringstream iss(str);
    std::chrono::system_clock::time_point tp;
#if DWARFS_USE_HH_DATE
    date::from_stream(iss, fmt, tp);
#else
    std::chrono::from_stream(iss, fmt, tp);
#endif
    if (!iss.fail()) {
      iss.peek();
      if (iss.eof()) {
        return tp;
      }
    }
  }

  DWARFS_THROW(runtime_error, "cannot parse time point");
}

std::unordered_map<std::string_view, std::string_view>
parse_option_string(std::string_view str) {
  std::unordered_map<std::string_view, std::string_view> opts;

  for (auto const part : split_to<std::vector<std::string_view>>(str, ',')) {
    auto const pos = part.find('=');

    if (pos != std::string_view::npos) {
      opts.emplace(part.substr(0, pos), part.substr(pos + 1));
    } else {
      opts.emplace(part, std::string_view{});
    }
  }

  return opts;
}

size_t utf8_display_width(char const* p, size_t len) {
  char const* const e = p + len;
  size_t rv = 0;

  while (p < e) {
    auto cp = utf8::next(p, e);
    rv += dwarfs_wcwidth(cp);
  }

  return rv;
}

size_t utf8_display_width(std::string const& str) {
  return utf8_display_width(str.data(), str.size());
}

void utf8_truncate(std::string& str, size_t len) {
  char const* p = str.data();
  char const* const e = p + str.size();
  size_t l = 0;

  while (p < e && l <= len) {
    auto np = p;
    auto cp = utf8::next(np, e);
    l += dwarfs_wcwidth(cp);
    if (l > len) {
      break;
    }
    p = np;
  }

  str.resize(p - str.data());
}

void utf8_sanitize(std::string& str) {
  if (!utf8::is_valid(str)) [[unlikely]] {
    str = utf8::replace_invalid(str);
  }
}

void shorten_path_string(std::string& path, char separator, size_t max_len) {
  if (utf8_display_width(path) > max_len) {
    if (max_len < 3) {
      path.clear();
      return;
    }

    size_t start = 0;

    while (utf8_display_width(path.data() + start, path.size() - start) >
           max_len - 3) {
      auto next = path.find(separator, start + 1);
      if (next == std::string::npos) {
        break;
      }
      start = next;
    }

    path.replace(0, start, "...");

    if (utf8_display_width(path) > max_len) {
      if (max_len >= 7) {
        utf8_truncate(path, max_len - 3);
        path += "...";
      } else {
        path = "...";
      }
    }
  }
}

std::filesystem::path canonical_path(std::filesystem::path p) {
  if (!p.empty()) {
    try {
      p = std::filesystem::canonical(p);
    } catch (std::filesystem::filesystem_error const&) {
      p = std::filesystem::absolute(p);
    }

#ifdef _WIN32
    if (auto wstr = p.wstring(); !wstr.starts_with(L"\\\\")) {
      p = std::filesystem::path(L"\\\\?\\" + wstr);
    }
#endif
  }

  return p;
}

std::string path_to_utf8_string_sanitized(std::filesystem::path const& p) {
#ifdef _WIN32
  static_assert(std::is_same_v<std::filesystem::path::value_type, wchar_t>);

  auto const& in = p.native();
  if (in.empty()) {
    return {};
  }
  int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(),
                                          NULL, 0, NULL, NULL);
  std::string out(size_needed, 0);
  ::WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), &out[0],
                        size_needed, NULL, NULL);
  return out;
#else
  return u8string_to_string(p.u8string());
#endif
}

bool getenv_is_enabled(char const* var) {
  if (auto val = std::getenv(var)) {
    if (auto maybeBool = try_to<bool>(val); maybeBool && *maybeBool) {
      return true;
    }
  }
  return false;
}

bool getenv_is_enabled(os_access const& os, char const* var) {
  if (auto val = os.getenv(var)) {
    if (auto maybeBool = try_to<bool>(*val); maybeBool && *maybeBool) {
      return true;
    }
  }
  return false;
}

void setup_default_locale() {
  try {
#ifdef _WIN32
    char const* locale = "en_US.utf8";
#else
    char const* locale = "";
#endif
    std::locale::global(std::locale(locale));
    if (!std::setlocale(LC_ALL, locale)) {
      std::cerr << "warning: setlocale(LC_ALL, \"\") failed\n";
    }
  } catch (std::exception const& e) {
    std::cerr << "warning: failed to set user default locale: " << e.what()
              << "\n";
    try {
      std::locale::global(std::locale::classic());
      if (!std::setlocale(LC_ALL, "C")) {
        std::cerr << "warning: setlocale(LC_ALL, \"C\") failed\n";
      }
    } catch (std::exception const& e) {
      std::cerr << "warning: also failed to set classic locale: " << e.what()
                << "\n";
    }
  }
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
}

std::string error_cp_to_utf8(std::string_view error) {
  std::string input(error);
#ifdef _WIN32
  UINT cp = GetACP();
  // convert from codepage to UTF-16
  int wclen = MultiByteToWideChar(cp, 0, input.c_str(), -1, NULL, 0);
  if (wclen == 0) {
    utf8_sanitize(input);
    return input;
  }
  std::wstring wstr(wclen, L'\0');
  MultiByteToWideChar(cp, 0, input.c_str(), -1, &wstr[0], wclen);

  // convert from UTF-16 to UTF-8
  int utf8len =
      WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
  if (utf8len == 0) {
    utf8_sanitize(input);
    return input;
  }
  std::string utf8err(utf8len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8err[0], utf8len, NULL,
                      NULL);

  // remove the null terminator added by WideCharToMultiByte
  if (!utf8err.empty() && utf8err.back() == '\0') {
    utf8err.pop_back();
  }

  return utf8err;
#else
  return input;
#endif
}

std::string_view basename(std::string_view path) {
  auto pos = path.find_last_of("/\\");
  if (pos == std::string_view::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

void ensure_binary_mode(std::ostream& os [[maybe_unused]]) {
#ifdef _WIN32
  if (&os == &std::cout) {
    _setmode(_fileno(stdout), _O_BINARY);
  } else if (&os == &std::cerr) {
    _setmode(_fileno(stderr), _O_BINARY);
  }
#endif
}

std::string exception_str(std::exception const& e) {
  return fmt::format("{}: {}", thrift_lite::demangle(typeid(e)), e.what());
}

namespace {

std::type_info const* get_current_exception_type_info() {
#ifdef DWARFS_UTIL_HAVE_CXXABI_H
  return __cxxabiv1::__cxa_current_exception_type();
#else
  return nullptr;
#endif
}

} // namespace

std::string exception_str(std::exception_ptr const& e) {
  try {
    std::rethrow_exception(e);
  } catch (std::exception const& ex) {
    return exception_str(ex);
  } catch (...) {
    // cppcheck-suppress knownConditionTrueFalse
    if (auto const ti = get_current_exception_type_info()) {
      return fmt::format("unknown exception of type {}",
                         thrift_lite::demangle(*ti));
    }
    return "unknown non-standard exception";
  }
}

std::string hexdump(void const* data, size_t size) {
  static constexpr size_t kBytesPerRow = 16;
  std::ostringstream oss;
  std::span<uint8_t const> bytes(static_cast<uint8_t const*>(data), size);
  std::uint64_t addr{0};

  for (auto const row : bytes | ranges::views::chunk(kBytesPerRow)) {
    oss << fmt::format("{:08x} ", addr);

    for (size_t i = 0; i < kBytesPerRow; ++i) {
      if (i % 8 == 0) {
        oss << " ";
      }
      if (i < row.size()) {
        oss << fmt::format("{:02x} ", row[i]);
      } else {
        oss << "   ";
      }
    }

    oss << " |";

    for (size_t i = 0; i < kBytesPerRow; ++i) {
      if (i < row.size()) {
        uint8_t const byte = row[i];
        oss << (std::isprint(byte) ? static_cast<char>(byte) : '.');
      } else {
        oss << ' ';
      }
    }

    oss << "|\n";

    addr += kBytesPerRow;
  }

  return oss.str();
}

std::string hexlify(std::span<std::byte const> data) {
  using namespace std::string_view_literals;
  static constexpr auto hex_digits = "0123456789abcdef"sv;
  std::string result;
  result.reserve(data.size() * 2);

  for (std::byte const b : data) {
    result += hex_digits[static_cast<uint8_t>(b) >> 4];
    result += hex_digits[static_cast<uint8_t>(b) & 0x0F];
  }

  return result;
}

std::string hexlify(std::string_view data) {
  return hexlify(std::as_bytes(std::span(data)));
}

unsigned int hardware_concurrency() noexcept {
  static auto const env = [] {
    std::optional<int> concurrency;
    if (auto env = std::getenv("DWARFS_OVERRIDE_HARDWARE_CONCURRENCY")) {
      concurrency = try_to<int>(env);
    }
    return concurrency;
  }();
  return env.value_or(hardware_concurrency_impl());
}

int get_current_umask() {
  // I'm pretty certain these warnings by Flawfinder are false positives.
  // After all, we're just doing a no-op by re-setting the original value
  // in order to read it.
  auto mask = ::umask(0077); /* Flawfinder: ignore */
  ::umask(mask);             /* Flawfinder: ignore */
  return mask;
}

namespace {

#ifdef DWARFS_INSTALL_FATAL_SIGNAL_HANDLERS

struct fatal_signal {
  int signum;
  std::string_view name;
};

constexpr std::array kFatalSignals{
#ifdef DWARFS_STACKTRACE_ENABLED
    fatal_signal{SIGSEGV, "SIGSEGV"}, fatal_signal{SIGILL, "SIGILL"},
    fatal_signal{SIGFPE, "SIGFPE"},   fatal_signal{SIGABRT, "SIGABRT"},
    fatal_signal{SIGTERM, "SIGTERM"},
#endif
#ifndef _WIN32
    fatal_signal{SIGBUS, "SIGBUS"},
#endif
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::once_flag g_signal_handlers_installed;

#ifdef _WIN32

std::vector<HANDLE> suspend_other_threads() {
  std::vector<HANDLE> handles;
  DWORD currend_tid = ::GetCurrentThreadId();
  DWORD current_pid = ::GetCurrentProcessId();

  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return handles;
  }

  THREADENTRY32 te;
  te.dwSize = sizeof(THREADENTRY32);

  if (::Thread32First(snapshot, &te)) {
    do {
      if (te.th32OwnerProcessID == current_pid &&
          te.th32ThreadID != currend_tid) {
        HANDLE th =
            ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                         FALSE, te.th32ThreadID);
        if (th) {
          if (::SuspendThread(th) != static_cast<DWORD>(-1)) {
            handles.push_back(th);
          } else {
            ::CloseHandle(th);
          }
        }
      }
    } while (::Thread32Next(snapshot, &te));
  }

  ::CloseHandle(snapshot);

  return handles;
}

void resume_suspended_threads(std::vector<HANDLE> const& handles) {
  for (auto th : handles) {
    ::ResumeThread(th);
    ::CloseHandle(th);
  }
}

void fatal_signal_handler_win(int signal) {
  auto suspended = suspend_other_threads();

  std::optional<std::string_view> signame;

  for (size_t i = 0; i < kFatalSignals.size(); ++i) {
    if (signal == kFatalSignals[i].signum) {
      signame = kFatalSignals[i].name;
      break;
    }
  }

  if (!signame) {
    signame = std::to_string(signal);
  }

  std::cerr << "Caught signal " << *signame << "\n";
#ifdef DWARFS_STACKTRACE_ENABLED
  cpptrace::generate_trace().print(std::cerr);
#endif

  resume_suspended_threads(suspended);

  ::exit(1);
}

#else

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<struct ::sigaction, kFatalSignals.size()> old_handlers;

void fatal_signal_handler_posix(int signal) {
  std::optional<std::string_view> signame;

  for (size_t i = 0; i < kFatalSignals.size(); ++i) {
    if (signal == kFatalSignals[i].signum) {
      ::sigaction(signal, &old_handlers[i], nullptr);
      signame = kFatalSignals[i].name;
      break;
    }
  }

  if (!signame) {
    struct ::sigaction sa_dfl{};
    sa_dfl.sa_handler = SIG_DFL;
    ::sigaction(signal, &sa_dfl, nullptr);
    signame = std::to_string(signal);
  }

  std::cerr << "*** Caught signal " << *signame << "\n";

#ifdef DWARFS_STACKTRACE_ENABLED
  cpptrace::generate_trace().print(std::cerr);
#endif

  if (signal == SIGBUS) {
    std::cerr << R"EOF(
*******************************************************************************
*
* SIGBUS is often caused by memory mapping files that are stored on faulty or
* unreliable storage devices, e.g. network shares or USB drives. You can try
* re-running the command with the following environment variable set to use
* regular reads instead of memory mapping:
*
*   export DWARFS_IOLAYER_OPTS=open_mode=read
*
*******************************************************************************

)EOF";
  }

#ifdef DWARFS_COVERAGE_ENABLED
  std::exit(1);
#else
  if (::raise(signal) != 0) {
    std::cerr << "Failed to re-raise signal " << *signame << "\n";
    std::abort();
  }
#endif
}

#endif

void install_signal_handlers_impl() {
  for (size_t i = 0; i < kFatalSignals.size(); ++i) {
#ifdef _WIN32
    ::signal(kFatalSignals[i].signum, fatal_signal_handler_win);
#else
    struct ::sigaction new_sa{};
    // this is potentially implemented as a macro
    sigfillset(&new_sa.sa_mask);
    new_sa.sa_handler = fatal_signal_handler_posix;
    ::sigaction(kFatalSignals[i].signum, &new_sa, &old_handlers[i]);
#endif
  }
}

#endif // DWARFS_INSTALL_FATAL_SIGNAL_HANDLERS

#ifdef _MSC_VER
int __cdecl
crt_report_hook(int report_type, char* message, int* /*return_value*/) {
  if (report_type == _CRT_ASSERT) {
    std::fputs("\n=== CRT ASSERT ===\n", stderr);
    if (message)
      std::fputs(message, stderr);
#ifdef DWARFS_STACKTRACE_ENABLED
    cpptrace::generate_trace(2).print();
#endif
    std::fflush(stderr);
  }
  return FALSE; // let CRT continue its normal handling
}
#endif

} // namespace

void install_signal_handlers() {
#ifdef DWARFS_INSTALL_FATAL_SIGNAL_HANDLERS
  std::call_once(g_signal_handlers_installed, install_signal_handlers_impl);
#endif
#ifdef _MSC_VER
  // Route asserts to stderr (avoids GUI dialogs in many setups)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, crt_report_hook);
#endif
}

std::tm safe_localtime(std::time_t t) {
  std::tm buf{};
#ifdef _WIN32
  if (auto r = ::localtime_s(&buf, &t); r != 0) {
    DWARFS_THROW(runtime_error, fmt::format("localtime_s: error code {}", r));
  }
#else
  if (!::localtime_r(&t, &buf)) {
    DWARFS_THROW(runtime_error,
                 fmt::format("localtime_r: error code {}", errno));
  }
#endif
  return buf;
}

memory_usage get_self_memory_usage(memory_usage_mode mode [[maybe_unused]]) {
  memory_usage usage;

#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX pmc{};

  if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                             reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&pmc),
                             sizeof(pmc))) {
    if (pmc.PrivateUsage > 0) {
      usage.total = static_cast<size_t>(pmc.PrivateUsage);
      // TODO: others?
    }
  }
#elif defined(__APPLE__)
  task_vm_info info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    usage.total = info.phys_footprint;
    // TODO: others?
  }
#elif defined(__FreeBSD__)
  std::array<int, 4> mib{CTL_KERN, KERN_PROC, KERN_PROC_PID,
                         static_cast<int>(::getpid())};
  struct kinfo_proc kp;
  size_t len = sizeof(kp);

  if (::sysctl(mib.data(), 4, &kp, &len, nullptr, 0) == 0 &&
      len == sizeof(kp)) {
    size_t const page_sz = static_cast<size_t>(::getpagesize());
    usage.total = static_cast<size_t>(kp.ki_rssize) * page_sz;
  }
#elif defined(__linux__)
  get_self_memory_usage_linux(mode, usage);

  if (mode == memory_usage_mode::accurate && !usage.total.has_value()) {
    // fallback to fast mode if smaps_rollup is not accessible
    get_self_memory_usage_linux(memory_usage_mode::fast, usage);
  }
#endif

  return usage;
}

} // namespace dwarfs

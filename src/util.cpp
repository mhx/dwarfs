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
#include <charconv>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#define PSAPI_VERSION 1
#endif

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#if __has_include(<date/date.h>)
#include <date/date.h>
#define DWARFS_USE_HH_DATE 1
#else
#define DWARFS_USE_HH_DATE 0
#endif

#include <folly/ExceptionString.h>
#include <folly/String.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/SysStat.h>
#include <folly/portability/Windows.h>
#include <folly/system/HardwareConcurrency.h>

#include <dwarfs/config.h>

#ifdef _WIN32
#include <psapi.h>
#endif

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/cpptrace.hpp>
#include <csignal>

#ifdef _WIN32
#include <tlhelp32.h>
#endif
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <dwarfs/conv.h>
#include <dwarfs/error.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

extern "C" int dwarfs_wcwidth(int ucs);

namespace dwarfs {

namespace {

inline std::string trimmed(std::string in) {
  while (!in.empty() && in.back() == ' ') {
    in.pop_back();
  }
  return in;
}

} // namespace

std::string size_with_unit(file_size_t size) {
  return trimmed(folly::prettyPrint(size, folly::PRETTY_BYTES_IEC, true));
}

std::string time_with_unit(double sec) {
  return trimmed(folly::prettyPrint(sec, folly::PRETTY_TIME_HMS, false));
}

std::string time_with_unit(std::chrono::nanoseconds ns) {
  return time_with_unit(1e-9 * ns.count());
}

file_size_t parse_size_with_unit(std::string const& str) {
  file_size_t value;
  auto [ptr, ec]{std::from_chars(str.data(), str.data() + str.size(), value)};

  if (ec != std::errc()) {
    DWARFS_THROW(runtime_error, "cannot parse size value");
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

  DWARFS_THROW(runtime_error, "unsupported size suffix");
}

std::chrono::milliseconds parse_time_with_unit(std::string const& str) {
  uint64_t value;
  auto [ptr, ec]{std::from_chars(str.data(), str.data() + str.size(), value)};

  if (ec != std::errc()) {
    DWARFS_THROW(runtime_error, "cannot parse time value");
  }

  switch (ptr[0]) {
  case 'h':
    if (ptr[1] == '\0') {
      return std::chrono::hours(value);
    }
    break;

  case 'm':
    if (ptr[1] == '\0') {
      return std::chrono::minutes(value);
    } else if (ptr[1] == 's' && ptr[2] == '\0') {
      return std::chrono::milliseconds(value);
    }
    break;

  case 's':
    if (ptr[1] != '\0') {
      break;
    }
    [[fallthrough]];
  case '\0':
    return std::chrono::seconds(value);

  default:
    break;
  }

  DWARFS_THROW(runtime_error, "unsupported time suffix");
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
  if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>) {
    auto const& in = p.native();
    if (in.empty()) {
      return {};
    }
    int size_needed = ::WideCharToMultiByte(
        CP_UTF8, 0, in.data(), (int)in.size(), NULL, 0, NULL, NULL);
    std::string out(size_needed, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), &out[0],
                          size_needed, NULL, NULL);
    return out;
  }
#endif

  return u8string_to_string(p.u8string());
}

bool getenv_is_enabled(char const* var) {
  if (auto val = std::getenv(var)) {
    if (auto maybeBool = try_to<bool>(val); maybeBool && *maybeBool) {
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
  return folly::exceptionStr(e).toStdString();
}

std::string exception_str(std::exception_ptr const& e) {
#if defined(__linux__) && defined(__arm__)
  try {
    if (e) {
      std::rethrow_exception(e);
    }
  } catch (std::exception const& ex) {
    return folly::exceptionStr(ex).toStdString();
  } catch (...) {
    return "unknown exception";
  }
  return "no exception";
#else
  return folly::exceptionStr(e).toStdString();
#endif
}

std::string hexdump(void const* data, size_t size) {
  return folly::hexDump(data, size);
}

unsigned int hardware_concurrency() noexcept {
  static auto const env = [] {
    std::optional<int> concurrency;
    if (auto env = std::getenv("DWARFS_OVERRIDE_HARDWARE_CONCURRENCY")) {
      concurrency = try_to<int>(env);
    }
    return concurrency;
  }();
  return env.value_or(folly::hardware_concurrency());
}

int get_current_umask() {
  // I'm pretty certain these warnings by Flawfinder are false positives.
  // After all, we're just doing a no-op by re-setting the original value
  // in order to read it.
  auto mask = ::umask(0077); /* Flawfinder: ignore */
  ::umask(mask);             /* Flawfinder: ignore */
  return mask;
}

#ifdef DWARFS_STACKTRACE_ENABLED

namespace {

struct fatal_signal {
  int signum;
  std::string_view name;
};

constexpr std::array kFatalSignals{
    fatal_signal{SIGSEGV, "SIGSEGV"}, fatal_signal{SIGILL, "SIGILL"},
    fatal_signal{SIGFPE, "SIGFPE"},   fatal_signal{SIGABRT, "SIGABRT"},
    fatal_signal{SIGTERM, "SIGTERM"},
#ifndef _WIN32
    fatal_signal{SIGBUS, "SIGBUS"},
#endif
};

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

  std::optional<std::string> signame;

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
  cpptrace::generate_trace().print();

  resume_suspended_threads(suspended);

  ::exit(1);
}

#else

std::array<struct ::sigaction, kFatalSignals.size()> old_handlers;

void fatal_signal_handler_posix(int signal) {
  std::optional<std::string> signame;

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

  std::cerr << "Caught signal " << *signame << "\n";
  cpptrace::generate_trace().print();

  if (::raise(signal) != 0) {
    std::cerr << "Failed to re-raise signal " << *signame << "\n";
    std::abort();
  }
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

} // namespace

#endif

void install_signal_handlers() {
#ifdef DWARFS_STACKTRACE_ENABLED
  std::call_once(g_signal_handlers_installed, install_signal_handlers_impl);
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

std::optional<size_t> get_self_memory_usage() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX pmc{};

  if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                             reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&pmc),
                             sizeof(pmc))) {
    if (pmc.PrivateUsage > 0) {
      return static_cast<size_t>(pmc.PrivateUsage);
    }
  }
#elif defined(__APPLE__)
  task_vm_info info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    return info.phys_footprint;
  }
#elif defined(__linux__)
  static constexpr auto kSmapsPath{"/proc/self/smaps_rollup"};
  std::ifstream smaps(kSmapsPath);

  if (smaps) {
    std::string line;

    while (std::getline(smaps, line)) {
      if (line.starts_with("Pss:")) {
        std::string_view size_str(line);
        size_str.remove_prefix(size_str.find_first_not_of(" \t", 4));
        size_t size;
        auto [endp, ec] = std::from_chars(
            size_str.data(), size_str.data() + size_str.size(), size);
        if (ec == std::errc() && std::string_view(endp).starts_with(" kB")) {
          return size * 1024;
        }
      }
    }
  }
#endif
  return std::nullopt; // Not implemented
}

} // namespace dwarfs

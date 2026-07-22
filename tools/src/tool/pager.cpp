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

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <dwarfs/os_access.h>
#include <dwarfs/string.h>
#include <dwarfs/tool/pager.h>
#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool {

namespace {

std::span<pager_program const> get_pagers() {
  static std::vector<pager_program> const pagers{
      {"less", {"-R"}},
  };

  return pagers;
}

#ifdef _WIN32

// Quote a single argument per the CommandLineToArgvW rules, so the child
// re-parses exactly what we intended. See "Parsing C++ Command-Line
// Arguments" / Daniel Colascione's "Everyone quotes command line arguments
// the wrong way".
void append_quoted(std::wstring& cmd, std::wstring const& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    cmd += arg;
    return;
  }

  cmd += L'"';

  for (auto it = arg.begin();;) {
    size_t backslashes = 0;

    while (it != arg.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }

    if (it == arg.end()) {
      // Escape all backslashes, but leave the terminating quote unescaped.
      cmd.append(2 * backslashes, L'\\');
      break;
    }

    if (*it == L'"') {
      // Escape all backslashes and the following quote.
      cmd.append(2 * backslashes + 1, L'\\');
      cmd += L'"';
    } else {
      // Backslashes are not special here.
      cmd.append(backslashes, L'\\');
      cmd += *it;
    }

    ++it;
  }

  cmd += L'"';
}

#endif

} // namespace

#ifdef _WIN32
#define X_OK 0
#endif

std::optional<pager_program> find_pager_program(os_access const& os) {
  if (auto pager_env = os.getenv("PAGER")) {
    std::string_view sv{pager_env.value()};

    if (sv == "cat") {
      return std::nullopt;
    }

    if (sv.starts_with('"') && sv.ends_with('"')) {
      sv.remove_prefix(1);
      sv.remove_suffix(1);
    }

    // split into program and arguments
    auto args = split_to<std::vector<std::string>>(sv, ' ');
    std::filesystem::path p{args.front()};
    args.erase(args.begin());

    if (os.access(p, X_OK) == 0) {
      return pager_program{p, args};
    }

    if (auto exe = os.find_executable(p); !exe.empty()) {
      if (exe.filename() == "less" && args.empty()) {
        args.emplace_back("-R");
      }
      return pager_program{exe, args};
    }
  }

  for (auto const& p : get_pagers()) {
    if (auto exe = os.find_executable(p.name); !exe.empty()) {
      return pager_program{exe, p.args};
    }
  }

  return std::nullopt;
}

#ifdef _WIN32

void show_in_pager(pager_program const& pager, std::string_view text) {
  std::wstring cmdline;
  append_quoted(cmdline, pager.name.wstring());

  for (auto const& arg : pager.args) {
    cmdline += L' ';
    append_quoted(cmdline, string_to_sys_string(arg));
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE rd{};
  HANDLE wr{};

  if (!::CreatePipe(&rd, &wr, &sa, 0)) {
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(), "CreatePipe");
  }

  // The write end must not leak into the child, or it will never see EOF.
  ::SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = rd;
  si.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION pi{};

  // CreateProcessW may modify the command line buffer in place.
  std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
  buf.push_back(L'\0');

  BOOL ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0,
                             nullptr, nullptr, &si, &pi);

  auto err = ::GetLastError();
  ::CloseHandle(rd);

  if (!ok) {
    ::CloseHandle(wr);
    throw std::system_error(static_cast<int>(err), std::system_category(),
                            "CreateProcessW");
  }

  size_t offset = 0;

  while (offset < text.size()) {
    auto chunk =
        static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1u << 20));
    DWORD written = 0;

    if (!::WriteFile(wr, text.data() + offset, chunk, &written, nullptr)) {
      break; // pager exited early; nothing more we can do
    }

    offset += written;
  }

  ::CloseHandle(wr);
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
}

#else

void show_in_pager(pager_program const& pager, std::string_view text) {
  int fds[2];

  if (::pipe(fds) != 0) {
    throw std::system_error(errno, std::generic_category(), "pipe");
  }

  posix_spawn_file_actions_t actions;
  ::posix_spawn_file_actions_init(&actions);
  ::posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
  ::posix_spawn_file_actions_addclose(&actions, fds[0]);
  ::posix_spawn_file_actions_addclose(&actions, fds[1]);

  // Make sure the pager doesn't inherit our ignored SIGPIPE.
  posix_spawnattr_t attr;
  ::posix_spawnattr_init(&attr);
  sigset_t defaults;
  ::sigemptyset(&defaults);
  ::sigaddset(&defaults, SIGPIPE);
  ::posix_spawnattr_setsigdefault(&attr, &defaults);
  ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

  auto path = pager.name.string();

  std::vector<char*> argv;
  argv.reserve(pager.args.size() + 2);
  argv.push_back(path.data());
  for (auto const& arg : pager.args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid{};
  int rv = ::posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(),
                         ::environ);

  ::posix_spawn_file_actions_destroy(&actions);
  ::posix_spawnattr_destroy(&attr);
  ::close(fds[0]);

  if (rv != 0) {
    ::close(fds[1]);
    throw std::system_error(rv, std::generic_category(), "posix_spawn");
  }

  // If the user quits the pager early we get EPIPE rather than a signal.
  auto prev_sigpipe = ::signal(SIGPIPE, SIG_IGN);

  size_t offset = 0;

  while (offset < text.size()) {
    auto n = ::write(fds[1], text.data() + offset, text.size() - offset);

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break; // EPIPE: pager exited early
    }

    offset += static_cast<size_t>(n);
  }

  ::signal(SIGPIPE, prev_sigpipe);

  // Closing the write end is what tells the pager it has the whole input.
  ::close(fds[1]);

  int status{};
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    // retry
  }
}

#endif

} // namespace dwarfs::tool

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
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#include <dwarfs/tool/internal/pager_command_line.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <dwarfs/detail/scoped_env.h>
#include <dwarfs/os_access.h>
#include <dwarfs/tool/pager.h>
#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool {

namespace {

constexpr std::array pager_env_vars{"DWARFS_PAGER", "PAGER"};

constexpr char const* default_pager{"less"};
constexpr char const* less_env_name{"LESS"};
constexpr char const* less_env_value{"FRX"};

/**
 * Turn a pager command line into something we can spawn.
 *
 * POSIX defines PAGER as "any string acceptable as a command string operand
 * to the sh -c command" (see environ(7)), so that is exactly what we do with
 * it. Windows has no shell we can rely on, so we parse the command line with
 * the platform's own rules instead.
 */
std::optional<pager_program> make_pager_program(std::string_view cmd) {
#ifdef _WIN32
  auto args = internal::split_command_line(cmd);

  if (!args || args->empty()) {
    return std::nullopt;
  }

  std::filesystem::path name{args->front()};
  args->erase(args->begin());

  return pager_program{std::move(name), std::move(*args), std::string{cmd}};
#else
  return pager_program{"/bin/sh", {"-c", std::string{cmd}}, std::string{cmd}};
#endif
}

} // namespace

std::optional<pager_program> find_pager_program(os_access const& os) {
  for (auto const* var : pager_env_vars) {
    auto value = os.getenv(var);

    if (!value) {
      continue;
    }

    std::string_view cmd{value.value()};

    // A blank value or literally "cat" disables paging.
    if (std::ranges::all_of(cmd, isspace) || cmd == "cat") {
      return std::nullopt;
    }

    return make_pager_program(cmd);
  }

  // No preference expressed: try the default pager and let show_in_pager()
  // report it if it isn't there.
  return make_pager_program(default_pager);
}

#ifdef _WIN32

void show_in_pager(pager_program const& pager, std::string_view text,
                   std::error_code& ec) {
  ec.clear();

  std::wstring cmdline;
  internal::append_quoted(cmdline, pager.name.wstring());

  for (auto const& arg : pager.args) {
    cmdline += L' ';
    internal::append_quoted(cmdline, string_to_sys_string(arg));
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE rd{};
  HANDLE wr{};

  if (!::CreatePipe(&rd, &wr, &sa, 0)) {
    ec.assign(static_cast<int>(::GetLastError()), std::system_category());
    return;
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

  BOOL ok{};
  DWORD err{};

  {
    // The child inherits the environment as it is at CreateProcessW() time,
    // so keep the modification scoped as tightly as possible.
    detail::scoped_env env;
    env.set_if_unset(less_env_name, less_env_value);

    ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0,
                          nullptr, nullptr, &si, &pi);
    err = ::GetLastError();
  }

  ::CloseHandle(rd);

  if (!ok) {
    ::CloseHandle(wr);
    ec.assign(static_cast<int>(err), std::system_category());
    return;
  }

  size_t offset = 0;

  while (offset < text.size()) {
    auto chunk =
        static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1u << 20));
    DWORD written = 0;

    if (!::WriteFile(wr, text.data() + offset, chunk, &written, nullptr) ||
        written == 0) {
      // The pager exited early. It has been showing our output, so this is
      // not an error: reporting one would make the caller print everything
      // a second time.
      break;
    }

    offset += written;
  }

  ::CloseHandle(wr);
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
}

#else

void show_in_pager(pager_program const& pager, std::string_view text,
                   std::error_code& ec) {
  ec.clear();

  int fds[2];

  if (::pipe(fds) != 0) {
    ec.assign(errno, std::generic_category());
    return;
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
  sigemptyset(&defaults);        // macro on macOS, don't qualify
  sigaddset(&defaults, SIGPIPE); // macro on macOS, don't qualify
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
  int rv{};

  {
    // The child inherits ::environ as it is at posix_spawn() time, so keep
    // the modification scoped as tightly as possible.
    detail::scoped_env env;
    env.set_if_unset(less_env_name, less_env_value);

    rv = ::posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(),
                       ::environ);
  }

  ::posix_spawn_file_actions_destroy(&actions);
  ::posix_spawnattr_destroy(&attr);
  ::close(fds[0]);

  if (rv != 0) {
    ::close(fds[1]);
    ec.assign(rv, std::generic_category());
    return;
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
      // EPIPE: the pager exited early. It has been showing our output, so
      // this is not an error: reporting one would make the caller print
      // everything a second time.
      break;
    }

    offset += static_cast<size_t>(n);
  }

  ::signal(SIGPIPE, prev_sigpipe);

  // Closing the write end is what tells the pager it has the whole input.
  ::close(fds[1]);

  int status{};

  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      ec.assign(errno, std::generic_category());
      return;
    }
  }

  // sh(1) reports 127 for "command not found" and 126 for "found but not
  // executable". Those are the only exit codes that tell us the text was
  // never displayed; anything else we have to assume the pager did its job.
  // (A pager that legitimately exits 127 will be misread here.)
  if (WIFEXITED(status)) {
    switch (WEXITSTATUS(status)) {
    case 126:
      ec = std::make_error_code(std::errc::permission_denied);
      break;

    case 127:
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      break;

    default:
      break;
    }
  }
}

#endif

} // namespace dwarfs::tool

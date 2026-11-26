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

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <dwarfs/file_util.h>
#include <dwarfs/scope_exit.h>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "subprocess.h"

namespace dwarfs::test {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

std::string
display_command(fs::path const& prog, subprocess::argument_list const& args) {
  std::ostringstream os;
  auto append = [&os](fs::path const& path) {
    auto s = path.u8string();
    os << std::quoted(std::string(s.begin(), s.end()));
  };
  append(prog);
  for (auto const& arg : args) {
    os << ' ';
    append(arg);
  }
  return os.str();
}

void validate_arg(fs::path const& arg) {
  if (arg.native().find(fs::path::value_type{}) !=
      fs::path::string_type::npos) {
    throw std::invalid_argument("NUL in subprocess argument");
  }
}

#ifdef _WIN32

[[noreturn]] void check_windows(char const* what) {
  throw std::system_error(static_cast<int>(::GetLastError()),
                          std::system_category(), what);
}

class unique_handle {
 public:
  explicit unique_handle(HANDLE handle = nullptr)
      : handle_{handle} {}

  ~unique_handle() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle_);
    }
  }

  unique_handle(unique_handle const&) = delete;
  unique_handle& operator=(unique_handle const&) = delete;

  HANDLE get() const { return handle_; }
  void reset(HANDLE handle) { handle_ = handle; }

 private:
  HANDLE handle_{};
};

void append_quoted(std::wstring& cmd, std::wstring const& arg) {
  cmd += L'"';
  size_t slashes = 0;

  for (auto ch : arg) {
    if (ch == L'\\') {
      ++slashes;
    } else {
      cmd.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
      cmd += ch;
      slashes = 0;
    }
  }

  cmd.append(2 * slashes, L'\\');
  cmd += L'"';
}

HANDLE open_child_file(fs::path const& path, DWORD access, DWORD disposition) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  auto h = ::CreateFileW(path.c_str(), access,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         &sa, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    check_windows("CreateFileW");
  }
  return h;
}

#else // !defined(_WIN32)

void check_posix(int error, char const* what) {
  if (error != 0) {
    throw std::system_error(error, std::generic_category(), what);
  }
}

#endif // defined(_WIN32)

} // namespace

class subprocess::impl {
 public:
  impl(fs::path const& prog, argument_list const& args)
      : cmdline_{display_command(prog, args)} {
    validate_arg(prog);

    for (auto const& arg : args) {
      validate_arg(arg);
    }

    std::cerr << "running: " << cmdline_ << '\n';

    auto const stdout_path = capture_.path() / "stdout";
    auto const stderr_path = capture_.path() / "stderr";

#ifdef _WIN32

    std::wstring command;
    append_quoted(command, prog.native());

    for (auto const& arg : args) {
      command += L' ';
      append_quoted(command, arg.native());
    }

    unique_handle in{open_child_file(L"NUL", GENERIC_READ, OPEN_EXISTING)};
    unique_handle out{open_child_file(stdout_path, GENERIC_WRITE, CREATE_NEW)};
    unique_handle err{open_child_file(stderr_path, GENERIC_WRITE, CREATE_NEW)};
    HANDLE handles[]{in.get(), out.get(), err.get()};
    SIZE_T bytes = 0;

    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (!bytes) {
      check_windows("InitializeProcThreadAttributeList (size)");
    }

    // operator new provides suitable alignment for the attribute list
    std::unique_ptr<void, decltype([](void* p) { ::operator delete(p); })>
        storage(::operator new(bytes));
    auto attrs = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
    if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &bytes)) {
      check_windows("InitializeProcThreadAttributeList");
    }

    auto cleanup =
        dwarfs::scope_exit([&] { ::DeleteProcThreadAttributeList(attrs); });

    if (!::UpdateProcThreadAttribute(attrs, 0,
                                     PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                     sizeof(handles), nullptr, nullptr)) {
      check_windows("UpdateProcThreadAttribute");
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in.get();
    si.StartupInfo.hStdOutput = out.get();
    si.StartupInfo.hStdError = err.get();
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};

    if (!::CreateProcessW(prog.c_str(), command.data(), nullptr, nullptr, TRUE,
                          CREATE_NEW_PROCESS_GROUP |
                              EXTENDED_STARTUPINFO_PRESENT,
                          nullptr, nullptr, &si.StartupInfo, &pi)) {
      check_windows("CreateProcessW");
    }

    process_.reset(pi.hProcess);

    pid_ = pi.dwProcessId;

    ::CloseHandle(pi.hThread);

#else // !defined(_WIN32)

    posix_spawn_file_actions_t actions;
    check_posix(::posix_spawn_file_actions_init(&actions),
                "spawn actions init");
    auto cleanup = dwarfs::scope_exit(
        [&] { ::posix_spawn_file_actions_destroy(&actions); });
    check_posix(::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                                   "/dev/null", O_RDONLY, 0),
                "redirect stdin");
    check_posix(::posix_spawn_file_actions_addopen(
                    &actions, STDOUT_FILENO, stdout_path.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL, 0600),
                "redirect stdout");
    check_posix(::posix_spawn_file_actions_addopen(
                    &actions, STDERR_FILENO, stderr_path.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL, 0600),
                "redirect stderr");

    std::vector<std::string> strings{prog.native()};
    for (auto const& arg : args) {
      strings.push_back(arg.native());
    }

    std::vector<char*> argv;
    for (auto& arg : strings) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    check_posix(::posix_spawn(&pid_, prog.c_str(), &actions, nullptr,
                              argv.data(), ::environ),
                "posix_spawn");

#endif // defined(_WIN32)
  }

  ~impl() {
    if (!status_) {
      std::cerr << "terminating unreaped subprocess: " << cmdline_ << '\n';
      try {
        kill();
        wait_native(std::nullopt);
      } catch (std::exception const& e) {
        std::cerr << "subprocess cleanup failed: " << e.what() << '\n';
      }
    }
  }

  bool wait_native(std::optional<std::chrono::milliseconds> timeout) {
    if (status_) {
      return true;
    }

#ifdef _WIN32

    DWORD const ms = timeout ? static_cast<DWORD>(std::clamp<int64_t>(
                                   timeout->count(), 0, INFINITE - 1))
                             : INFINITE;
    auto rv = ::WaitForSingleObject(process_.get(), ms);
    if (rv == WAIT_TIMEOUT) {
      return false;
    }
    if (rv != WAIT_OBJECT_0) {
      check_windows("WaitForSingleObject");
    }
    DWORD code;
    if (!::GetExitCodeProcess(process_.get(), &code)) {
      check_windows("GetExitCodeProcess");
    }
    status_ = static_cast<int32_t>(code);

#else // !defined(_WIN32)

    auto deadline = std::chrono::steady_clock::now() + timeout.value_or(0ms);

    for (;;) {
      int status;
      auto rv = ::waitpid(pid_, &status, timeout ? WNOHANG : 0);
      if (rv == pid_) {
        if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
          continue;
        }
        status_ = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
        break;
      }
      if (rv < 0 && errno != EINTR) {
        if (errno == ECHILD) {
          status_ = -1;
        }
        check_posix(errno, "waitpid");
      }
      if (timeout) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          return false;
        }
        std::this_thread::sleep_for(std::min(
            deadline - now,
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                10ms)));
      }
    }

#endif

    return true;
  }

  bool wait(std::optional<std::chrono::milliseconds> timeout) {
    if (!wait_native(timeout)) {
      return false;
    }

    if (!captured_) {
      out_ = read_file(capture_.path() / "stdout");
      err_ = read_file(capture_.path() / "stderr");
      captured_ = true;
    }

    return true;
  }

  bool interrupt() {
    if (wait_native(0ms)) {
      return true;
    }

#ifdef _WIN32
    return ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid_) != 0;
#else
    return ::kill(pid_, SIGINT) == 0 || errno == ESRCH;
#endif
  }

  void kill() {
    if (wait_native(0ms)) {
      return;
    }

#ifdef _WIN32
    if (!::TerminateProcess(process_.get(), 1) && !wait_native(0ms)) {
      check_windows("TerminateProcess");
    }
#else
    if (::kill(pid_, SIGKILL) != 0 && errno != ESRCH) {
      check_posix(errno, "kill(SIGKILL)");
    }
#endif
  }

  std::string const cmdline_;
  temporary_directory capture_{"dwarfs-subprocess"};
  std::optional<int> status_;
  std::string out_, err_;
  bool captured_{false};
#ifdef _WIN32
  unique_handle process_;
  DWORD pid_{};
#else
  pid_t pid_{};
#endif
};

subprocess::subprocess(fs::path const& prog, argument_list const& args)
    : impl_{std::make_unique<impl>(prog, args)} {}

subprocess::~subprocess() = default;

void subprocess::wait() { impl_->wait(std::nullopt); }

bool subprocess::wait_for(std::chrono::milliseconds timeout) {
  return impl_->wait(timeout);
}

bool subprocess::interrupt() { return impl_->interrupt(); }

void subprocess::terminate() {
  impl_->kill();
  wait();
}

std::string const& subprocess::cmdline() const { return impl_->cmdline_; }

std::string const& subprocess::out() const { return impl_->out_; }

std::string const& subprocess::err() const { return impl_->err_; }

int subprocess::exit_code() const { return impl_->status_.value(); }

std::optional<std::string> subprocess::check_result(result value) {
  auto& [out, err, ec] = value;
  if (ec != 0) {
    std::cerr << "stdout:\n"
              << out << "\nstderr:\n"
              << err << "\nexit code: " << ec << '\n';
    return std::nullopt;
  }
  return std::move(out);
}

} // namespace dwarfs::test

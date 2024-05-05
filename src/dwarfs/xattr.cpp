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
 */

#ifdef _WIN32

#include <vector>

#include <ntstatus.h>
#include <tchar.h>
#include <windows.h>
#include <winternl.h>

#include <boost/algorithm/string.hpp>

#else

#include <sys/xattr.h>

#endif

#include <folly/ScopeGuard.h>
#include <folly/String.h>

#include <fmt/format.h>

#include "dwarfs/xattr.h"

#ifdef _WIN32

extern "C" {

typedef struct _FILE_FULL_EA_INFORMATION {
  ULONG NextEntryOffset;
  UCHAR Flags;
  UCHAR EaNameLength;
  USHORT EaValueLength;
  CHAR EaName[1];
} FILE_FULL_EA_INFORMATION, *PFILE_FULL_EA_INFORMATION;

typedef struct _FILE_GET_EA_INFORMATION {
  ULONG NextEntryOffset;
  UCHAR EaNameLength;
  CHAR EaName[1];
} FILE_GET_EA_INFORMATION, *PFILE_GET_EA_INFORMATION;

NTSYSAPI NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(
    PCWSTR DosFileName, PUNICODE_STRING NtFileName, PWSTR* FilePart,
    PVOID RelativeName);

VOID NTAPI RtlFreeUnicodeString(PUNICODE_STRING UnicodeString);

NTSYSAPI NTSTATUS NTAPI NtQueryEaFile(HANDLE FileHandle,
                                      PIO_STATUS_BLOCK IoStatusBlock,
                                      PVOID Buffer, ULONG Length,
                                      BOOLEAN ReturnSingleEntry, PVOID EaList,
                                      ULONG EaListLength, PULONG EaIndex,
                                      BOOLEAN RestartScan);

NTSYSAPI NTSTATUS NTAPI NtSetEaFile(HANDLE FileHandle,
                                    PIO_STATUS_BLOCK IoStatusBlock,
                                    PVOID Buffer, ULONG Length);
}

#endif

namespace dwarfs {

namespace {

#ifdef _WIN32

constexpr size_t kMaxFullEaBufferSize{
    offsetof(FILE_FULL_EA_INFORMATION, EaName) + 256 + 65536};
constexpr size_t kMaxGetEaBufferSize{offsetof(FILE_GET_EA_INFORMATION, EaName) +
                                     256};

HANDLE open_file(std::filesystem::path const& path, bool writeable,
                 std::error_code& ec) {
  UNICODE_STRING nt_path;

  if (auto r = ::RtlDosPathNameToNtPathName_U_WithStatus(
          path.wstring().c_str(), &nt_path, nullptr, nullptr);
      r != 0) {
    ec = std::error_code(r, std::system_category());
    return nullptr;
  }

  SCOPE_EXIT { ::RtlFreeUnicodeString(&nt_path); };

  HANDLE fh;
  IO_STATUS_BLOCK iosb;
  OBJECT_ATTRIBUTES attr;
  ACCESS_MASK desired_access = FILE_READ_EA;

  if (writeable) {
    desired_access |= FILE_WRITE_EA;
  }

  InitializeObjectAttributes(&attr, &nt_path, 0, nullptr, nullptr);

  if (auto r = ::NtCreateFile(
          &fh, desired_access, &attr, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, 0,
          nullptr, 0);
      r != 0) {
    ec = std::error_code(::RtlNtStatusToDosError(r), std::system_category());
    return nullptr;
  }

  return fh;
}

#else

ssize_t portable_getxattr(const char* path, const char* name, void* value,
                          size_t size) {
#ifdef __APPLE__
  return ::getxattr(path, name, value, size, 0, 0);
#else
  return ::getxattr(path, name, value, size);
#endif
}

int portable_setxattr(const char* path, const char* name, const void* value,
                      size_t size, int flags) {
#ifdef __APPLE__
  return ::setxattr(path, name, value, size, 0, flags);
#else
  return ::setxattr(path, name, value, size, flags);
#endif
}

int portable_removexattr(const char* path, const char* name) {
#ifdef __APPLE__
  return ::removexattr(path, name, 0);
#else
  return ::removexattr(path, name);
#endif
}

ssize_t portable_listxattr(const char* path, char* list, size_t size) {
#ifdef __APPLE__
  return ::listxattr(path, list, size, 0);
#else
  return ::listxattr(path, list, size);
#endif
}

#endif

constexpr size_t kExtraSize{1024};

} // namespace

#ifdef _WIN32

std::string getxattr(std::filesystem::path const& path, std::string const& name,
                     std::error_code& ec) {
  ec.clear();

  if (name.size() > std::numeric_limits<UCHAR>::max()) {
    ec = std::error_code(ERROR_INVALID_EA_NAME, std::system_category());
    return {};
  }

  auto fh = open_file(path, false, ec);

  if (!fh) {
    // error code already set
    return {};
  }

  SCOPE_EXIT { ::NtClose(fh); };

  CHAR getea_buf[kMaxGetEaBufferSize];
  ULONG getea_len =
      FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + name.size() + 1;
  auto getea = reinterpret_cast<PFILE_GET_EA_INFORMATION>(getea_buf);

  getea->NextEntryOffset = 0;
  getea->EaNameLength = static_cast<UCHAR>(name.size());
  std::memcpy(getea->EaName, name.data(), name.size());
  getea->EaName[name.size()] = '\0';

  std::vector<CHAR> ea_buf(kMaxFullEaBufferSize);
  PFILE_FULL_EA_INFORMATION ea;
  IO_STATUS_BLOCK iosb;

  ea = reinterpret_cast<PFILE_FULL_EA_INFORMATION>(ea_buf.data());

  auto res = ::NtQueryEaFile(fh, &iosb, ea, ea_buf.size(), FALSE, getea,
                             getea_len, nullptr, FALSE);

  if (res != STATUS_SUCCESS) {
    ec = std::error_code(::RtlNtStatusToDosError(res), std::system_category());
    return {};
  }

  if (ea->EaValueLength == 0) {
    ec = std::error_code(ENODATA, std::generic_category());
    return {};
  }

  return {ea->EaName + ea->EaNameLength + 1, ea->EaValueLength};
}

void setxattr(std::filesystem::path const& path, std::string const& name,
              std::string_view value, std::error_code& ec) {
  // TODO
}

void removexattr(std::filesystem::path const& path, std::string const& name,
                 std::error_code& ec) {
  // TODO
}

std::vector<std::string>
listxattr(std::filesystem::path const& path, std::error_code& ec) {
  ec.clear();

  auto fh = open_file(path, false, ec);

  if (!fh) {
    // error code already set
    return {};
  }

  SCOPE_EXIT { ::NtClose(fh); };

  std::vector<std::string> names;
  std::vector<CHAR> ea_buf(kMaxFullEaBufferSize);
  BOOLEAN restart = TRUE;

  for (;;) {
    IO_STATUS_BLOCK iosb;

    auto ea = reinterpret_cast<PFILE_FULL_EA_INFORMATION>(ea_buf.data());
    auto res = ::NtQueryEaFile(fh, &iosb, ea, ea_buf.size(), FALSE, nullptr, 0,
                               nullptr, restart);

    if (res != STATUS_SUCCESS && res != STATUS_BUFFER_OVERFLOW) {
      ec =
          std::error_code(::RtlNtStatusToDosError(res), std::system_category());
      return {};
    }

    for (;;) {
      std::string name(ea->EaName, ea->EaNameLength);
      boost::algorithm::to_lower(name);
      names.push_back(std::move(name));

      if (ea->NextEntryOffset == 0) {
        break;
      }

      ea = reinterpret_cast<PFILE_FULL_EA_INFORMATION>(
          reinterpret_cast<PCHAR>(ea) + ea->NextEntryOffset);
    }

    if (res == STATUS_SUCCESS) {
      break;
    }

    restart = FALSE;
  }

  return names;
}

#else

std::string getxattr(std::filesystem::path const& path, std::string const& name,
                     std::error_code& ec) {
  ec.clear();

  auto cpath = path.c_str();
  auto cname = name.c_str();

  for (;;) {
    ssize_t size = portable_getxattr(cpath, cname, nullptr, 0);

    if (size < 0) {
      break;
    }

    std::string value;
    value.resize(size + kExtraSize);

    size = portable_getxattr(cpath, cname, value.data(), value.size());

    if (size >= 0) {
      value.resize(size);
      return value;
    }

    if (errno != ERANGE) {
      break;
    }
  }

  ec = std::error_code(errno, std::generic_category());

  return {};
}

void setxattr(std::filesystem::path const& path, std::string const& name,
              std::string_view value, std::error_code& ec) {
  ec.clear();

  if (portable_setxattr(path.c_str(), name.c_str(), value.data(), value.size(),
                        0) < 0) {
    ec = std::error_code(errno, std::generic_category());
  }
}

void removexattr(std::filesystem::path const& path, std::string const& name,
                 std::error_code& ec) {
  ec.clear();

  if (portable_removexattr(path.c_str(), name.c_str()) < 0) {
    ec = std::error_code(errno, std::generic_category());
  }
}

std::vector<std::string>
listxattr(std::filesystem::path const& path, std::error_code& ec) {
  ec.clear();

  auto cpath = path.c_str();

  for (;;) {
    ssize_t size = portable_listxattr(cpath, nullptr, 0);

    if (size < 0) {
      break;
    }

    std::string list;
    list.resize(size + kExtraSize);

    size = portable_listxattr(cpath, list.data(), list.size());

    if (size >= 0) {
      std::vector<std::string> names;

      if (size > 0) {
        // drop the last '\0'
        list.resize(size - 1);
        folly::split('\0', list, names);
      }

      return names;
    }

    if (errno != ERANGE) {
      break;
    }
  }

  ec = std::error_code(errno, std::generic_category());

  return {};
}

#endif

std::string
getxattr(std::filesystem::path const& path, std::string const& name) {
  std::error_code ec;

  auto value = getxattr(path, name, ec);

  if (ec) {
    throw std::system_error(ec);
  }

  return value;
}

void setxattr(std::filesystem::path const& path, std::string const& name,
              std::string_view value) {
  std::error_code ec;

  setxattr(path, name, value, ec);

  if (ec) {
    throw std::system_error(ec);
  }
}

void removexattr(std::filesystem::path const& path, std::string const& name) {
  std::error_code ec;

  removexattr(path, name, ec);

  if (ec) {
    throw std::system_error(ec);
  }
}

std::vector<std::string> listxattr(std::filesystem::path const& path) {
  std::error_code ec;

  auto names = listxattr(path, ec);

  if (ec) {
    throw std::system_error(ec);
  }

  return names;
}

} // namespace dwarfs

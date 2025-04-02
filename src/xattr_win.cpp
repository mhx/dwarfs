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

#include <vector>

#include <ntstatus.h>
#include <tchar.h>
#include <windows.h>
#include <winternl.h>

#include <boost/algorithm/string.hpp>

#include <dwarfs/scope_exit.h>
#include <dwarfs/xattr.h>

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

namespace dwarfs {

namespace {

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

  scope_exit free_nt_path{[&] { ::RtlFreeUnicodeString(&nt_path); }};

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

} // namespace

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

  scope_exit close_fh{[&] { ::NtClose(fh); }};

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

  auto res = ::NtQueryEaFile(fh, &iosb, ea, ea_buf.size(), TRUE, getea,
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

  scope_exit close_fh{[&] { ::NtClose(fh); }};

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

} // namespace dwarfs

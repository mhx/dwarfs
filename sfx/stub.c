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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/memfd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef DWARFS_SFX_STUB_USE_LZ4
#include <lz4.h>
#include <xxhash.h>
#else
#include "zstddeclib.c"
#endif

#include "nanoprintf.h"

#define TRAILER_SIZE 32
static uint8_t const trailer_magic[8] = {'S', 'Q', 'U', 'E',
                                         'E', 'Z', 'E', '!'};

static char const* const fexecve_test_flag = "--sfx-test-fexecve";

static uint64_t read_le64(uint8_t const b[8]) {
  return ((uint64_t)b[0]) | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16) |
         ((uint64_t)b[3] << 24) | ((uint64_t)b[4] << 32) |
         ((uint64_t)b[5] << 40) | ((uint64_t)b[6] << 48) |
         ((uint64_t)b[7] << 56);
}

struct trailer_info {
  uint64_t u_size;
  uint64_t c_size;
  uint64_t u_xxh64;
  off_t c_off;
};

static void fmterr(char const* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[1024];
  npf_vsnprintf(buf, sizeof(buf), fmt, ap);
  fputs(buf, stderr);
  va_end(ap);
}

static void msgerr(char const* msg) { fputs(msg, stderr); }

static int open_self_ro(void) {
  int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    perror("open /proc/self/exe");
  }
  return fd;
}

static int
read_trailer(uint8_t const* addr, uint64_t size, struct trailer_info* ti) {
  if (size < TRAILER_SIZE) {
    msgerr("wrapped: file too small\n");
    return -1;
  }

  uint8_t const* buf = addr + size - TRAILER_SIZE;

  if (memcmp(buf, trailer_magic, 8) != 0) {
    msgerr("wrapped: bad magic\n");
    return -1;
  }

  ti->u_size = read_le64(buf + 8);
  ti->c_size = read_le64(buf + 16);
  ti->u_xxh64 = read_le64(buf + 24);

  if (size < TRAILER_SIZE + ti->c_size) {
    msgerr("wrapped: inconsistent sizes\n");
    return -1;
  }

  ti->c_off = size - TRAILER_SIZE - (off_t)ti->c_size;

  return 0;
}

static int create_exec_memfd(size_t size) {
  // MFD_CLOEXEC breaks QEMU + binfmt_misc
  unsigned int flags = /*MFD_CLOEXEC |*/ MFD_ALLOW_SEALING | MFD_EXEC;
  int fd = memfd_create("wrapped", flags);
  if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
    flags &= ~MFD_EXEC;
    fd = memfd_create("wrapped", flags);
    if (fd < 0) {
      return -1;
    }
  }
  if (ftruncate(fd, size) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int reopen_readonly(int fd) {
  char path[64];
  npf_snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
  int ro_fd = open(path, O_RDONLY | O_CLOEXEC);
  if (ro_fd < 0) {
    return -1;
  }
  close(fd);
  return ro_fd;
}

static int dir_allows_exec(char const* dir) {
  struct statvfs sv;
  if (statvfs(dir, &sv) != 0) {
    return 0;
  }
  if ((sv.f_flag & ST_NOEXEC) != 0) {
    return 0;
  }
  return access(dir, W_OK | X_OK) == 0;
}

static int try_create_tmpfd_in_dir(char* template, size_t template_size,
                                   char const* dir, size_t size) {
  if (!dir_allows_exec(dir)) {
    return -1;
  }

  npf_snprintf(template, template_size, "%s/sfx-XXXXXX", dir);

  int fd = mkstemp(template);
  if (fd < 0) {
    return -1;
  }

  if (fchmod(fd, 0700) != 0) {
    unlink(template);
    close(fd);
    return -1;
  }

  if (ftruncate(fd, size) != 0) {
    unlink(template);
    close(fd);
    return -1;
  }

  return fd;
}

static int
create_exec_tmpfd(char* template, size_t template_size, size_t size) {
  char const* dirs[] = {"TMPDIR",   "XDG_RUNTIME_DIR", "/dev/shm", "/tmp",
                        "/usr/tmp", "/var/tmp",        NULL};

  for (char const** d = dirs; *d != NULL; ++d) {
    char const* dir = *d;

    if (*dir != '/') {
      dir = getenv(dir);
      if (dir == NULL || *dir == '\0' || *dir != '/') {
        continue;
      }
    }

    int fd = try_create_tmpfd_in_dir(template, template_size, dir, size);

    if (fd >= 0) {
      return fd;
    }
  }

  return -1;
}

static int add_seals_immutable_exec(int fd) {
  int seals = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
  if (fcntl(fd, F_ADD_SEALS, seals) != 0) {
    // older kernels may not support F_ADD_SEALS
    if (errno != EINVAL && errno != EPERM) {
      return -1;
    }
  }
  return 0;
}

static int decompress_wrapped(void const* src, size_t src_size, void* dst,
                              size_t dst_size) {
#ifdef DWARFS_SFX_STUB_USE_LZ4
  int rv = LZ4_decompress_safe(src, dst, src_size, dst_size);

  if (rv < 0) {
    msgerr("wrapped: lz4 error\n");
    return -1;
  }

  if ((size_t)rv != dst_size) {
    fmterr("wrapped: lz4 decompression size mismatch "
           "(got %d, expected %zu)\n",
           rv, dst_size);
    return -1;
  }
#else
  size_t rv = ZSTD_decompress(dst, dst_size, src, src_size);

  if (ZSTD_isError(rv)) {
    fmterr("wrapped: zstd error: %s\n", ZSTD_getErrorName(rv));
    return -1;
  }

  if (rv != dst_size) {
    fmterr("wrapped: zstd decompression size mismatch "
           "(got %zu, expected %zu)\n",
           rv, dst_size);
    return -1;
  }
#endif

  return 0;
}

static int
xxh64_verify(void const* addr, uint64_t expect_hash, uint64_t expect_size) {
  uint64_t got = XXH64(addr, expect_size, 0);

  if (got != expect_hash) {
    fmterr("wrapped: XXH64 mismatch (got 0x%016" PRIx64
           ", expected 0x%016" PRIx64 ")\n",
           got, expect_hash);
    return -1;
  }

  return 0;
}

static int extract_to_path_verified(char const* path, uint8_t const* addr,
                                    const struct trailer_info* ti) {
  int out = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0755);
  if (out < 0) {
    perror("open(output)");
    return -1;
  }

  if (ftruncate(out, ti->u_size) != 0) {
    perror("ftruncate(output)");
    close(out);
    return -1;
  }

  void* out_addr =
      mmap(NULL, ti->u_size, PROT_READ | PROT_WRITE, MAP_SHARED, out, 0);

  if (out_addr == MAP_FAILED) {
    perror("mmap(output)");
    close(out);
    return -1;
  }

  int rc =
      decompress_wrapped(addr + ti->c_off, ti->c_size, out_addr, ti->u_size);
  if (rc == 0) {
    rc = xxh64_verify(out_addr, ti->u_xxh64, ti->u_size);
    if (rc == 0) {
      (void)fchmod(out, 0755);
    }
  }

  if (munmap(out_addr, ti->u_size) != 0) {
    perror("munmap(output)");
    rc = -1;
  }

  close(out);

  return rc;
}

static void print_extract_hint(char const* prog_name) {
  fmterr("\nYou can extract the wrapped binary using:\n\n"
         "  %s --extract-wrapped-binary <output_path>\n\n",
         prog_name);
}

static int test_fexecve(uint8_t const* stub_addr, off_t stub_size, char** argv,
                        char** envp) {
  int fd = create_exec_memfd(stub_size);
  if (fd < 0) {
    return 0;
  }

  void* test_addr =
      mmap(NULL, stub_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (test_addr == MAP_FAILED) {
    close(fd);
    return 0;
  }

  memcpy(test_addr, stub_addr, stub_size);

  if (munmap(test_addr, stub_size) != 0) {
    close(fd);
    return 0;
  }

  if (add_seals_immutable_exec(fd) != 0) {
    close(fd);
    return 0;
  }

  fd = reopen_readonly(fd);

  if (fd < 0) {
    return 0;
  }

  pid_t pid = fork();

  if (pid < 0) {
    close(fd);
    return 0;
  }

  if (pid == 0) {
    char const* av[] = {argv[0], fexecve_test_flag, NULL};
    lseek(fd, 0, SEEK_SET);
    fexecve(fd, (char* const*)av, envp);
    _exit(1);
  }

  close(fd);

  int st = 0;
  waitpid(pid, &st, 0);

  return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 1 : 0;
}

int main(int argc, char** argv, char** envp) {
  if (argc == 2 && strcmp(argv[1], fexecve_test_flag) == 0) {
    // exit immediately with success to indicate fexecve support
    return 0;
  }

  int self_fd = open_self_ro();
  if (self_fd < 0) {
    return 1;
  }

  struct stat self_st;
  if (fstat(self_fd, &self_st) != 0) {
    perror("fstat /proc/self/exe");
    close(self_fd);
    return 1;
  }

  uint8_t const* self_addr =
      mmap(NULL, self_st.st_size, PROT_READ, MAP_PRIVATE, self_fd, 0);

  close(self_fd); // safe to close now

  if (self_addr == MAP_FAILED) {
    perror("mmap /proc/self/exe");
    return 1;
  }

  struct trailer_info ti;
  if (read_trailer(self_addr, self_st.st_size, &ti) != 0) {
    munmap((void*)self_addr, self_st.st_size);
    return 1;
  }

  if (argc == 3 && strcmp(argv[1], "--extract-wrapped-binary") == 0) {
    char const* out_path = argv[2];
    int rc = extract_to_path_verified(out_path, self_addr, &ti);
    munmap((void*)self_addr, self_st.st_size);
    return rc == 0 ? 0 : 1;
  }

  int can_use_fexecve = test_fexecve(self_addr, ti.c_off, argv, envp);

  char template[1024];
  char const* tmpfile = NULL;
  int app_fd = -1;

  if (can_use_fexecve) {
    app_fd = create_exec_memfd(ti.u_size);
  } else {
    tmpfile = template;
    app_fd = create_exec_tmpfd(template, sizeof(template), ti.u_size);
  }

  if (app_fd < 0) {
    munmap((void*)self_addr, self_st.st_size);
    msgerr("could not create temporary executable file\n");
    print_extract_hint(argv[0]);
    return 1;
  }

  void* app_addr =
      mmap(NULL, ti.u_size, PROT_READ | PROT_WRITE, MAP_SHARED, app_fd, 0);

  if (app_addr == MAP_FAILED) {
    perror("mmap");
    munmap((void*)self_addr, self_st.st_size);
    goto on_error_hint;
  }

  int decompress_rv =
      decompress_wrapped(self_addr + ti.c_off, ti.c_size, app_addr, ti.u_size);

  if (munmap((void*)self_addr, self_st.st_size) != 0) {
    perror("munmap /proc/self/exe");
  }

  if (munmap(app_addr, ti.u_size) != 0) {
    perror("munmap");
  }

  if (decompress_rv != 0) {
    goto on_error;
  }

  if (fchmod(app_fd, 0755) != 0) {
    perror("fchmod");
    // We'll still try to execute the file, but it may fail.
  }

  if (add_seals_immutable_exec(app_fd) != 0) {
    perror("F_ADD_SEALS");
    goto on_error_hint;
  }

  app_addr = mmap(NULL, ti.u_size, PROT_READ, MAP_PRIVATE, app_fd, 0);

  if (app_addr == MAP_FAILED) {
    perror("mmap (read-only)");
    goto on_error_hint;
  }

  int verify_rv = xxh64_verify(app_addr, ti.u_xxh64, ti.u_size);

  if (munmap(app_addr, ti.u_size) != 0) {
    perror("munmap");
  }

  if (verify_rv != 0) {
    goto on_error;
  }

  if (can_use_fexecve) {
    app_fd = reopen_readonly(app_fd);

    if (app_fd < 0) {
      perror("open(readonly)");
      goto on_error_hint;
    }

    lseek(app_fd, 0, SEEK_SET);
    fexecve(app_fd, argv, envp);

    // ===== fexecve only returns on error =====

    perror("fexecve");
    print_extract_hint(argv[0]);

    close(app_fd);

    return 127;
  }

  // use execve on tmpfile

  close(app_fd);
  app_fd = -1;

  int px[2];

  if (pipe2(px, O_CLOEXEC) != 0) {
    goto on_error_hint;
  }

  pid_t pid = fork();

  if (pid < 0) {
    perror("fork");
    goto on_error_hint;
  }

  if (pid == 0) {
    pid_t pid2 = fork();

    if (pid2 < 0) {
      _exit(1);
    }

    if (pid2 == 0) {
      close(px[1]); // keep only read end
      char dummy;

      // we'll never read anything here, just wait for EOF to signal that
      // the parent has either succeeded or failed the `execve`
      (void)read(px[0], &dummy, 1);
      close(px[0]);

      // in any case, now is the time to clean up
      unlink(tmpfile);
    }

    _exit(0);
  }

  int st = 0;

  waitpid(pid, &st, 0);

  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    msgerr("could not fork janitor process\n");
    goto on_error_hint;
  }

  close(px[0]); // keep write end

  execve(tmpfile, argv, envp);

  perror("execve(temp)");
  close(px[1]); // close write end explicitly

  return 127;

on_error_hint:
  print_extract_hint(argv[0]);

on_error:
  if (tmpfile != NULL) {
    unlink(tmpfile);
  }

  if (app_fd >= 0) {
    close(app_fd);
  }

  return 1;
}

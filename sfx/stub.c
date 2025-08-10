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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef DWARFS_SFX_STUB_USE_LZ4
#include <lz4.h>
#include <xxhash.h>
#else
#include "zstddeclib.c"
#endif

#define TRAILER_SIZE 32
static uint8_t const trailer_magic[8] = {'S', 'Q', 'U', 'E',
                                         'E', 'Z', 'E', '!'};

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
    fprintf(stderr, "wrapped: file too small\n");
    return -1;
  }

  uint8_t const* buf = addr + size - TRAILER_SIZE;

  if (memcmp(buf, trailer_magic, 8) != 0) {
    fprintf(stderr, "wrapped: bad magic\n");
    return -1;
  }

  ti->u_size = read_le64(buf + 8);
  ti->c_size = read_le64(buf + 16);
  ti->u_xxh64 = read_le64(buf + 24);

  if (size < TRAILER_SIZE + ti->c_size) {
    fprintf(stderr, "wrapped: inconsistent sizes\n");
    return -1;
  }

  ti->c_off = size - TRAILER_SIZE - (off_t)ti->c_size;

  return 0;
}

static int create_exec_memfd(void) {
  unsigned int flags = MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_EXEC;
  int fd = memfd_create("wrapped", flags);
  if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
    flags &= ~MFD_EXEC;
    fd = memfd_create("wrapped", flags);
    if (fd < 0) {
      return -1;
    }
  }
  return fd;
}

static int add_seals_immutable_exec(int fd) {
  int seals = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
  if (fcntl(fd, F_ADD_SEALS, seals) != 0) {
    perror("F_ADD_SEALS");
    return -1;
  }
  return 0;
}

static int decompress_wrapped(void const* src, size_t src_size, void* dst,
                              size_t dst_size) {
#ifdef DWARFS_SFX_STUB_USE_LZ4
  int rv = LZ4_decompress_safe(src, dst, src_size, dst_size);

  if (rv < 0) {
    fprintf(stderr, "wrapped: lz4 error\n");
    return -1;
  }

  if ((size_t)rv != dst_size) {
    fprintf(stderr,
            "wrapped: lz4 decompression size mismatch "
            "(got %d, expected %zu)\n",
            rv, dst_size);
    return -1;
  }
#else
  size_t rv = ZSTD_decompress(dst, dst_size, src, src_size);

  if (ZSTD_isError(rv)) {
    fprintf(stderr, "wrapped: zstd error: %s\n", ZSTD_getErrorName(rv));
    return -1;
  }

  if (rv != dst_size) {
    fprintf(stderr,
            "wrapped: zstd decompression size mismatch "
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
    fprintf(stderr,
            "wrapped: XXH64 mismatch (got 0x%016" PRIx64
            ", expect 0x%016" PRIx64 ")\n",
            got, expect_hash);
    return -1;
  }

  return 0;
}

static int extract_to_path_verified(char const* path, uint8_t const* addr,
                                    const struct trailer_info* ti) {
  int out = open(path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0755);
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
  fprintf(stderr,
          "\nYou can extract the wrapped binary using:\n\n"
          "  %s --extract-wrapped-binary <output_path>\n\n",
          prog_name);
}

int main(int argc, char** argv, char** envp) {
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

  int app_fd = create_exec_memfd();
  if (app_fd < 0) {
    if (errno == ENOSYS) {
      fprintf(
          stderr,
          "memfd_create() not supported, are you running on an old kernel?\n");
      print_extract_hint(argv[0]);
    } else {
      perror("memfd_create");
    }
    munmap((void*)self_addr, self_st.st_size);
    return 1;
  }

  if (ftruncate(app_fd, ti.u_size) != 0) {
    perror("ftruncate");
    munmap((void*)self_addr, self_st.st_size);
    close(app_fd);
    return 1;
  }

  void* app_addr =
      mmap(NULL, ti.u_size, PROT_READ | PROT_WRITE, MAP_SHARED, app_fd, 0);

  if (app_addr == MAP_FAILED) {
    perror("mmap");
    munmap((void*)self_addr, self_st.st_size);
    close(app_fd);
    return 1;
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
    close(app_fd);
    return 1;
  }

  if (fchmod(app_fd, 0755) != 0) {
    perror("fchmod");
    // We'll still try to execute the file, but it may fail.
  }

  if (add_seals_immutable_exec(app_fd) != 0) {
    print_extract_hint(argv[0]);
    close(app_fd);
    return 1;
  }

  app_addr = mmap(NULL, ti.u_size, PROT_READ, MAP_PRIVATE, app_fd, 0);

  if (app_addr == MAP_FAILED) {
    perror("mmap (read-only)");
    print_extract_hint(argv[0]);
    close(app_fd);
    return 1;
  }

  int verify_rv = xxh64_verify(app_addr, ti.u_xxh64, ti.u_size);

  if (munmap(app_addr, ti.u_size) != 0) {
    perror("munmap");
  }

  if (verify_rv != 0) {
    close(app_fd);
    return 1;
  }

  lseek(app_fd, 0, SEEK_SET);
  fexecve(app_fd, argv, envp);

  if (errno == ENOEXEC) {
    fprintf(stderr, "fexecve() failed with ENOEXEC, are you using QEMU?\n");
    print_extract_hint(argv[0]);
  } else {
    perror("fexecve");
  }

  close(app_fd);
  return 127;
}

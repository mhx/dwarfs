# Change Log

## Version 0.14.1 - 2025-10-24

- (fix) The new Windows code from the v0.14.0 release that implemented
  `lstat()`-like functionality to get file status information did not
  work correctly for reparse points that were *not* symbolic links (i.e.
  "real" symlinks or junctions). When dealing with such reparse points,
  the code would incorrectly determine the allocated size of the file
  to be zero, since it tried to determine that size from the reparse
  point and not from the target. This caused the progress percentage
  to be calculated incorrectly, since *much* more data was processed
  than was expected, leading to progress percentages well over 100%.
  Thankfully, @lunighty not only reported this issue, but also provided
  significant context by mentioning that this was on a Windows Server
  NTFS volume with deduplication enabled. Fortunately, this was also
  reproducible on "regular" Windows using a mounted WIM image. The fix
  will now correctly handle reparse points by re-opening the handle
  without `FILE_FLAG_OPEN_REPARSE_POINT` in order to open the target.
  This bug only affected Windows builds of `mkdwarfs`, and only when
  actually used on filesystems with reparse points that were not symlinks.
  Even then, the resulting DwarFS image would still be correct, except
  for a way too small `total_allocated_fs_size` metadata entry. However,
  this entry is purely informational. Huge thanks to @lunighty for both
  reporting and helping to debug this issue! Fixes github #307.

- (fix) A bunch of changes were made to make the static (Linux) release
  binaries work on FreeBSD with the Linux emulation layer. While you *can*
  easily build DwarFS on FreeBSD natively, the static binaries might be
  bundled in Linux application images that you may want to run on FreeBSD
  as well. This required patching `musl` to fall back to `FUTEX_WAKE` if
  `FUTEX_REQUEUE` is not supported (which is the case on FreeBSD's Linux
  emulation); otherwise, the binaries would regularly hang when trying to
  shut down thread pools. Furthermore, the SFX stub did not work with the
  emulation layer due to lack of support for `fexecve`; so it now uses a
  fallback of temporary file + `execve` in case `create_memfd` and/or
  `fexecve` are not supported. This fallback is very similar to the one
  that was already in use for old Linux kernels, only that cleanup of the
  temporary file is now done using a child "janitor" process as soon as
  the `execve` call succeeds. Lastly, all static binaries are now branded
  as using the Linux ABI rather than the generic "System V" in the ELF
  header, which makes it unnecessary to explicitly `brandelf` the binaries
  on FreeBSD.

- (fix) The `pcmaudio` categorizer had two minor issues discovered by
  @lunighty when compressing a large number of WAV files. One was reporting
  an `unsupported format: 3/0` or `unsupported format: 65,534/3` warning,
  which isn't very useful for the end user. These format codes correspond
  to IEEE floating point formats, which are indeed unsupported. However,
  the format appears to be quite common, so the warning has been downgraded
  to an info message that explicitly mentions the floating point format.
  The second issue was an unexpected `fmt` chunk size of 20 bytes, which
  caused the file to be rejected as a PCM audio file (meaning it was added
  using a generic compressor instead of FLAC). It turns out that these
  non-conforming `fmt` chunks are also quite common in practice, so the
  code has been changed to accept the non-conforming file, but also logging
  an info message mentioning the non-conformance. Fixes github #309.

- (fix) The `test_helpers` library used for unit tests was not explicitly
  listing `xxhash` as a dependency, which caused linker errors on macOS.
  These unfortunately only showed up in the Homebrew CI. The dependency has
  been added.

- (fix) Instead of making the FUSE drivers fail hard when seeing the options
  that were removed in v0.14.0, they now just log a warning and ignore them.
  The options may still be fully removed in a future release. Fixes github
  #303.

- (feat) Added shell completion for `dwarfsck` and `dwarfsextract`. (Thanks
  to Ahmad Khalifa for the contribution.)

- (feat) Added sample desktop unmount handlers. (Thanks to Ahmad Khalifa
  for the contribution.)

- (build) Bumped `libarchive`, `libressl`, and `libdwarf` versions in the
  static build pipeline.

- (test) Added test for `file_stat` on symbolic links containing multibyte
  characters on Windows.

- (test) Added test for obsolete FUSE driver options being ignored and
  producing a warning.

- (test) Added test for the FUSE driver's `imagesize` option.

- (test) Added more tests for the `--auto-mountpoint` option in the FUSE
  driver.

## Version 0.14.0 - 2025-10-21

- (fix) Leading dots in `--input-list` file paths were incorrectly treated
  as literal directory names instead of being expanded. This has been fixed.
  Fixes github #292.

- (fix) The SPDX license identifier in GPL-licensed source files was
  incorrectly specified as `GPL-3.0-only` instead of `GPL-3.0-or-later`.
  This has been corrected. Fixes github #275.

- (fix) Fixed an off-by-one error when recovering `self_index` fields in
  metadata, which could cause the sentinel directory to have a non-zero
  `self_entry`. While harmless by itself (since that entry is never actually
  used), this would cause the metadata consistency check to fail. The fix
  covers three aspects: correcting the off-by-one error; ensuring the
  `self_entry` recovery code does not run for the sentinel directory; and
  changing the metadata consistency check to only warn about a non-zero
  `self_entry` rather than fail. Running `mkdwarfs` with `--rebuild-metadata`
  will also reset a non-zero sentinel `self_entry` to zero.

- (fix) Fixed the implementation of the `read` operation in the FUSE driver
  to send positive error code values to libfuse. This was likely never
  triggered in practice, but in cases where parts of the filesystem image
  vanish while being accessed (which previously caused SIGBUS crashes),
  libfuse would not understand the negative error codes.

- (fix) Moved the FUSE driver binaries from `sbin` to `bin` and kept only the
  `mount.dwarfs`/`mount.dwarfs2` symlinks in `sbin`. This better aligns with
  user expectations, other FUSE drivers, and the fact that the man pages are
  installed in section 1. (Thanks to Ahmad Khalifa for the fix.)

- (fix) The `dwarfs2` binary was broken in builds using shared libraries.
  (Thanks to Ahmad Khalifa for the fix.)

- (fix) When setting CPU thread affinity for worker group threads via
  `DWARFS_WORKER_GROUP_AFFINITY`, the code did not `CPU_ZERO` the `cpu_set_t`
  structure before setting individual CPUs. This could pin threads to random
  CPUs in addition to the requested ones.

- (fix) The FITS categorizer would scan entire files for the end-of-header
  marker if their size was a multiple of 2880 bytes, causing significant
  slowdowns on large non-FITS files. Additional checks now ensure scanning
  only continues if the data truly looks like a standards-compliant FITS
  header.

- (fix) GCC caught a potential null-pointer dereference on error when opening
  a file in `mkdwarfs`. This has been fixed.

- (fix) Numerous fixes for 32-bit architectures, mostly related to integer
  overflows with file sizes larger than 4 GiB.

- (fix) Another off-by-one error caused the first regular file inode to be
  excluded from the file-size cache. This would be hard to notice unless that
  file was highly fragmented. The cache will be fixed when rebuilding the
  metadata.

- (fix) The FUSE driver’s `enable_nlink` option is now the default behavior
  and cannot be disabled. The previous optimization skipped building a table
  of hardlink counts, which produced inherently incorrect file status
  information (hardlinked files share an inode, so reporting a link count
  of 1 is wrong). The hardlink table is now stored in the metadata by default;
  if there are no hardlinks, it consumes no space. You can still omit the
  hardlink table with `--no-hardlink-table`, at the cost of building it
  on-the-fly when the filesystem image is loaded (typically fast — e.g.,
  ~300 ms for 14 million files).

- (fix) Fixed a typo in `dwarfs-format.md`. (Thanks to Dennis Brakhane for
  spotting this and sending a PR.)

- (feature) New I/O layer abstraction that supports “classic” `mmap`-based
  file access, granular `mmap`-based access on 32-bit systems, and fully
  `mmap`-less access if desired. This applies to all DwarFS tools. By default,
  tools use the most efficient method—memory-mapping whole files on 64-bit
  systems and mapping file segments on 32-bit systems (to conserve address
  space). This can be controlled via the new `DWARFS_IOLAYER_OPTS` environment
  variable described in `dwarfs-env(7)`.

- (feature) Full support for sparse files. `mkdwarfs` now detects and
  efficiently processes sparse files, skipping holes where possible and
  preserving them in the filesystem image. This is supported on all platforms.
  The FUSE driver implements `lseek()` where supported by the FUSE library
  (currently Linux and FreeBSD); Windows and macOS fall back to showing files
  as non-sparse. `dwarfsextract` extracts sparse files as such and preserves
  sparse representations when extracting to archive formats that support them
  (e.g., tar). **Note:** Sparse file support is not backwards compatible;
  images containing sparse files cannot be processed by DwarFS versions prior
  to 0.14.0. By default, `mkdwarfs` enables sparse file support if it detects
  sparse input. Use `--no-sparse-files` to disable it and ensure compatibility
  with older versions.

- (feature) Support for subsecond timestamp resolution. The default remains
  one second, but finer resolutions (down to nanoseconds) can be specified
  with `--time-resolution`. `mkdwarfs` will warn if the requested resolution
  is finer than the native filesystem resolution. This is fully backwards
  compatible: older DwarFS versions will handle such images but ignore the
  subsecond parts.

- (feature) Desktop integration for Linux. A new `--auto-mountpoint` option
  automatically creates or selects a mount-point directory, making it easier
  to mount DwarFS images from file managers. Desktop files and MIME type
  definitions are now installed to enable double-click mounting of `.dwarfs`
  files. (Thanks to Ahmad Khalifa for the implementation.)

- (feature) Shell completion for `mkdwarfs` (bash and zsh). (Thanks to Ahmad
  Khalifa for the contribution.)

- (feature) Improved error handling when DwarFS tools encounter `SIGBUS`
  (usually caused by accessing memory-mapped files on unreliable or faulty
  storage like network shares or flaky USB drives). When `SIGBUS` is caught,
  tools now print an error suggesting switching from `mmap`- to `read`-based
  I/O via `DWARFS_IOLAYER_OPTS`.

- (feature) `dwarfsck` now checks metadata consistency by default (unless
  `--no-check` is given), improving detection of filesystem image corruption.

- (feature) If sparse files are supported by the FUSE library, the FUSE driver
  exposes new options `cache_sparse` and `no_cache_sparse` to control whether
  sparse files should be cached in the kernel page cache. See `dwarfs(1)` for
  details.

- (feature) The JSON output from `dwarfsck` now contains a complete raw
  metadata dump when the detail level includes `metadata_full_dump`.

- (feature) `dwarfsck` no longer artificially limits string sizes when dumping
  metadata. (Thanks to Dennis Brakhane for the contribution.)

- (feature) Accelerated search for the start of a DwarFS image in files with
  custom headers; the new code is about four times faster, scanning at more
  than 6 GiB/s on a modern CPU.

- (feature) The cache size can now be configured for `dwarfsck`, useful with
  the `--checksum` option.

- (feature) Both `dwarfsck` and `dwarfsextract` now limit the amount of data
  requested from the filesystem image at once to avoid exhausting memory (and
  virtual address space on 32-bit systems).

- (feature) Improved self-extracting binary stub with better compatibility for
  `qemu`, `binfmt_misc`, and old kernels. The stub now works on Linux kernels
  as old as 2.6.21 (and possibly older), and it now uses `nanoprintf` to
  further reduce binary size.

- (feature) The *accepted* minor version for the DwarFS image format has been
  incremented. Release v0.16.0 will also increment the *written* minor version.
  This means images produced with v0.16.0 will not be readable by DwarFS tools
  prior to v0.14.0. See the “Features” section in `dwarfs-format(7)` for
  details.

- (feature) The FUSE driver will now show the name of the mounted file system
  image in the mount point listing (e.g., in `df` or `mount` output) on Linux,
  FreeBSD and macOS, as well as the filesystem subtype (`dwarfs`) on Linux and
  FreeBSD.

- (compat) The `(no_)cache_image` option has been removed from the FUSE driver.

- (docs) Added documentation on manual FSST decoding to `dwarfs-format.md`.
  (Thanks to Dennis Brakhane for the PR.)

- (docs) Several cleanups and additions to `dwarfs-format.md`, including a
  glossary of terms, clarification of blocks vs. sections, and descriptions
  of compatibility handling via features, plus details on the representation
  of sparse files and hardlinks.

- (docs) New manual page `dwarfs-env(7)` documenting DwarFS-specific
  environment variables.

- (build) Removed the dependency on Boost.Iostreams and the hard dependency
  on Boost.System.

- (build) Removed the hard dependency on the `date` library, which caused
  build issues on distributions that no longer bundle it (e.g., SUSE).

- (build) The build system now creates symbolic mount links at install time
  rather than in the build directory.

- (test) Significantly improved test coverage.

## Version 0.13.0 - 2025-08-29

- (fix) The linker configuration for the release binaries was broken. The
  symptom of this was that *very* occasionally, tests would fail in the CI
  with `std::terminate` being called after the exception handling code failed
  to unwind the stack. The root cause was that in clang builds, code from
  `libunwind` and code from `libstdc++` was rather arbitrarily mixed, which,
  depending the order in which individual threads were scheduled in the unit
  tests, could lead to stack unwinding working flawlessly, or not at all.
  This was one of the hardest bugs to track down this year, fortunately the
  fix was quite simple. In any case, there is potential for this issue being
  present in the previously released binaries, although there haven't been
  any reports.

- (fix) Made section index discovery more robust. Fixes github #264.

- (fix) A recent kernel change (https://lkml.org/lkml/2025/5/5/2868) caused
  the `tools_test` to fail on Linux 6.14 and later. This has been fixed by
  accepting both `EPERM` and `ENOSYS` as valid error codes for `link()` calls.

- (feature) Support for FreeBSD. Everything that works on Linux should
  also work on FreeBSD. There are no static binaries, but the build should
  work out of the box once all dependencies are installed.

- (feature) Support for big-endian architectures. This is still experimental,
  even though all unit tests pass with QEMU, and the benchmark suite runs
  fine on real hardware. This currently requires forked versions of `folly`
  and `fsst`. The changes are small and the pull requests will hopefully
  be merged upstream soon.

- (feature) Experimental support for 32-bit architectures. While DwarFS
  should mostly "just work" on 32-bit when using small images (a few hundred
  megabytes), the limited address space is a problem for the extensive use
  of memory-mapped files inside DwarFS. There will be changes to limit the
  use of `mmap` in the future (mainly due to other issues), which should help
  32-bit compatibility as a side-effect. Fixes github #268.

- (feature) Support for a wide range of CPU architectures. The static
  binary releases (including the universal binaries) are now available for
  `x86_64`, `aarch64`, `i386`, `arm`, `ppc64`, `ppc64le`, `riscv64`, `s390x`,
  and `loongarch64`. Trying to build the new release binaries has uncovered a
  few bugs in `clang`, `binutils`, `mold`, and `upx`, not all of which have
  been fixed yet. As a result, the binaries use slightly different toolchains
  and configurations depending on the architecture. Fixes github #266, #268.

- (feature) Custom self-extracting binary stub for universal binaries. This
  aims for simplicity and portability and should work on most Linux systems.
  This is used if `upx` support for an architecture is not available, or if
  the binaries extract much faster than the `upx` compressed version. The
  stub also supports a `--extract-wrapped-binary <file>` option to extract
  the embedded binary.

- (feature) The category metadata for categorized blocks is now stored in
  the metadata block by default. This allows re-compressing the blocks with
  a metadata-dependent algorithm (e.g. FLAC) even if they were previously
  compressed using a metadata-independent algorithm. This can be disabled
  using the `--no-category-metadata` option. See the `mkdwarfs` man page
  for more details.

- (feature) The `--no-category-names` and `--no-category-metadata` options
  can be used to reduce the size of the metadata. However, this will make
  it impossible to use metadata-dependent compression algorithms (e.g. FLAC),
  or even select category-specific compression, when recompressing the image.

- (feature) Metadata rebuilding is now supported in `mkdwarfs` using the
  `--rebuild-metadata` option. Previously, the metadata could only be
  recompressed, but it was impossible to change it. With the new option,
  it is now possible to change metadata packing and apply operations like
  `--set-owner`, `--set-group`, `--set-time`, `--time-resolution`, `--chmod`,
  or `--no-create-timestamp`. Note that these are potentially lossy operations
  that may be irreversible. By default, the history of metadata rebuilds is
  tracked in the metadata itself, but this can be disabled using
  `--no-metadata-version-history`.

- (feature) In addition to metadata rebuilding, it is now also possible to
  change the block size of an existing image using the `--change-block-size`
  option. This implies `--rebuild-metadata` and `--recompress=all`. This can
  be useful for tuning the performance of an existing image without having
  to re-create it from scratch.

- (feature) `mkdwarfs` now shows its current memory usage while running.
  Note that `-L`/`--memory-limit` still only limits the memory used for
  the block queue, not the overall memory usage. Fixing this is on the
  roadmap, there's no need to file an issue.

- (feature) `dwarfsextract` has new options to control the output format:
  `--format-options` and `--format-filters`. There is also `--format=auto`
  to automatically "guess" the format and filters based on the output file
  name. (Thanks to @oxalica for the pull request.)

- (feature) `dwarfsck` has a new `frozen_details` detail level that will
  show the `frozen_analysis` content ordered by memory location instead of
  memory usage and also shows the address range of each section.

- (feature) Replaced `folly`’s `EvictingCacheMap` with a simple in-repo LRU
  cache implementation. Reduces external dependencies and binary size while
  not sacrificing performance.

- (feature) The `pxattr` utility now supports all extended attribute
  operations, including `setxattr` and `removexattr`, on Windows. Also,
  error handling/reporting for extended attributes on Windows has been
  improved.

- (docs) Updated `dwarfs-format.md` with more info on section types,
  compression metadata, and Frozen2 binary metadata layout. (Thanks to
  @oxalica for asking the right questions, reporting bugs, and ultimately
  releasing a Rust library to read/write DwarFS images.)

- (docs) Added notable users section to README. (Thanks to Vitaly Zdanevich
  for the PRs.)

- (docs) Updated `mkdwarfs` docs with more info on worker threads and
  requirements for bit-identical images.

- (docs) Major README overhaul. Added quick start section, added more
  links, fixed typos and wording.

## Version 0.12.4 - 2025-05-14

- (fix) Segfault on `bad_compression_ratio_error`. When recompressing a
  filesystem where some blocks cannot be compressed using the selected
  algorithm because of a `bad_compression_ratio_error`, the resulting
  `block` was left empty after the refactoring done in 06f8728cc.

- (fix) Add history unless `--no-history` is given when rewriting a file
  system image.

- (fix) Allow dumping `frozen_layout` w/o `frozen_analysis` in `dwarfsck`.

- (fix) Logging timestamps should show local time.

- (fix) Workaround a weird MSVC bug.

- (fix) Remove useless cast causing compiler warning.

- (feat) More complete breakdown of metadata in `dwarfsck`.

- (feat) Add `schema_raw_dump` flag to `dwarfsck --detail`.

- (build) Switch static build to libressl on Windows.

- (build) Update static build libraries.

- (build) Update folly/fbthrift/fsst.

- (refactor) Use `unordered_set::contains` to simplify check.

- (refactor) Introduce and use `safe_localtime()` to prevent issues
  with `fmt` deprecating `fmt::localtime`.

- (test) Speed up a few slow tests on Windows.

## Version 0.12.3 - 2025-04-21

- (fix) Automatic image offset detection (for images using a custom
  header) did not work correctly if the header contained a string that
  would be identified as the start of a v1 section header (these were
  only used before dwarfs-0.3.0). If there was either `"DWARFS\x02\x00"`
  or `"DWARFS\x02\x01"` in the header, offset detection would fail.
  The check has been modified to peek further into the data and ensure
  this *really* is a v1 section header, and also checking if the next
  section header position can be derived from the length field. It is
  still possible to construct a file system image where offset detection
  will ultimately fail, but it is much less likely with the change.

- (build) The build process for the release binaries has been further
  tweaked to reduce binary size. The `dwarfs-fuse-extract` binary now
  again supports extracting files by pattern; I didn't realize that
  this was actually a widely used feature before dropping it in the
  last release. `dwarfs-universal` is now linked against LibreSSL's
  `libcrypto` instead of OpenSSL's. This significantly reduces the size
  at the expense slightly slower cryptographic hash functions. However,
  this will likely *only* be perceivable when using `--tool=dwarfsck`
  with either `--check-integrity` or `--checksum`. The binaries from the
  release tarballs are still linked against `libcrypto` from OpenSSL.

## Version 0.12.2 - 2025-04-16

- (fix) A performance regression introduced in v0.12.0 was fixed. This
  caused pcmaudio compression in `mkdwarfs` to take more than twice as
  long as in the previous releases.

- (build) A few small refactoring changes to further reduce the size of
  the `fuse-extract` binary. In particular, the performance monitor and
  the history feature are now fully removed. Also, the functionality to
  extract in different archive formats as well as to extract only files
  matching a pattern have been removed, so the image can only be fully
  extracted to disk.

## Version 0.12.1 - 2025-04-10

- (fix) Attempt to fix linking issue in Homebrew build.

- (feature) Added `--memory-limit=auto` to `mkdwarfs` to use a more
  reasonably (hopefully) default for the block queue. The old default
  of 1 GiB was quite arbitrary and definitely not suitable for low-end
  systems. The new `auto` default will determine the limit based on the
  number of workers (which in turn is based on the number of CPUs), the
  block size, and the amount of physical memory of the system.

- (feature) Replace `vector_byte_buffer` with `malloc_byte_buffer`,
  which is internally based around a simple buffer that doesn't incur
  the cost of initializing each element like `std::vector`. Especially
  for large blocks which are known to be overwritten immediately, this
  can save a few CPU cycles.

- (feature) In the `x86_64` release binaries, use an optimized `memcpy`
  implementation if supported by the CPU.

## Version 0.12.0 - 2025-04-08

- (fix) Build release binaries against an up-to-date `libfuse`.
  Fixes github #252.

- (fix) Changes for compatibility with Boost.Process v2.

- (feature) Re-licensed all libraries required for *reading* DwarFS
  images under the MIT license. The source of all tools that just
  *read* DwarFS images (i.e. everything except for `mkdwarfs`) are
  also under the MIT license now. Everything else is still GPL-3.0.
  Addresses github #255.

- (feature) Significantly reduced binary size in the static release
  builds. This is the result of refactoring code that unconditionally
  pulled in code-heavy dependencies such as `libcrypto`, as well as
  optimizing the build pipeline (e.g. building dependencies with only
  the necessary set of features) and turning on link time optimization.

- (feature) A new kind of "universal" binary `dwarfs-fuse-extract` is
  part of the release now. This combines the FUSE driver (`dwarfs`)
  and `dwarfsextract` into a single binary, but does not include the
  `mkdwarfs` and `dwarfsck` tools that are also part of the regular
  universal binary. `dwarfs-fuse-extract` is much smaller than the
  regular universal binary and especially suitable to AppImage-like
  applications.

- (feature) New `hotness` categorizer in `mkdwarfs` that allows a list
  of "hot" files to be stored in distinct file system blocks.

- (feature) New `explicit` ordering mode in `mkdwarfs` that allows
  files to be ordered accoring to the order in a given list file.

- (feature) `dwarfs` now shows the version of the FUSE library used.

- (feature) New `dwarfs` options `preload_all` and `preload_category`
  to populate the block cache immediately after mounting.

- (feature) New `dwarfs` option `analysis_file` that can be used for
  profiling and as input to `mkdwarfs` new `hotness` categorizer and
  `explicit` ordering mode.

- (feature) New `dwarfs` option `block_allocator` that allows the user
  to switch from a `malloc`-based block allocator to an `mmap`-based
  one. This can help with returning memory back to the system if the
  blocks are evicted from the cache.

## Version 0.11.3 - 2025-03-31

- (fix) Handle absolute paths in `--input-list`. Fixes github #259.

- (fix) Don't prefetch blocks that are already in the active list
  within the block cache.

- (fix) Ensure that statistics for block tidying are correctly
  updated in the block cache.

- (build) A few build fixes, mainly to simplify building on Alpine.

## Version 0.11.2 - 2025-03-20

- (fix) macOS Ventura's version of clang appears to be missing an
  implementation of `std::hash<std::filesystem::path`, making it
  hard to define an `unordered_map<filesystem::path`. Work around
  by simply using an `unordered_map<string>` instead.

- (fix) Installing the binaries using cmake did not honor the
  `CMAKE_INSTALL_BINDIR` or `CMAKE_INSTALL_SBINDIR` variables.
  Fixes github #253.

## Version 0.11.1 - 2025-03-18

- (fix) macOS Ventura's version of clang appears to be missing the
  `<source_location>` header, despite Apple claiming otherwise. Fix
  this by shipping a wrapper and providing a fallback implementation.

## Version 0.11.0 - 2025-03-17

- (fix) Remove the `access` implementation from the FUSE driver.
  There's no point here trying to be more clever than FUSE's
  default. This makes sure DwarFS will behave more like other
  FUSE file systems. See github discussion #244 for details.

- (fix) Limit the number of chunks returned in `inodeinfo`
  xattr. Highly fragmented files would have *megabytes* in
  `inodeinfo`, which not only breaks the xattr interface, but
  can also dramatically slow down tools like `eza` who like to
  read xattrs for no apparent reason.

- (fix) Avoid nested indentation due to `ronn-ng` bug. Fixes
  github #249.

- (fix) Don't link library against `jemalloc`. This fixes both
  issues with `pydwarfs` and issues building with `jemalloc`
  support on macOS. Only the binaries are now linked against
  `jemalloc`, which should be sufficient.

- (feat) Support case-insensitive lookups. Fixes github #232.

- (feat) Allow setting image size in FUSE driver. Fixes github
  #239.

- (feat) Support extracting a subset of files with `dwarfsextract`
  using the new `--pattern` option. The same glob patterns can be
  used as for the filter rules in `mkdwarfs`. Fixes github #243.

- (feat) Allow overriding UID / GID for the whole file system
  when using the FUSE driver on non-Windows platforms. See github
  discussion #244.

- (feat) Expose more LZMA options (`mode`, `mf`, `nice`, `depth`).

- (feat) Improve filter patterns, which now support ranges and
  complementation.

- (feat) Improve speed of filesystem `walk` / `walk_data_order`
  calls by 80% / 40%. The impact of this will largely depend on
  what the code is being run for each inode, but, for example,
  the speed of listing more than 14 million files with `dwarfsck`
  will take about 16 seconds compared to 17 seconds with the
  previous release.

- (feat) Added an inode size cache to the metadata to speed up
  file size computation for large, highly fragmented files. The
  configuration is currently fixed using a conservative default.
  Only files with at least 128 chunks will be added to the cache,
  so in a lot of cases this cache may be completely empty and not
  contribute to the size of the file system image at all.

- (feat) Use bit-packing for hardlink, shared files, and chunk
  tables. This will consume less memory when loading a DwarFS
  image.

- (feat) Show total hardlink size in `dwarfsck` output.

- (feat) Library: return a `dir_entry_view` from `readdir` and
  `find`. This is more consistent, but was previously not easily
  possible due to the lack of a "self" dir entry in the metadata.
  The "self" entry has been added and will only impact the size
  of the metadata if `directories` metadata is not packed.

- (feat) Library: prefer `std::string_view` over `char const*`.

- (feat) Library: add directory iterator to `directory_view`.

- (feat) Library: support for `maxiov` parameter in `readv` call.

- (refactor) *Lots* of internal refactoring to improve overall
  code quality.

## Version 0.10.2 - 2024-12-02

- (fix) Gracefully handle localized error message on Windows.
  These error messages can contain characters from a Windows
  (non-UTF-8) code page, which could cause a fatal error in
  `fmt::print` in the logging code. Call sites that log such
  error messages now try to convert these from the code page
  to UTF-8 or, if that fails, simply replace all characters
  that are invalid from a UTF-8 point-of-view. Partial fix for
  github #241.

- (fix) Handle invalid wide chars in file names on Windows. For
  some reason, Windows allows invalid UTF-16 characters in file
  names. Try to handle these gracefully when converting to UTF-8.
  Partial fix for github #241.

- (fix) Workaround for new boost versions which have a `process`
  component.

- (fix) Workaround for a deprecated boost header.

- (fix) Support for upcoming Boost 1.87.0. `io_service` was
  deprecated and replaced by `io_context` in 1.66.0. The upcoming
  Boost 1.87.0 will remove the deprecated API. (Thanks to Michael
  Cho for the fix.)

- (fix) Disable extended output algorithms (`shake(128|256)`).

- (fix) Install libraries to `CMAKE_INSTALL_LIBDIR`. Fixes github
  #240.

- (fix) mode/uid/gid checks were expecting 16-bit types.

- (fix) stricter metadata checks and improved error messages.

- (fix) Various fixes for `filesystem_extractor` to prevent memory
  leaks, correctly handle errors during extraction, and prevent
  creation of invalid archive outputs due to padding.

- (fix) Various minor fixes: non-virtual dtors, missing includes,
  `std::move` vs. `std::forward`, unused code removal.

- (test) More test cases for stricter metadata checks. Also enable
  the strict checks in in unit tests by default.

- (docs) Fix typos in man pages.

## Version 0.10.1 - 2024-08-17

- (fix) Allow building `utils_test` against a non-compatible,
  system-installed version of gtest. This is a common issue
  when trying to integrate dwarfs into a package manager, as
  these generally disallow fetching external dependencies at
  build time.

- (fix) `dwarfsck` was always reporting a block size of 1 byte
  rather than the actual block size of the image.

- (fix) `DWARFS_HAVE_LIBBROTLI` was not set correctly in the
  config file, causing build errors if the library was built
  without `brotli`.

- (fix) Several small fixes for building with Homebrew.

## Version 0.10.0 - 2024-08-14

- (fix) Fixed a race condition identified by ThreadSanitizer
  in the root node name processing.

- (fix) The terminal abstraction code did not check any errors
  when trying to determine the terminal width, leading to a
  random terminal width value. This caused the manual page
  tests to occasionally crash.

- (feat) Two sets of universal binaries and binary tarballs are
  provided for Linux platforms: one without any debug symbols, the
  other with minimal debug symbols and support for stack traces.
  For the universal binary, only the version without debug symbols
  will be UPX-compressed, as the stack trace functionality doesn't
  work with a compressed binary.

- (feat) Symbolic links to the universal binary may now be
  suffixed with a version (i.e. any part of the name starting
  with `-` and followed by a digit will be ignored, e.g. the
  symlink could be `mkdwarfs-0.10` and it would be treated
  as `mkdwarfs`).

- (feat) Introduced support for extended attributes on Windows,
  including a new utility for cross-platform xattr manipulation
  (`pxattr`, for portable xattr).

- (feat) Enhanced file system API, adding error-code based and
  exception-safe versions for `getattr`, `access`, and similar
  functions.

- (feat) Filter rules now consistently use Unix path separators,
  even for the root path component. Addresses a comment in github
  discussion #228.

- (refactor) Extensive refactoring to improve code modularity,
  maintainability and to provide proper libraries. The library
  code has been moved to different namespaces to make it easier
  to understand the role of different components (e.g. `reader`,
  `writer`, `extractor`).

- (refactor) Replaced all `folly` library dependencies in the public
  DwarFS library interface with alternatives from libraries like
  e.g. `boost` or `nlohmann::json` which are more broadly available.
  `folly` and `fbthrift` are still used as implementation details,
  but no longer leak into the public library interfaces.

- (refactor) A much smaller subset of `folly` is now used in DwarFS
  and only the necessary components are built, significantly
  reducing the number of compilation units when building DwarFS.

- (build) It is now possible to do modular builds in addition to
  the default monolithic build, i.e. you can build and install
  just the DwarFS libraries and later build/install the tools
  (`mkdwarfs`, ...) and/or the FUSE driver against these libraries.
  This is particularly useful for packaging (e.g. in Homebrew,
  which has removed all FUSE support from the core formulae).

- (build) Shared library builds are now explicitly supported.
  This fixes issues such as github #184.

- (build) The source tarball now contains all auto-generated code,
  e.g. manual pages or generated thrift code. This reduces the
  number of build-time dependencies (e.g. `ronn` or `mistletoe`
  are no longer required) and significantly reduces the build
  steps (it is no longer necessary to build the thrift compiler).
  The build is now roughly twice as fast as in the 0.9.x releases.

- (build) The `parallel-hashmap`, `xxHash` and `zstd` submodules
  have been removed from the git repo and are no longer added to
  the source tarball. Both `xxHash` and `zstd` are now widely
  available. If a suitable version of `parallel-hashmap` is found
  on the system, this will be used, otherwise it will be fetched
  during the build. Being a header-only library and only used
  internally, there's no need for it to be installed.

- (build) A *lot* of GCC warnings have been fixed and upstreamed
  to `folly` / `fbthrift`.

- (test) Fixed some flaky tests, e.g. unmounting the FUSE driver
  on macOS, or the manpage test that used to crash occasionally.

## Version 0.9.10 - 2024-05-30

- (fix) When cloning LZMA compressor objects, the LZMA filter options
  of the cloned instance would still point to an options object in the
  original instance. This could lead to LZMA errors when initializing
  a new compressor. Fixes github #224.

- (fix) Fetch range-v3 if no suitable version is found. Fixes github #221.

- (fix) Filter rules did not work correctly when input is root dir

- (fix) `duf` reports odd sizes due to using `bsize` instead of `frsize`

## Version 0.9.9 - 2024-04-30

- (fix) A bug introduced by an optimization to skip hashing of large
  files if they already differ in the first 4 KiB could, under rare
  circumstances, lead to an unexpected "inode has no file" exception
  after the scanning phase. This bug did not cause any file system
  inconsistency issues; `mkdwarfs` either crashes with the exception,
  or its output will be correct. Fixes github #217.

- (feat) Add sequential access detector and block prefetching to the
  block cache. This improves sequential read throughput roughly by a
  factor of two. Can be configured / disabled using `-o seq_detector`.

- (feat) Add tracing support in FUSE driver and `dwarfsextract`, which
  allows simple performance analysis using chrome://tracing. Traces can
  be enabled using `-o perfmon_trace` and `--perfmon-trace`.

- (feat) Add performance monitoring and tracing support for the block
  cache.

- (perf) Significantly improve speed of `dwarfsck --checksum`.

## Version 0.9.8 - 2024-04-14

- (fix) Build custom version of libcrypto to link with the release
  binaries in order for them to run properly on FIPS-enabled setups.
  Fixes github #210.

- (fix) When mounting a DwarFS image on macOS and viewing the volume
  in Finder, only the directories were shown, but no files. The root
  cause was that a non-existent extended attribute is reported via a
  different error code in macOS (`ENOATTR`) compared to Linux (`ENODATA`)
  and the wrong error code was returned for certain Finder-related
  attributes. Fixes github #211.

- (fix) macOS builds using jemalloc were crashing when calling
  `mallctl("version", ...)`. The root cause of the crash is still
  unclear, but as a workaround, the jemalloc version is compiled
  in from a preprocessor constant rather than using `mallctl`.

## Version 0.9.7 - 2024-04-10

- (fix) Handle root uid correctly in access() implementation.
  Fixes github #204.

- (feature) Show and track library dependencies. Dependencies will
  be displayed in the command line help; they will also be tracked
  in the history metadata of a DwarFS image. See also github #207.

- (doc) Describe nilsimsa ordering algorithm more accurately.

- (perf) Reorder branches to improve ricepp speed with real world
  data.

- (perf) Some tweaks to improve segmenter speed.

## Version 0.9.6 - 2024-02-24

- (fix) Add workaround for new glog release breaking folly build.
  Fixes github #201.

- (perf) Improve `ricepp` decoding speed by about 25% on x86 and arm
  and up to 100% on Windows. Also improve encoding speed on Windows
  by 25%. No more need for special Clang build.

## Version 0.9.5 - 2024-02-13

- (fix) Windows path handling was wrong and didn't work properly for
  e.g. network shares. This is hopefully fixed for all tools now.

## Version 0.9.4 - 2024-02-12

- (fix) Prevent installation of ricepp headers/libs. Fixes github #195.

- (fix) Don't fetch googletest in ricepp build if the targets are
  already available. Fixes github #194.

- (feature) Added `blocksize` option to the FUSE driver, which allows
  the `st_blksize` value to be configured for the mounted file system.
  Increasing this value can improve throughput for large files.

- (feature) Added experimental `readahead` option to the FUSE driver.
  This can potentially increase throughput when performing sequential
  reads.

## Version 0.9.3 - 2024-02-11

- (fix) v0.8.0 removed the implementation of the `null` decompressor
  under the assumption that it was no longer used; it was, however,
  still used when recompressing an image with `null`-compressed blocks.
  The change to remove the implementation was reverted and a new test
  case was added. Fixes github #193.

- (perf) Some more `ricepp` compression speed improvements. Also, the
  universal binaries for `x86_64` now automatically choose a `ricepp`
  version based on CPU capabilities.

## Version 0.9.2 - 2024-02-09

- (fix) v0.9.0 introduced an optimization where large files of equal
  size were only fully hashed for deduplication if the first 4K of their
  contents also produced the same hash. This introduced a bug causing
  an exception to be thrown when processing large hard-linked files.
  The root cause was that the data structure intended to be used for
  exactly this case was just never populated, and the fix was adding
  a single line to fill the data structure. The test cases didn't cover
  large hard-linked files, so this slipped through into the release.
  A new test case has been added as well.

- (fix) On Windows, when using Power Shell, the error message dialog
  for a missing WinFsp DLL was not shown when running `dwarfs.exe`.
  The workaround is to use the same delayed loading mechanism that's
  already used for the universal binary and show the error in the
  terminal. See also the discussion on github #192.

- (feature) Added a `--list` option to `dwarfsck`. This lists all files
  in the files system image. When used with `--verbose`, the list also
  shows permissions, size, uid/git and symbolic link information.
  Fixes github #192.

- (feature) Added a `--checksum` option to `dwarfsck`. This produces
  output similar to the `*sum` programs from coreutils and can be used
  to check the contents of a DwarFS image against local files.

## Version 0.9.1 - 2024-02-06

- (fix) Invalid UTF-8 characters in file paths would crash `mkdwarfs`
  if these paths were displayed in the progress output. A possible
  workaround was to disable progress output. This fix replaces any
  invalid characters before displaying them. Fixes github #191.

- (fix) The `CMakeLists.txt` would bail out as soon as it discovered
  `--as-needed` in the linker flags. However, `--as-needed` is only
  a problem when combined with `BUILD_SHARED_LIBS=ON`. The check has
  been changed to only trigger if both conditions are met.

- (perf) Minor speed improvements in `ricepp` compression.

## Version 0.9.0 - 2024-02-05

- (feature) Experimental macOS support. Fixes github #132.

- (feature) New ricepp compression algorithm for raw images as well
  as a categorizer for the FITS image format. This is quite limited
  at the moment, as only two-dimensional, 16-bit integer FITS is
  supported. However, this covers the majority of astro camera images,
  which is the primary use case at the moment. This can likely be
  extended to other raw image formats in the future.

## Version 0.8.0 - 2024-01-22

- (fix) Allow version override for nixpkgs. Fixes github #155.

- (fix) Resize progress bar when terminal size changes. Fixes github #159.

- (fix) Add Extended Attributes section to README. Fixes github #160.

- (fix) Support 32-bit uid/gid/mode. Also support more than 65536
  uids/gids/modes in a filesystem image. Fixes gh #173.

- (fix) Add workaround for broken `utf8cpp` release. Fixes github #182.

- (fix) Don't call `check_section()` in filesystem ctor, as it renders
  the section index useless. Also add regression test to ensure this
  won't be accidentally reintroduced. Fixes github #183.

- (fix) Ensure timely exit in progress dtor. This could occasionally
  block command line tools for a few seconds before exiting.

- (fix) `--set-owner` and `--set-group` did not work properly with
  non-zero ids. There were two distinct issues: (1) when building a
  DwarFS image with `--set-owner` and/or `--set-group`, the single
  uid/gid was stored in place of the index and the respective lookup
  vectors were left empty and (2) when reading such a DwarFS image,
  the uid/gid was always set to zero. The issue with (1) is not only
  that it's a special case, but it also wastes metadata space by
  repeatedly storing a potentially wide integer value.
  This fix addresses both issues. The uid/gid information is now
  stored more efficiently and, when reading an image using the old
  representation, the correct uid/gid will be reported.
  Unit tests were added to ensure both old and new formats are
  read correctly.

- (fix) `mkdwarfs` is now much better at handling inaccessible or
  vanishing files. In particular on Windows, where a successful
  `access()` call doesn't necessarily mean it'll be possible to open
  a file, this will make it possible to create a DwarFS file system
  from hierarchies containing inaccessible files. On other platforms,
  this means `mkdwarfs` can now handle files that are vanishing while
  the file system is being built.

- (fix) `mkdwarfs` progress updates are now "atomic", i.e. one update
  is always written with a single system call. This didn't make much
  of a difference on Linux, but the notoriously slow Windows terminal,
  along with somewhat interesting thread scheduling, would sometimes
  make the updates look like a typewriter in slow-motion.

- (fix) `utf8_truncate()` didn't handle zero-width characters properly.
  This could cause issues when truncating certain UTF8 strings.

- (fix) A race condition in `simple` progress mode was fixed.

- (fix) A race condition in `filesystem_writer` was fixed.

- (fix) The `--no-create-timestamp` option in `mkdwarfs` was always
  enabled and thus useless.

- (fix) Common options (like `--log-level`) were inconsistent between
  tools.

- (fix) Progress was incorrect when `mkdwarfs` was copying sections
  with `--recompress`.

- (fix) Treat NTFS junctions like directories.

- (fix) Fix canonical path on Windows when accessing mounted DwarFS image.

- (fix) Fix slow sorting in `file_scanner` due to path comparison.

- (fix) On Windows, don't crash with an assertion if the input path for
  `mkdwarfs` is not found.

- (remove) Python scripting support has been completely removed.

- (feature) Categorizer framework. Initially supported categorizers are
  `pcmaudio` (detect audio data & metadata and provide context for FLAC
  compressor) and `incompressible` (detects "incompressible" data).
  Enabled using the `--categorize` option.

- (feature) Multiple segmenters can now run in parallel and write to
  the same filesystem image in a fully deterministic way. Currently,
  a segmenter instance will be used per category/subcategory. This can
  makes segmenting multi-threaded in cases where there are multiple
  categories. The number of segmenter worker threads can be configured
  using `--num-segmenter-workers`.

- (feature) The segmenter now supports different "granularities". The
  granularity is determined by the categorizer. For example, when
  segmenting the audio data in a 16-bit stereo PCM file, the granularity
  is 4 (bytes). This ensures that the segmenter will only produce chunks
  that start/end on a sample boundary.

- (feature) The segmenter now also features simple "repeating sequence
  detection". Under certain conditions, these sequences could cause the
  segmenter to slow down dramatically. See github #161 for details.

- (feature) FLAC compression. This can only be used along with the
  `pcmaudio` categorizer. Due to the way data is spread across different
  blocks, both FLAC compression and decompression can likely make use
  of multiple CPU cores for large audio files, meaning that loading a
  `.wav` file from a DwarFS image using FLAC compression will likely
  be much faster than loading the same data from a single FLAC file.

- (feature) Completely new similarity ordering implementation that
  supports multi-threaded and fully deterministic nilsimsa ordering.
  Also, nilsimsa options are now ever so slightly more user friendly.

- (feature) The `--recompress` feature of `mkdwarfs` has been largely
  rewritten. It now ensures the input filesystem is checked before an
  attempt is made to recompress it. Decompression is now using multiple
  threads. Also, recompression can be applied only to a subset of
  categories and compression options can be selected per category.

- (feature) `mkdwarfs` now stores a history block in the output image
  by default. The history block contains information about the version
  of `mkdwarfs`, all command line arguments, and a time stamp. A new
  history entry will be added whenever the image is altered (i.e. by
  using `--recompress`). The history can be displayed using `dwarfsck`.
  History timestamps can be disabled using `--no-history-timestamps`
  for bit-identical images. History creation can also be completely
  disabled using `--no-history`.

- (feature) All tools now come with built-in manual pages. This is
  valuable especially on Windows, which doesn't have `man` at all,
  or for the universal binaries, which are usually not installed
  alongside the manual pages. Running each tool with `--man` will
  show the manual page for the tool, using the configured pager.
  On Windows, if `less.exe` is in the PATH, it'll also be used as
  a pager.

- (feature) New `verbose` logging level (between `info` and `debug`).

- (feature) Logging now properly supports multi-line strings.

- (feature) Show compression library versions as part of the `--help`
  output. For `dwarfsextract`, also show `libarchive` version.

- (feature) `--set-time` now supports time strings in different formats
  (e.g. `20240101T0530`).

- (feature) `mkdwarfs` can now write the filesystem image to `stdout`,
  making it possible to directly stream the output image to e.g. `netcat`.

- (feature) Progress display for `mkdwarfs` has been completely
  overhauled. Different components (e.g. hashing, categorization,
  segmenting, ...) can now display their own progress in addition
  to a "global" progress.

- (feature) `mkdwarfs` now supports ordering by "reverse path" with
  `--order=revpath`. This is like `path` ordering, but with the path
  components reversed (i.e. `foo/bar/baz.xyz` will be ordered as if
  it were `baz.xyz/bar/foo`).

- (feature) It is now possible to configure larger bloom filters in
  `mkdwarfs`.

- (feature) The `mkdwarfs` segmenter can now be fully disabled using
  `-W 0`.

- (feature) `mkdwarfs` now adds "feature sets" to the filesystem
  metadata. These can be used to introduce now features without
  necessarily breaking compatibility with older tools. As long as
  a filesystem image doesn't actively use the new features, it can
  still be read by old tools. Addresses github #158.

- (feature) `dwarfsck` has a new `--quiet` option that will only
  report errors.

- (feature) `dwarfsck` with `--print-header` will exit with a special
  exit code (2) if the image has no header. In all other cases, the
  exit code will be 0 (no error) or 1 (error).

- (feature) The `--json` option of `dwarfsck` now outputs filesystem
  information in JSON format.

- (feature) `dwarfsck` has a new `--no-check` option that skips
  checking all block hashes. This is useful for quickly accessing
  filesystem information.

- (feature) The FUSE driver exposes a new `dwarfs.inodeinfo` xattr
  on Linux that contains a JSON object with information about the
  inode, e.g. a list of chunks and associated categories.

- (feature) Don't enable `readlink` in the FUSE driver if filesystem
  has no symlinks. This is mainly useful for Windows where symlink
  support increases the number of `getattr` calls issued by `WinFsp`.

- (feature) As an experimental feature, CPU affinity for each worker
  group can be configured via the `DWARFS_WORKER_GROUP_AFFINITY`
  environment variable. This works for all tools, but is really only
  useful if you have different types of cores (e.g. performance and
  efficiency cores) and would like to e.g. always run the segmenter
  on a performance core.

- (doc) Add mkdwarfs sequence diagram.

- (doc) Document known issues with WinFsp.

- (doc) Update README with extended attributes information.

- (doc) Add script to check if all options are documented in manpage.

- (build) Factor out repetitive thrift library code in CMakeLists.txt.

- (build) Use FetchContent for both `fmt` and `googletest`.

- (build) Use `mold` for linking when available.

- (build) The CI workflow now uploads coverage information to codecov.io
  with every commit.

- (test) A *ton* of tests were added (from 4 kLOC to more than 10 kLOC)
  and, unsurprisingly, a number of bugs were found in the process.

- (test) Introduced I/O abstraction layer for all `*_main()` functions.
  This allows testing of almost all tool functionality without the need
  to start the tool as a subprocess. It also allows to inject errors more
  easily, and change properties such as the terminal size.

- (other) The universal binaries are now compressed with a different `upx`
  compression level, making them slightly bigger, but decompress much
  faster.

## Version 0.7.5 - 2024-01-16

- (fix) Fix crash in the FUSE driver on Windows when tools like Notepad++
  try to access a file like a directory (presumably because this works in
  cases where the file is an archive). This is a Windows-only issue because
  the Linux FUSE driver uses the inode-based API, whereas the Windows driver
  uses the string-based API. While parsing a path in the string-based API,
  there was no check whether a path component was a directory before trying
  to descend further.

## Version 0.7.4 - 2023-12-28

- (fix) Fix regression that broke section index optimization introduced
  in v0.7.3. Fixes github #183.

- (fix) Add workaround for broken utf8cpp release. Fixes github #182.

## Version 0.7.3 - 2023-12-05

- (feature) Support forward-compatibility. Fixes github #158.

## Version 0.7.2 - 2023-07-24

- (fix) Fix locale fallback if user-default locale cannot be set.
  Fixes github #156.

## Version 0.7.1 - 2023-07-20

- (fix) Fix potential division by zero crash in speedometer.

- (other) New tool header.

- (other) Source code cleanups.

- (other) Updated static build procedure (see README).

## Version 0.7.0 - 2023-07-11

- (fix) FUSE/WinFsp driver now handles Unicode characters in the
  file system image name (the file system itself would already
  work properly with Unicode file names).

- (fix) Fixed heap-use-after-free when using a file system image
  built with brotli compression. This was caught last minute by
  ASAN.

- (fix) Catch errors from locale-setting at startup. These errors
  will only be reported now, but will no longer cause the program
  to abort.

- (feature) `mkdwarfs` command-line options have been reorganized
  into groups to make them easier to find and to make the default
  help message less intimidating. The full help can now be accessed
  using `-H` or `--long-help`.

- (feature) Symbolic links to the universal binary now also work
  as aliases on Windows.

- (test) Test universal binary in both `--tool` and symlink modes.

- (other) CI pipeline tweaks & fixes.

## Version 0.7.0-RC6 - 2023-07-09

- (feature) Support delayed loading of WinFsp DLL for universal
  binary. This makes the `mkdwarfs`, `dwarfsck` and `dwarfsextract`
  tools of the universal binary usable without the WinFsp DLL.

- (perf) Optimized the offset cache to improve random read
  latency as well as sequential read latency. This gave a
  [100x higher throughput](https://github.com/mhx/dwarfs/issues/142)
  for a case where DwarFS was used to compress raw file system
  images. Fixes github #142.

- (fix) Fix building with `make` instead of `ninja`. Also fix
  builing in `Debug` mode. Fixes github #146.

- (fix) Fix `ninja clean`.

- (fix) Fix symlink creation for `mount.dwarfs`/`mount.dwarfs2`.

- (other) Added [CI pipeline](https://github.com/mhx/dwarfs/actions).

- (other) Don't write versioning files to source tree.

## Version 0.7.0-RC5 - 2023-07-04

- (feature) Windows support. All tools can now be built and run
  on Windows, including the FUSE driver, which makes use of
  [WinFsp](https://github.com/winfsp/winfsp).

- (feature) Build a "universal" binary that combines `mkdwarfs`,
  `dwarfsck`, `dwarfsextract` and `dwarfs` in a single binary.
  This binary can be used either through symbolic links with
  the proper names of the tool, or by passing `--tool=<name>`
  as the first argument on the command line.

- (feature) Bypass the block cache for uncompressed blocks. This
  saves copying block data to memory unnecessarily and allows us
  to keep all uncompressed blocks accessible directly through the
  memory mapping. Partially addresses github #139.

- (feature) Show throughput in the scanning and segmenting
  phases in `mkdwarfs`.

- (feature) Show how much of a file has been consumed in the
  segmenting phase. Useful primarily for large files.

- (feature) `dwarfs` and `dwarfsextract` now have options to
  enable performance monitoring. This can give insight into the
  latency of various file system operations.

- (feature) Added inode offset cache, which improves `read()`
  latency for very fragmented files.

- (fix) Use `folly::hardware_concurrency()`. Fixes github #130.

- (fix) Handle `ARCHIVE_FAILED` status from libarchive, which
  could be triggered by trying to write long path names to old
  archive formats.

- (fix) Properly handle unicode path truncation.

- (doc) Update file system format documentation to cover headers
  and section indices.

- (test) Lots of new tools tests.

- (test) Remove dependency on `tar` and `diff` binaries.

- (other) Switch to C++20.

## Version 0.7.0-RC4 - 2022-12-24

- (feature) Add `--compress-niceness` option to `mkdwarfs`.

## Version 0.7.0-RC3 - 2022-11-20

- (fix) Fix heap-use-after-free in dwarfsextract.

- (fix) Fix dwarfs benchmark binary.

- (feature) Add `--stdout-progress` option to `dwarfsextract`.
  Fixes github #117.

- (test) Reduce amount of test data to speed up compiles and avoid
  timeouts on travis.

## Version 0.7.0-RC2 - 2022-11-17

- (fix) Fix linking against compression libs. Fixes github #112.

- (fix) Default FUSE driver debuglevel to `warn` in background
  mode. Fixes github #113.

- (feature) Add `--chmod` option. Fixes github #7.

- (feature) Add unreadable files as empty files. Fixes github #40.

- (doc) Document how to produce bit-identical images

- (doc) Update internal operation section of mkdwarfs manpage

- (doc) Add more documentation details for `--file-hash` option

- (test) Test image reproducibility for path and similarity ordering

## Version 0.7.0-RC1 - 2022-11-08

- (fix) Fixed `extract_block.py`, which was incorrectly using `printf`
  instead of `print`.

- (fix) Support LZ4 compression levels above 9.

- (feature) Added `--filter` option to support simple (rsync-like)
  filter rules. This was driven by a discussion on github #6.

- (feature) Added `--input-list` option to support reading a list
  of input files from a file or stdin. At least partially fixes
  github #6.

- (feature) The compression code has been made more modular. This
  should make it much easier to add support for more compression
  algorithms in the future.

- (feature) Added support for Brotli compression. This is generally much
  slower at compression than ZSTD or LZMA, but faster than LZMA, while
  offering a compression ratio better than ZSTD. Fixes github #76.

- (feature) Added support for choosing the file hashing algorithm using
  the `--file-hash` option. This allows you to pick a secure hash
  instead of the default XXH3. Also fixes github #92.

- (feature) Improved de-duplication algorithm to only hash files with
  the same size. File hashing is delayed until at least one more file
  with the same size is discovered. This happens automatically and
  should improve scanning speed, especially on slow file systems.

- (feature) Added `--max-similarity-size` option to prevent similarity
  hashing of huge files. This saves scanning time, especially on slow
  file systems, while it shouldn't affect compression ratio too much.

- (feature) Honour user locale when formatting numbers.

- (feature) Added `--num-scanner-workers` option.

- (feature) Added support for extracting corrupted file systems with
  `dwarfsextract`. This is enabled using the `--continue-on-error`
  and, if really needed, `--disable-integrity-check` options. Fixes
  github #51.

- (test) Added unit tests for progress class.

- (other) Lots of internal cleanups.

## Version 0.6.2 - 2022-10-24

- (fix) Fix github #91: image creation reproducibility.
  Add `--no-create-timestamp` option, produce deterministic
  inode numbers and fix `fsst` bug that causes symbol tables
  to be non-deterministic. Images built while omitting create
  timestamps will now be bit-identical.

- (fix) Fix github #93: only overwrite existing output file
  when `--force` option given on command line.

- (fix) Fix github #104: extracting large files was causing
  `dwarfsextract` to OOM. This was fixed by extracting large
  files in chunks rather than all at once.

- (fix) Fix github #105: handle `strrchr()` return `NULL`.

- (fix) Fix out-of-bounds access (PR #106).

- (fix) Fix swapped-out cached block detection (PR #107).

- (fix) Fix data race in cached block that was triggered by
  statistics collection and could cause the process to crash.

- (fix) Fix heap-use-after-free when writing section index.

## Version 0.6.1 - 2022-06-11

- (fix) Fix binary installation

## Version 0.6.0 - 2022-06-11

- (fix) Fix and simplify static builds as much as possible.
  Document how to set up a static build environment. This
  also fixes github #75 and github #54. Huge shoutout to
  Maxim Samsonov for implementing most of this!

- (fix) Fix github #71: driver hangs when unmounting

- (fix) Fix github #67: dwarfs I/O hangs if call to to
  `fuse_reply_iov` fails

- (fix) Fix github #86: block size bits config issues

- (fix) Various build fixes.

- (feature) Add support for cache tidying, which releases
  cache memory when the mounted file system is unused.

- (feature) Section index support for speeding up mount times
  (fixes github #48).

## Version 0.5.6 - 2021-07-03

- (fix) Build fixes for gcc-11

- (fix) Use `REALPATH` in `version.cmake` to fix building in
  symbolically linked repositories (fixes github #47).

## Version 0.5.5 - 2021-05-03

- (feature) If a filesystem block cannot be compressed to less
  than the uncompressed size, it will be stored uncompressed.
  This feature actually fixes the bug described below.

- (fix) When building a filesystem from high entropy input data
  (e.g. already compressed files), and when using LZMA compression
  with block sizes >= 25, the LZMA algorithm could be unable to
  pack a block into the worst-case allocated size. This behaviour
  was not expected and crashed `mkdwarfs`, and seems to me like a
  bug in LZMA's `lzma_stream_buffer_bound()` function. The issue
  has been fixed by not compressing blocks at all if the compressed
  size matches or exceeds the uncompressed size. This fixes part of
  github #45.

- (fix) Filesystems created such that after segmenting the total
  data size was a multiple of the block size (i.e. the last block
  was completely filled) had the last block written to the image
  twice. Such a filesystem image is perfectly usable, but the
  repeated block uses space unnecessarily. This is highly unlikely
  to happen with real data.

- (fix) Filesystems created with `-P shared_files`, but no shared
  files in the source tree, were created correctly, but could not
  be loaded. This has been fixed and the filesystems can now be
  loaded correctly.

- (test) Add tests for binaries and FUSE driver.

- (other) Minor code cleanups.

## Version 0.5.4 - 2021-04-11

- (fix) FUSE driver hangs when accessing files and the driver is
  *not* started in foreground or debug mode. This bug is present
  in both the 0.5.2 and 0.5.3 releases. Fixes github #44.

## Version 0.5.3 - 2021-04-11

- (fix) Add `PREFER_SYSTEM_GTEST` for distributions (like Gentoo)
  that have a `gtest` package.

- (fix) Make sure the source tarball can be built inside a git repo.
  The version file generation code would attempt to pull information
  from any outside git repository without checking if it's actually
  the DwarFS repo.

## Version 0.5.2 - 2021-04-07

- (fix) Make FUSE driver exit with non-zero exit code if filesystem
  cannot be mounted. Fixes github #41.

## Version 0.5.1 - 2021-04-06

- (fix) `fsst` library was built with `-march=native`, which caused
  the static binaries not to work on non-AVX platforms. The `fsst`
  library is now being built with no extra flags.

## Version 0.5.0 - 2021-04-05

- (fix) Disable multiversioning on non-x86 platforms, which broke
  the ARM build.

- (fix) Due to a bug in the bloom filter code, only half of each
  64-bit block in the bloom filter was utilized, which reduced the
  efficiency of the filter. The bug was spotted thanks to `ubsan`.
  With the fixed filter being twice as effective, the default size
  of the bloom filter has now been halved.

- (fix) When exporting metadata using `--export-metadata`, `dwarfsck`
  was not truncating the output file, which could lead to a corrupt
  metadata export.

- (perf) Scanning has been significantly optimized and is now up to
  three times faster on average.

- (perf) Digest computation has been parallelized in both `mkdwarfs`
  and `dwarfsck` giving better performance on multi-core systems.

- (perf) A set of micro-benchmarks has been added to evaluate the
  performance of different filesystem operations. This can be
  build by enabling the `-DWITH_BENCHMARKS=1` cmake option.

- (perf) Zstd contexts are now reused during compression, which
  seems to give some minor speedup.

- (feature) New metadata format (v2.3). This includes a number of
  changes:

  - Correct hardlink preservation. With older metadata formats,
    all duplicate files would appear hardlinked. The new format
    preserves hardlinked files exactly as present in the input
    data, and performs additional deduplication at a lower level.

  - The new format offers a lot of customization for additional
    packing of metadata. You can use these to trade off metadata size,
    mounting speed, etc. Especially for filesystems with millions of
    files, the metadata size can be reduced significantly.

  - In particular, filename and symlink data can be stored in a
    [format](https://github.com/cwida/fsst) that reduces the size
    by roughly a factor of two, but still allows for random access,
    so the compressed data can be mapped into memory and decompressed
    on the fly.

- (feature) DwarFS now directly supports images using a custom
  header. The header can be completely arbitrary. `mkdwarfs` can
  write, replace or remove such headers, and all other tools can
  either skip to a specified offset, or determine this offset
  automatically. This fixes github #38.

- (feature) `dwarfsck` has been improved to perform extensive
  metadata checks. Also, checksumming is now done in a thread pool,
  which significantly speeds up `dwarfsck` for large file systems.

- (feature) `dwarfsck` now shows a detailed breakdown of metadata
  memory usage, which can be used to optimize metadata packing
  options.

- (feature) Added `ENABLE_COVERAGE` cmake option.

- (test) Compatibility testing with older filesystem versions has
  been improved.

- (test) A new test suite has been added to check detection of
  corrupted DwarFS images.

- (doc) Added some high level internals documentation for `mkdwarfs`.

- (doc) Documented the filesystem and metadata formats.

- (other) Lots of internal cleanups.

## Version 0.4.1 - 2021-03-13

- (fix) Linking against libarchive was fixed so that it also
  works for shared library builds. (fixes github #36)

- (fix) `mkdwarfs` didn't catch certain exceptions correctly,
  which would cause a stack trace instead of a simple error
  message. This has been fixed.

- (fix) The statically linked executables were unable to handle
  any exceptions at all due to duplicate stack unwinding code.
  This has (hopefully) been fixed now.

- (perf) GCC builds have traditionally been much slower than
  Clang builds, though it was unclear why that was the case.
  It turns out the reason is simply that CMake defaults to
  `-O3` optimization, which is known to cause performance
  regressions in some cases. The build has been changed to
  *always* build with `-O2` when doing an optimized GCC build.
  The Clang build is unaffected. (fixes github #14)

- (perf) The segmenting code now uses a bloom filter to discard
  unsuccessful matches as early and quickly as possible. While
  this only gives a minor speedup when using a single lookback
  block, as you increase the number of lookback blocks speed is
  barely affected whereas before it would slow down significantly.
  The bloom filter size (relative to the number of values) can be
  tuned by using `--bloom-filter-size`, though increasing it any
  further from the default is likely not going to make a difference.

- (perf) Nilsimsa similarity computation has been improved to
  make use of different instruction sets depending on the CPU
  architecture, speeding up the process of ordering files by
  similarity by almost a factor of 2.

- (doc) Added comparison with `lrzip`, `zpaq`. Updated `wimlib`
  comparison.

## Version 0.4.0 - 2021-03-06

- (feature) New `dwarfsextract` tool that allows extracting a file
  system image. It also allows conversion of the file system image
  directly into a standard archive format (e.g. `tar` or `cpio`).
  Extracting a DwarFS image can be significantly faster than
  extracting a equivalent compressed archive.

- (feature) The segmenting algorithm has been completely rewritten
  and is now much cleaner, uses much less memory, is significantly
  faster and detects a lot more duplicate segments. At the same time
  it's easier to configure (just a single window size instead of a
  list).

- (feature) There's a new option `--max-lookback-blocks` that
  allows duplicate segments to be detected across multiple blocks,
  which can result in significantly better compression when using
  small file system blocks.

- (compat) The `--blockhash-window-sizes` and
  `--blockhash-increment-shift` options were replaced by
  `--window-size` and `--window-step`, respectively. The new
  `--window-size` option takes only a single window size instead
  of a list.

- (fix) The rewrite of the segmenting algorithm was triggered by
  a "bug" (github #35) that caused excessive memory consumption
  in `mkdwarfs`. It wasn't really a bug, though, more like a bad
  algorithm that used memory proportional to the file size. This
  issue has now been fully solved.

- (fix) Scanning of large files would excessively grow `mkdwarfs`
  RSS. The memory would have sooner or later be reclaimed by the
  kernel, but the code now actively releases the memory while
  scanning.

- (perf) `mkdwarfs` speed has been significantly improved. The
  47 GiB worth of Perl installations can now be turned into a
  DwarFS image in less then 6 minutes, about 30% faster than
  with the 0.3.1 release. Using `lzma` compression, it actually
  takes less than 4 minutes now, almost twice as fast as 0.3.1.

- (perf) At the same time, compression ratio also significantly
  improved, mostly due to the new segmenting algorithm. With the
  0.3.1 release, using the default configuration, the 47 GiB of
  Perl installations compressed down to 471.6 MiB. With the 0.4.0
  release, this has dropped to 426.5 MiB, a 10% improvement.
  Using `lzma` compression (`-l9`), the size of the resulting
  image went from 319.5 MiB to 300.9 MiB, about 5% better. More
  importantly, though, the uncompressed file system size dropped
  from about 7 GiB to 4 GiB thanks to improved segmenting, which
  means *less* blocks need to be decompressed on average when
  using the file system.

- (build) The project can now be built to use the system installed
  `zstd` and `xxHash` libraries. (fixes github #34)

- (build) The project can now be built without the legacy FUSE
  driver. (fixes github #32)

- (other) Several small code cleanups.

## Version 0.3.1 - 2021-01-07

- (fix) Fix linking of Python libraries

- (fix) Fix missing brace in version generator code

- (fix) Ensure the code builds fine without libdwarf

- (fix) Silence a warning and remove an unused definition

## Version 0.3.0 - 2020-12-30

- (fix) File system images created with versions 0.2.2 and before
  did store symlinks incorrectly. While this was fixed in 0.2.3,
  old images could still not be read correctly. This has now been
  fixed and symlinks on all 0.2.x images will work correctly when
  using the 0.3.0+ FUSE driver.

- (fix) There was no error if the output file could not be written,
  `mkdwarfs` would just fail silently. This has now been fixed.

- (fix) When corrupted compressed blocks in either format (LZ4,
  ZSTD, LZMA) were detected, the FUSE driver would actually show
  the file contents as all zero bytes instead of signaling an I/O
  error. This has been fixes and verified for all formats.

- (fix) Better (hopefully) auto-detection of terminal settings to
  avoid using features like unicode or color when terminals don't
  support them. Fixes github #20.

- (fix) A number of checks has been added to make sure that corrupt
  file system images will not crash the binaries. In order for this
  to be most efficient, old images should be rewritten in the new
  format using:

  ```
  mkdwarfs -i old.dwarfs -o new.dwarfs --recompress none
  ```

- (compat) The metadata format has changed and new file system
  images can no longer be read by old FUSE drivers.

- (perf) Lots of tweaks and optimizations have resulted in an even
  better compression ratio while at the same time taking less time
  to build file system images. On the 48 GiB Perl dataset, for
  example, the compression improved from 555.7 MiB in 15m12s with
  0.2.3 to 471.6 MiB in 13m59s with 0.3.0.

- (perf) Replace the cyclic hash function with the one used by rsync.
  The rsync hash produces similar results, but it's faster.

- (perf) `mkdwarfs` will now make use of hard link and inode data
  to avoid scanning the same inode multiple times.

- (perf) Segmenting performance has been improved by re-using data
  structures and thus avoiding extra memory allocations.

- (perf) All binaries now use `jemalloc` by default, which uses
  significantly less memory than glibc or tcmalloc, especially
  in the FUSE driver.

- (feature) New file system image format adds integrity checking
  as well as features for easier recovery in case of corruption.
  While currently there is no way to recover a corrupt file system,
  it is important to have the data in place sooner rather than later.

- (feature) New Python scripting support completely replaces Lua
  scripting. The new interface offers a lot more options and should
  be much easier to use.

- (feature) New `nilsimsa` similarity algorithm. This has become
  the default, as it's significantly better on my test data than
  the "simple" `similarity` algorithm.

- (feature) New option `--keep-all-times` to keep atime and ctime
  in addition to just keeping mtime.

- (feature) New option `--time-resolution` that allows to configure
  the resolution with which time stamp are stored.

- (feature) Device, FIFO and socket inodes can now be stored in
  DwarFS file system images. This has to be enabled with the new
  `--with-devices` and `--with-specials` options.

- (feature) The FUSE driver can now optionally expose correct
  hard link counts.

- (feature) `mkdwarfs` now has an option `--remove-empty-dirs` to
  remove empty directories.

- (feature) The FUSE driver has 4 new options to control caching.
  `no_cache_image` will explictly try to release compressed
  blocks from the file system image back to the kernel after
  reading. `cache_image` will keep them in the cache.
  `no_cache_files` will cause decompressed files not to be cached
  by the kernel. `cache_files` will cause them to be cached. The
  defaults are `no_cache_image` and `cache_files`.

- (feature) The FUSE driver now has a `readonly` option that will
  prevent any entries in the mounted file system to show up as
  writeable. This is *not* the default, because it interferes with
  setting up overlays.

- (feature) `dwarfsck` can now dump metadata as JSON blob.

- (feature) `dwarfsck` can now also export raw metadata as JSON.
  The difference to the `--json` option is that this JSON export
  could be used to fully reconstruct the metadata for a DwarFS
  image.

- (feature) More detailed logging and better error handling.

- (test) Added backwards compatibility tests.

- (build) Added `zstd` as a submodule.

- (build) There is now a binary package with statically linked
  binaries available.

- (doc) More accurate list of dependencies.

- (doc) Document how to add `/etc/fstab` entry for DwarFS image.

- (doc) Comparison with wimlib.

- (doc) Comparison with Cromfs.

- (doc) Comparison with EROFS.

- (doc) Updated benchmarks.

## Version 0.2.4 - 2020-12-13

- Fix `--set-owner` and `--set-group` options, which caused an
  exception to be thrown at the end of creating a file system.
  (fixes github #24)

## Version 0.2.3 - 2020-12-01

- Fix link handling. There were two bugs introduced with the
  new metadata format, one in file system creation and another
  in the fuse driver. You will have to re-create a file system
  created with dwarfs < 0.2.3 if it contained links. If you
  can absolutely not re-create the file system and the data
  is precious, let me know, there's actually a way to recover
  the missing data. EDIT: There will be a fix available in the
  0.3.0 release, so you don't have to rebuild old file systems.

## Version 0.2.2 - 2020-11-30

- Remove read-only masking as it prevents writable overlays

- Throw an error in `mkdwarfs` if unrecognized command line
  arguments are encountered (github #5)

- Various build fixes (github #2. #3)

- More documentation

## Version 0.2.1 - 2020-11-29

- Replace --no-owner and --no-time with more flexible --set-owner,
  --set-group and --set-time options

- Update man pages

## Version 0.2.0 - 2020-11-29

- Complete rewrite of the file system metadata storage using
  fbthrift's `frozen` library

## Version 0.1.1 - 2020-11-23

- Test and fix Debian Buster and Ubuntu Focal builds

- Migrate from `folly::StringPiece` to `std::string_view`

- Documentation updates, list Debian/Ubuntu dependencies

## Version 0.1.0 - 2020-11-22

- Initial release

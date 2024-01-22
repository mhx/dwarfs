# Change Log

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

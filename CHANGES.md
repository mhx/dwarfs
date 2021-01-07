# Change Log

## Version 0.3.1 - 2021-01-07

- [fix] Fix linking of Python libraries

- [fix] Fix missing brace in version generator code

- [fix] Ensure the code builds fine without libdwarf

- [fix] Silence a warning and remove an unused definition

## Version 0.3.0 - 2020-12-30

- [fix] File system images created with versions 0.2.2 and before
  did store symlinks incorrectly. While this was fixed in 0.2.3,
  old images could still not be read correctly. This has now been
  fixed and symlinks on all 0.2.x images will work correctly when
  using the 0.3.0+ FUSE driver.

- [fix] There was no error if the output file could not be written,
  `mkdwarfs` would just fail silently. This has now been fixed.

- [fix] When corrupted compressed blocks in either format (LZ4,
  ZSTD, LZMA) were detected, the FUSE driver would actually show
  the file contents as all zero bytes instead of signaling an I/O
  error. This has been fixes and verified for all formats.

- [fix] Better (hopefully) auto-detection of terminal settings to
  avoid using features like unicode or color when terminals don't
  support them. Fixes github #20.

- [fix] A number of checks has been added to make sure that corrupt
  file system images will not crash the binaries. In order for this
  to be most efficient, old images should be rewritten in the new
  format using:

    mkdwarfs -i old.dwarfs -o new.dwarfs --recompress none

- [compat] The metadata format has changed and new file system
  images can no longer be read by old FUSE drivers.

- [perf] Lots of tweaks and optimizations have resulted in an even
  better compression ratio while at the same time taking less time
  to build file system images. On the 48 GiB Perl dataset, for
  example, the compression improved from 555.7 MiB in 15m12s with
  0.2.3 to 471.6 MiB in 13m59s with 0.3.0.

- [perf] Replace the cyclic hash function with the one used by rsync.
  The rsync hash produces similar results, but it's faster.

- [perf] `mkdwarfs` will now make use of hard link and inode data
  to avoid scanning the same inode multiple times.

- [perf] Segmenting performance has been improved by re-using data
  structures and thus avoiding extra memory allocations.

- [perf] All binaries now use `jemalloc` by default, which uses
  significantly less memory than glibc or tcmalloc, especially
  in the FUSE driver.

- [feature] New file system image format adds integrity checking
  as well as features for easier recovery in case of corruption.
  While currently there is no way to recover a corrupt file system,
  it is important to have the data in place sooner rather than later.

- [feature] New Python scripting support completely replaces Lua
  scripting. The new interface offers a lot more options and should
  be much easier to use.

- [feature] New `nilsimsa` similarity algorithm. This has become
  the default, as it's significantly better on my test data than
  the "simple" `similarity` algorithm.

- [feature] New option `--keep-all-times` to keep atime and ctime
  in addition to just keeping mtime.

- [feature] New option `--time-resolution` that allows to configure
  the resolution with which time stamp are stored.

- [feature] Device, FIFO and socket inodes can now be stored in
  DwarFS file system images. This has to be enabled with the new
  `--with-devices` and `--with-specials` options.

- [feature] The FUSE driver can now optionally expose correct
  hard link counts.

- [feature] `mkdwarfs` now has an option `--remove-empty-dirs` to
  remove empty directories.

- [feature] The FUSE driver has 4 new options to control caching.
  `no_cache_image` will explictly try to release compressed
  blocks from the file system image back to the kernel after
  reading. `cache_image` will keep them in the cache.
  `no_cache_files` will cause decompressed files not to be cached
  by the kernel. `cache_files` will cause them to be cached. The
  defaults are `no_cache_image` and `cache_files`.

- [feature] The FUSE driver now has a `readonly` option that will
  prevent any entries in the mounted file system to show up as
  writeable. This is *not* the default, because it interferes with
  setting up overlays.

- [feature] `dwarfsck` can now dump metadata as JSON blob.

- [feature] `dwarfsck` can now also export raw metadata as JSON.
  The difference to the `--json` option is that this JSON export
  could be used to fully reconstruct the metadata for a DwarFS
  image.

- [feature] More detailed logging and better error handling.

- [test] Added backwards compatibility tests.

- [build] Added `zstd` as a submodule.

- [build] There is now a binary package with statically linked
  binaries available.

- [doc] More accurate list of dependencies.

- [doc] Document how to add `/etc/fstab` entry for DwarFS image.

- [doc] Comparison with wimlib.

- [doc] Comparison with Cromfs.

- [doc] Comparison with EROFS.

- [doc] Updated benchmarks.


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

# dwarfs(1) -- mount highly compressed read-only file system

## SYNOPSIS

`dwarfs` *image* *mountpoint* [*options*...]

## DESCRIPTION

`dwarfs` is the FUSE driver for DwarFS, a highly compressed, read-only file
system. As such, it's similar to file systems like SquashFS, cramfs or CromFS,
but it has some distinct features.

Other than that, it's pretty straightforward to use. Once you've created a
file system image using mkdwarfs(1), you can mount it with:

```
dwarfs image.dwarfs /path/to/mountpoint
```

## OPTIONS

In addition to the regular FUSE options, `dwarfs` supports the following
options:

- `-o cachesize=`*value*:
  Size of the block cache, in bytes. You can append suffixes
  (`k`, `m`, `g`) to specify the size in KiB, MiB and GiB,
  respectively. Note that this is not the upper memory limit
  of the process, as there may be blocks in flight that are
  not stored in the cache. Also, each block that hasn't been
  fully decompressed yet will carry decompressor state along
  with it, which can use a significant amount of additional
  memory. For more details, see mkdwarfs(1).

- `-o blocksize=`*value*:
  Size reported for files in `st_blksize`. You can use this to
  optimize throughput in certain situations.

- `-o readahead=`*value*:
  How much data to read ahead when receiving a read request.
  This is experimental and disabled by default. If you perform
  a lot of large, sequential reads, throughput may benefit from
  enabling readahead.

- `-o workers=`*value*:
  Number of worker threads to use for decompressing blocks.
  If you have a lot of CPUs, increasing this number can help
  speed up access to files in the filesystem.

- `-o uid=`*num*:
  Override the user ID for the whole file system. This option
  is not supported on Windows.

- `-o gid=`*num*:
  Override the group ID for the whole file system. This option
  is not supported on Windows.

- `-o decratio=`*value*:
  The ratio over which a block is fully decompressed. Blocks
  are only decompressed partially, so each block has to carry
  the decompressor state with it until it is fully decompressed.
  However, if a certain fraction of the block has already been
  decompressed, it may be beneficial to just decompress the rest
  and free the decompressor state. This value determines the
  ratio at which we fully decompress the block rather than
  keeping a partially decompressed block. A value of 0.8 means
  that as long as we've decompressed less than 80% of the block,
  we keep the partially decompressed block, but if we've
  decompressed more then 80%, we'll fully decompress it.

- `-o offset=`*value*|`auto`:
  Specify the byte offset at which the filesystem is located in
  the image, or use `auto` to detect the offset automatically.
  This is only useful for images that have some header located
  before the actual filesystem data.

- `-o imagesize=`*value*:
  Explicitly set the size of the filesystem image in bytes,
  starting from the offset. This can be used in cases where
  the image is embedded in a larger file.

- `-o mlock=none`|`try`|`must`:
  Set this to `try` or `must` instead of the default `none` to
  try or require `mlock()`ing of the file system metadata into
  memory.

- `-o readonly`:
  Show all file system entries as read-only. By default, DwarFS
  will preserve the original writability, which is obviously a
  lie as it's a read-only file system. However, this is needed
  for overlays to work correctly, as otherwise directories are
  seen as read-only by the overlay and it'll be impossible to
  create new files even in a writeable overlay. If you don't use
  overlays and want the file system to reflect its read-only
  state, you can set this option.

- `-o case_insensitive`:
  Perform case-insensitive lookups in the mounted file system,
  i.e. an entry originally named `ReadMe.txt` can be accessed as
  `readme.txt`, `README.TXT`, or `rEaDmE.tXt`. This works across
  all platforms. When mounting a file system with many files, this
  may be slightly slower and consume slightly more memory as case-
  insensitive lookup requires an additional mapping table that is
  built on-demand. Note that this is not supported if the file
  system contains directories with entries that only differ in
  case.

- `-o preload_category=`*category*:
  Preload all blocks from this category when mounting the file
  system. This is typically used together with the `mkdwarfs`
  "hotness" categorizer. If the cache size is too small, only as
  many blocks as will fit in the cache will be preloaded.

- `-o preload_all`
  Preload *all* blocks from the file system. This is only useful
  for file systems where all uncompressed blocks fit fully into
  the configured cache size. To see the uncompressed block size,
  you can use `dwarfsck`. If the cache size is too small, only as
  many blocks as will fit in the cache will be preloaded.

- `-o (no_)cache_files`:
  By default, files in the mounted file system will be cached by
  the kernel (i.e. the default is `-o cache_files`). This will
  significantly improve performance when accessing the same files
  over and over again, especially if the data from these files has
  been (partially) evicted from the block cache. By setting the
  `-o no_cache_files` option, you can force the fuse driver to not
  use the kernel cache for file data. If you're short on memory and
  only infrequently accessing files, this can be worth trying, even
  though it's likely that the kernel will already do the right thing
  even when the cache is enabled.

- `-o debuglevel=`*name*:
  Use this for different levels of verbosity along with either
  the `-f` or `-d` FUSE options. This can give you some insight
  over what the file system driver is doing internally, but it's
  mainly meant for debugging and the `debug` and `trace` levels
  in particular will slow down the driver. This defaults to `info`
  in foreground mode (`-f`, `-d`) and to `warn` in background mode.

- `-o analysis_file=`*file*:
  Write the paths of all files that were opened while the file system
  image was mounted to this file. This can be used as a set of "hot"
  files for the `hotness` categorizer in `mkdwarfs`. See the `mkdwarfs`
  documentation for details on producing images optimized for fast
  access times after mounting.

- `-o tidy_strategy=none`|`time`|`swap`:
  Use one of the following strategies to tidy the block cache.
  `none` is the default strategy that never tidies the cache. Blocks
  will only be evicted from the cache if the cache is full and a more
  recently used block is added to the cache. `time` enables a
  time-based tidying strategy. Every `tidy_interval`, the block cache
  is traversed and all blocks that have not been accessed for more
  than `tidy_max_age` will be removed. `swap` enables a swap-based
  tidying strategy. Every `tidy_interval`, the block cache is
  traversed and all blocks that have been fully or partially swapped
  out by the kernel will be removed.

- `-o tidy_interval=`*time*:
  Used only if `tidy_strategy` is not `none`. This is the interval
  at which the cache tidying thread wakes up to look for blocks
  that can be removed from the cache. This must be an integer value.
  Suffixes `ms`, `s`, `m`, `h` are supported. If no suffix is given,
  the value will be assumed to be in seconds.

- `-o tidy_max_age=`*time*:
  Used only if `tidy_strategy` is `time`. A block will be removed
  from the cache if it hasn't been used for this time span. This must
  be an integer value. Suffixes `ms`, `s`, `m`, `h` are supported.
  If no suffix is given, the value will be assumed to be in seconds.

- `-o block_allocator=malloc`|`mmap`:
  Select the allocator for decompressed file system blocks. By default,
  blocks will be allocated using `malloc`. However, depending on the way
  that `malloc` is implemented on your system, you may find that memory
  used by `dwarfs` isn't released despite using cache tidying. In this
  case, using the `mmap` block allocator is much more likely to release
  the memory. Note, however, that the `mmap` allocator can be slower than
  the `malloc` allocator. If your use case causes large numbers of blocks
  to be constantly created/evicted (e.g. you have a huge image and are
  randomly accessing a large fraction of the files), this may impact the
  performance.

- `-o seq_detector=`*num*:
  Threshold, in blocks, for the sequential access detector. If the most
  recently accessed *num* blocks are sequential, then the block following
  the sequence is prefetched. This can significantly increase throughput
  if data is accessed sequentially. A value of `0` completely disables
  detection and prefetching.

- `-o perfmon=`*name*[`+`*name*...]:
  Enable performance monitoring for the list of `+`-separated components.
  This option is only available if the project was built with performance
  monitoring enabled. Available components include `fuse`, `filesystem_v2`,
  `inode_reader_v2` and `block_cache`.

- `-o perfmon_trace=`*file*:
  Write JSON trace data for all components enabled by `--perfmon` to this
  file when the process exits.

- `--man`:
  If the project was built with support for built-in manual pages, this
  option will show the manual page. If supported by the terminal and a
  suitable pager (e.g. `less`) is found, the manual page is displayed
  in the pager.

There's two particular FUSE options that you'll likely need at some
point, e.g. when trying to set up an `overlayfs` mount on top of
a DwarFS image:

- `-o allow_root` and `-o allow_other`:
  These will ensure that the mounted file system can be read by
  either `root` or any other user in addition to the user that
  started the fuse driver. So if you're running `dwarfs` as a
  non-privileged user, you want to `-o allow_root` in case `root`
  needs access, for example when you're trying to use `overlayfs`
  along with `dwarfs`. If you're running `dwarfs` as `root`, you
  need `allow_other`.

## ENVIRONMENT VARIABLES

The `DWARFS_IOLAYER_OPTS` environment variable can be used to configure
certain aspects of the I/O layer used by all DwarFS tools. The value
consists of a comma-separated list of key-value pairs (or just keys for
boolean options). The following options are supported:

- `max_eager_map_size`=*value*:
  The maximum size of a file that will be eagerly mapped into memory
  when opened. Larger files will be accessed using on-demand mappings.
  This is mostly relevant for 32-bit systems, where the address space
  is limited. *value* can be either `unlimited`, a size in bytes, or
  an integer value with a suffix of `k`, `m`, or `g` to indicate
  kibibytes, mebibytes, or gibibytes, respectively. The default is
  `unlimited` on 64-bit systems and 32 MiB on 32-bit systems.

## TIPS & TRICKS

### Adding a DwarFS image to /etc/fstab

This should be relatively straightforward if you're already familiar
with adding other FUSE file systems to `/etc/fstab`. An entry looks
like this:

```
dwarfs#/path/to/image.dwarfs /mnt/mountpoint fuse noauto,defaults,user,cachesize=1g 0 0
```

The first bit before the `#` tells `mount` to look for `mount.dwarfs`,
which is installed as a symbolic link to the DwarFS FUSE driver. The
part after the `#` looks pretty much like any other `fstab` entry.
It starts with the path of the file system image to mount, followed
by the mount point, followed by the file system type (`fuse`), and
finally followed by a set of options.

If you want to automatically mount a DwarFS file system, you'll also
need the `allow_other` option to make sure non-privileged users will
be able to access the data. If you want to work with overlays, you'll
need either `allow_other` or `allow_root`. For any of these options
to work, you will have to set `user_allow_other` in `/etc/fuse.conf`.

### Setting up a writable file system on top of a DwarFS image

This will show you how to set up a read/write layer on top of a
read-only DwarFS image, which can be incredibly handy if you want
to be able to partially and/or temporarily modify/amend the contents
of your read-only image.

My primary use case for this feature is keeping over 1000 Perl
versions in the DwarFS image and then setting up a read/write
layer to be able to install additional modules for each of these
versions. When I didn't need the modules anymore, I could just
completely wipe the read/write layer and get my pristine original
set of Perl versions back.

Here's what you need to do:

- Create a set of directories. In my case, these are all located
  in `/tmp/perl` as this was the original install location.

  ```
  cd /tmp/perl
  mkdir install-ro
  mkdir install-rw
  mkdir install-work
  mkdir install
  ```

- Mount the DwarFS image. `-o allow_root` is needed to make sure
  `overlayfs` has access to the mounted file system. In order
  to use `-o allow_root`, you may have to uncomment or add
  `user_allow_other` in `/etc/fuse.conf`.

  ```
  dwarfs perl-install.dwarfs install-ro -o allow_root
  ```

- Now set up `overlayfs`.

  ```
  sudo mount -t overlay overlay -o lowerdir=install-ro,upperdir=install-rw,workdir=install-work install
  ```

- That's it. You should now be able to access a writeable version
  of your DwarFS image in `install`.

You can go even further than that. Say you have different sets of
modules that you regularly want to layer on top of the base DwarFS
image. In that case, you can simply build a new DwarFS image from
the read-write directory after unmounting the `overlayfs`, and
selectively add this by passing a colon-separated list to the
`lowerdir` option when setting up the `overlayfs` mount:

```
sudo mount -t overlay overlay -o lowerdir=install-ro:install-modules install
```

If you want *this* merged overlay to be writable, just add in the
`upperdir` and `workdir` options from before again.

### Optimizing Performance and Memory Usage

Depending on your use case, you may want to ensure that `dwarfs` isn't
constantly consuming large amounts of memory. Or you may want to make
sure the file system can always be accessed as quickly as possible.
There are several options to tune performance based on your use case.

If you don't care much about memory, use the `cachesize` option to make
sure as many decompressed file system blocks as possible can be kept in
memory.

If your file system image is relatively small, you can also use the
`preload_all` option to immediately populate the cache after mounting.

The more interesting use case is if you want to be conservative about
memory, but still don't want to sacrifice performance too much. Maybe
you only need to access a lot of files directly after mounting and then
only infrequently need to access other files. If this is the case, you
can use the `tidy_strategy`, `tidy_interval` and `tidy_max_age` options.
With these options, you can usually keep the `cachesize` relatively large
in order to maintain good throughput when accessing files, but the cache
will be tidied up quickly, releasing the memory again if it is no longer
accessed. A useful configuration could look like this:

```
dwarfs image mountpoint -otidy_strategy=time,tidy_interval=5s,tidy_max_age=10s
```

This will check the cache every 5 seconds and evict any blocks from the
cache that haven't been accessed for more than 10 seconds. What sounds
good in theory can be tricky in practice: just because `dwarfs` has freed
the memory doesn't necessarily mean that the memory allocator will *really*
return the memory to the system.

If `dwarfs` is built with `jemalloc`, the memory allocator can be tuned
to return memory to the system quickly by setting the `MALLOC_CONF`
environment variable, for example:

```
MALLOC_CONF="background_thread:true,dirty_decay_ms:5000,muzzy_decay_ms:5000"
```

If `dwarfs` is *not* build with `jemalloc`, it is still possible to run it
with `jemalloc` by using `LD_PRELOAD`:

```
LD_PRELOAD=/usr/lib/libjemalloc.so dwarfs image mountpoint ...
```

The exact location of the `jemalloc` shared object depends on your system.
If that is *also* not an option, you can use the `block_allocator` option
to `dwarfs`:

```
dwarfs image mountpoint -oblock_allocator=mmap,tidy_strategy=time,...
```

This will instruct `dwarfs` to *not* use `malloc` for allocating blocks,
but rather use `mmap`. This should work nicely, albeit with some potential
impact on performance, especially with smaller block sizes.

### Optimizing Application Startup Time

If you're using DwarFS as storage for an application container, you may
want to optimize startup time. There are different ways to do that.

If the application is going to read most of the file system image data
during startup, and the image is relatively small, it's worth trying to
just use the `preload_all` option. This will fill the cache with blocks
from the file system image as soon as it is mounted and can already have
a significant impact on startup time.

If the application is only using a small subset of the data in the image
during startup, you can use "hotness" analysis and build an image that
is optimized to improve startup speed. It's basically profile guide
optimization for file systems.

First you have to build an initial image you can use to perform the
analysis. Then, you use the `analysis_file` option to mount the image:

```
dwarfs image mountpoint -oanalysis_file=/tmp/image.prof
```

While the image is mounted, you start the application (or perform
whichever task you want to optimize the file system for). Then, when
you unmount the image, the file you have specified will contain a list
of all paths in the image that have been accessed, in the order in
which the access happened.

You can then use the `hotness` categorizer in `mkdwarfs`, potentially
along with `explicit` ordering, to build the optimized image:

```
mkdwarfs -i input -o image --categorize=hotness --hotness-list=/tmp/image.prof
```

Or, with additional explicit ordering:

```
mkdwarfs -i input -o image --categorize=hotness --hotness-list=/tmp/image.prof \
         --order hotness::explicit:file=/tmp/image.prof
```

This will order the files in the `hotness` category using the same order
as in the profile. Otherwise, they will be ordered by similarity.

Once you have built this optimized image, you can mount it using the
`preload_category` option:

```
dwarfs image mountpoint -opreload_category=hotness
```

This will preload all `hotness` blocks into the cache immediately after
mounting and hopefully speed up application startup significantly.

There are plenty of other ways you can tune how the image is generated.
For example, if the input data already contains compressed files, you
may want to add the `incompressible` categorizer. This will not only
speed up the creation of the file system image as `mkdwarfs` won't waste
time trying to compress incompressible data, but also speed up access as
the data won't need to be decompressed. Also, you could think about using
different compression algorithms for the "hot" and "cold" files, e.g.
something fast like `zstd` for the hot files and `lzma` for the cold files.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

[mkdwarfs(1)](mkdwarfs.md), [dwarfsextract(1)](dwarfsextract.md), [dwarfsck(1)](dwarfsck.md), [dwarfs-format(5)](dwarfs-format.md)

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

- `-o workers=`*value*:
  Number of worker threads to use for decompressing blocks.
  If you have a lot of CPUs, increasing this number can help
  speed up access to files in the filesystem.

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

- `-o mlock=none`|`try`|`must`:
  Set this to `try` or `must` instead of the default `none` to
  try or require `mlock()`ing of the file system metadata into
  memory.

- `-o enable_nlink`:
  Set this option if you want correct hardlink counts for regular
  files. If this is not specified, the hardlink count will be 1.
  Enabling this will slow down the initialization of the fuse
  driver as the hardlink counts will be determined by a full
  file system scan (it only takes about a millisecond to scan
  through 100,000 files, so this isn't dramatic). The fuse driver
  will also consume more memory to hold the hardlink count table.
  This will be 4 bytes for every regular file inode.

- `-o readonly`:
  Show all file system entries as read-only. By default, DwarFS
  will preserve the original writeability, which is obviously a
  lie as it's a read-only file system. However, this is needed
  for overlays to work correctly, as otherwise directories are
  seen as read-only by the overlay and it'll be impossible to
  create new files even in a writeable overlay. If you don't use
  overlays and want the file system to reflect its read-only
  state, you can set this option.

- `-o (no_)cache_image`:
  By default, `dwarfs` tries to ensure that the compressed file
  system image will not be cached by the kernel (i.e. the default
  is `-o no_cache_image`). This will reduce the memory consumption
  of the FUSE driver to slightly more than the `cachesize`, plus
  the size of the metadata block. This usually isn't a problem,
  especially when the image is stored on an SSD, but if you want
  to maximize performance it can be beneficial to use
  `-o cache_image` to keep the compressed image data in the kernel
  cache.

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
  in particular will slow down the driver.

- `-o tidy_strategy=`*name*:
  Use one of the following strategies to tidy the block cache:

  - `none`:
    This is the default strategy that never tidies the cache.
    Blocks will only be evicted from the cache if the cache is
    full and a more recently used block is added to the cache.

  - `time`:
    Time based tidying strategy. Every `tidy_interval`, the block
    cache is traversed and all blocks that have not been accessed
    for more then `tidy_max_age` will be removed.

  - `swap`:
    Swap based tidying strategy. Every `tidy_interval`, the block
    cache is traversed and all blocks that have been fully or
    partially swapped out by the kernel will be removed.

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

## TIPS & TRICKS

### Adding a DwarFS image to /etc/fstab

This should be relatively straightforward if you're already familiar
with adding other FUSE file systems to `/etc/fstab`. An entry looks
like this:

    dwarfs#/path/to/image.dwarfs /mnt/mountpoint fuse noauto,defaults,user,cachesize=1g 0 0

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
  in `/tmp/perl` as this was the orginal install location.

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

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

mkdwarfs(1), dwarfsextract(1), dwarfsck(1), dwarfs-format(5)

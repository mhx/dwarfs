mkdwarfs(1) -- create highly compressed read-only file systems
==============================================================

## SYNOPSIS

`mkdwarfs` -i *path* -o *file* [*options*...]<br>
`mkdwarfs` -i *file* -o *file* --recompress [*options*...]

## DESCRIPTION

**mkdwarfs** allows you to create highly compressed, read-only file systems
in the DwarFS format. DwarFS is similar to file systems like SquashFS,
cramfs or CromFS, but it has some distinct features. For more detail,
see dwarfs(1).

In its simplest usage form, you can create a file system containing the
full contents of `/path/dir` with:

    mkdwarfs -i /path/dir -o image.dwarfs

After that, you can mount it with dwarfs(1):

    dwarfs image.dwarfs /path/to/mountpoint

## OPTIONS

There two mandatory options for specifying the input and output:

  * `-i`, `--input=`*path*|*file*:
    Path to the root directory containing the files from which you want to
    build a filesystem. If the `--recompress` option is given, this argument
    is the source filesystem.

  * `-o`, `--output=`*file*:
    File name of the output filesystem.

Most other options are concerned with compression tuning:

  * `-l`, `--compress-level=`*value*:
    Compression level to use for the filesystem. This is intended to provide
    some sensible defaults and will depend on which compression libraries
    were available at build time. This is meant to be the "easy" interface
    to configure compression, and it will actually pick the defaults for
    three distinct options: `--block-size-bits`, `--compression` and
    `--blockhash-window-sizes`. See the output of `mkdwarfs --help` for a
    table listing the exact defaults used for each compression level.

  * `-S`, `--block-size-bits=`*value*:
    The block size used for the compressed filesystem. The actual block size
    is two to the power of this value. The valid range of this option is from
    12 to 28, i.e. block sizes between 4kiB and 256MiB. Larger block sizes
    will offer better compression, but will be slower and consume more memory
    when actually using the filesystem, as blocks will have to be fully or at
    least partially decompressed into memory. Value between 20 and 24, i.e.
    between 1MiB and 16MiB, are usually a good compromise.

  * `-N`, `--num-workers=`*value*:
    Number of worker threads used for building the filesystem. This defaults
    to the number of processors available on your system. Use this option if
    you want to limit the resources used by `mkdwarfs`.

  * `-L`, `--memory-limit=`*value*:
    Approximately how much memory you want `mkdwarfs` to use during filesystem
    creation. Note that currently this will only affect the block manager
    component, i.e. the number of filesystem blocks that are in flight but
    haven't been compressed and written to the output file yet. So the memory
    used by `mkdwarfs` can certainly be larger than this limit, but it's a
    good option when building large filesystems with expensive compression
    algorithms.

  * `-C`, `--compression=`*algorithm*[:*algopt*[=*value*]]...:
    The compression algorithm and configuration used for creating the new
    filesystem. The value for this option is a colon-separated list. The
    first item is the compression algorithm, the remaining item are its
    options. Options can be either boolean or have a value. For details on
    which algorithms and options are available, see the output of
    `mkdwarfs --help`.

  * `--recompress`:
    Take an existing DwarFS filesystem and recompress it using a different
    compression algorithm. Note that *only* the compression algorithm, i.e.
    the `--compression` option, has an impact on the new filesystem. Other
    options, e.g. `--block-size-bits`, have no impact.

  * `--no-owner`:
    Don't store user/group information in the filesystem. This will make
    the resulting filesystem smaller. This option implies `--no-time`.

  * `--no-time`:
    Don't store timestamp information in the filesystem.

  * `--order`=`none`|`path`|`similarity`:
    The order in which files will be written to the filesystem. Currently,
    the choices are `none`, `path` and `similarity`. With `none`, the files
    will be stored in the order in which they are discovered. With `path`,
    they will be sorted asciibetically by path name. With `similarity`, they
    will be ordered using a similarity hash function. This is the default,
    as it will cause similar files to be located close to each other, which
    means compression will be better.

  * `--blockhash-window-sizes=`*value*[,*value*]...:
    Window sizes used for block hashing. These sizes, separated by commas,
    are again exponents to a base of two. These block hashes are used by
    `mkdwarfs` for finding identical segments in across multiple files.
    This is done on top of duplicate file detection, If a reasonable amount
    of duplicate segments is found, this means less blocks will be used in
    the filesystem and potentially less memory will be used when accessing
    the filesystem. It doesn't necessarily mean that the filesystem will be
    smaller, as this is remove redundany that cannot be exploited by the
    block compression anymore. But it shouldn't make the resulting filesystem
    any bigger. This option is used along with `--window-increment-shift` to
    determine how extensively this segment search will be. The smaller the
    window sizes, the more segments will obviously be found. However, this
    also means files will become more fragmented and thus the filesystem
    can be slower to use. If multiple window sizes are specified, larger
    segments will be searched first, meaning less fragmentation and better
    efficiency. However, multiple window sizes will also make `mkdwarfs`
    slower. It's all a tradeoff, so YMMV.

  * `--window-increment-shift=`*value*:
    This option specifies how many hash values are kept for lookup. It is
    specified in number of right shifts of the window size. To give an
    example, if `--block-hash-window-sizes=16` and `--window-increment-shift=1`,
    then a block hash across 65536 bytes will be stored at every 32768 bytes
    of input data. This means that not every 65536-byte duplicate segment will
    be detected, but duplicate segments of 98304 bytes or more will be detected.
    If you use a larger value for this option, the increments become *smaller*,
    and `mkdwarfs` will be slower and use more memory.

  * `--log-level=`*name*:
    Specifiy a logging level.

  * `--no-progress`:
    Don't show progress output while building filesystem.

  * `--help`:
    Show program help, including defaults, compression level detail and
    supported compression algorithms.

## TIPS & TRICKS

If high compression ratio is your primary goal, definitely go for lzma
compression. However, I've found that it's only about 10% better than
zstd at the highest level. The big advantage of zstd over lzma is that
its decompression speed is about an order of magnitude faster. So if
you're extensively using the compressed file system, you'll probably
find that it's much faster with zstd.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

dwarfs(1)

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
    five distinct options: `--block-size-bits`, `--compression`,
    `--schema-compression`, `--metadata-compression` and
    `--blockhash-window-sizes`. See the output of `mkdwarfs --help` for
    a table listing the exact defaults used for each compression level.

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

  * `-M`, `--max-scanner-workers=`*value*:
    Maximum number of worker threads used for building the filesystem. This
    defaults to the number of processors available on your system, but the
    number of active workers will be automatically adjusted based on load.
    With fast SSDs, scanning multiple files is probably fine, but with older
    spinning disks, having less concurrency can improve overall speed.

  * `-L`, `--memory-limit=`*value*:
    Approximately how much memory you want `mkdwarfs` to use during filesystem
    creation. Note that currently this will only affect the block manager
    component, i.e. the number of filesystem blocks that are in flight but
    haven't been compressed and written to the output file yet. So the memory
    used by `mkdwarfs` can certainly be larger than this limit, but it's a
    good option when building large filesystems with expensive compression
    algorithms.

  * `-C`, `--compression=`*algorithm*[:*algopt*[=*value*]]...:
    The compression algorithm and configuration used for file system data.
    The value for this option is a colon-separated list. The first item is
    the compression algorithm, the remaining item are its options. Options
    can be either boolean or have a value. For details on which algorithms
    and options are available, see the output of `mkdwarfs --help`.

  * `--schema-compression=`*algorithm*[:*algopt*[=*value*]]...:
    The compression algorithm and configuration used for the metadata schema.
    Takes the same arguments as `--compression` above. The schema is *very*
    small, in the hundreds of bytes, so this is only relevant for extremely
    small file systems. The default (`zstd`) has shown to give considerably
    better results than any other algorithms.

  * `--metadata-compression=`*algorithm*[:*algopt*[=*value*]]...:
    The compression algorithm and configuration used for the metadata.
    Takes the same arguments as `--compression` above. The metadata has been
    optimized for very little redundancy and leaving it uncompressed, the
    default for all levels below 8, has the benefit that it can be mapped
    to memory and used directly. This significantly improves mount time for
    large file systems compared to e.g. an lzma compressed metadata block.

  * `--recompress`:
    Take an existing DwarFS filesystem and recompress it using a different
    compression algorithm. Note that *only* the compression algorithm, i.e.
    the `--compression` option, has an impact on the new filesystem. Other
    options, e.g. `--block-size-bits`, have no impact.

  * `--set-owner=`*uid*:
    Set the owner for all entities in the file system. This can reduce the
    size of the file system. If the input only has a single owner already,
    setting this won't make any difference.

  * `--set-group=`*gid*:
    Set the group for all entities in the file system. This can reduce the
    size of the file system. If the input only has a single group already,
    setting this won't make any difference.

  * `--set-time=`*time*|`now`:
    Set the time stamps for all entities to this value. This can significantly
    reduce the size of the file system. You can pass either a unix time stamp
    or `now`.

  * `--time-resolution=`*sec*|`sec`|`min`|`hour`|`day`:
    Specify the resolution with which time stamps are stored. By default,
    time stamps are stored with second resolution. You can specify "odd"
    resolutions as well, e.g. something like 15 second resolution is
    entirely possible. Moving from second to minute resolution, for example,
    will save roughly 6 bits per file system entry in the metadata block.

  * `--keep-all-times`:
    As of release 0.3.0, by default, `mkdwarfs` will only save the contents of
    the `mtime` field in order to save metadata space. If you want to save
    `atime` and `ctime` as well, use this option.

  * `--order=none`|`path`|`similarity`|`nilsimsa`[`:`*limit*[`:`*depth*]]|`script`:
    The order in which inodes will be written to the file system. Choosing `none`,
    the inodes will be stored in the order in which they are discovered. With
    `path`, they will be sorted asciibetically by path name of the first file
    representing this inode. With `similarity`, they will be ordered using a
    simple, yet fast and efficient, similarity hash function. `nilsimsa` ordering
    uses a more sophisticated similarity function that is typically better than
    `similarity`, but is significantly slower to compute. However, computation
    can happen in the background while already building the file system.
    `nilsimsa` ordering can be further tweaked by specifying a *limit* and
    *depth*. The *limit* determines how soon an inode is considered similar
    enough for adding. A *limit* of 255 means "essentially identical", whereas
    a *limit* of 0 means "not similar at all". The *depth* determines up to
    how many inodes can be checked at most while searching for a similar one.
    The default if you omit these values is a *limit* of 255 and a *depth*
    of 20000. Last but not least, if scripting support is built into `mkdwarfs`,
    you can choose `script` to let the script determine the order.

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

  * `--remove-empty-dirs`:
    Removes all empty directories from the output file system, recursively.
    This is particularly useful when using scripts that filter out a lot of
    file system entries.

  * `--with-devices`:
    Include character and block devices in the output file system. These are
    not included by default, and due to security measures in FUSE, they will
    never work in the mounted file system. However, they can still be copied
    out of the mounted file system, for example using `rsync`.

  * `--with-specials`:
    Include named fifos and sockets in the output file system. These are not
    included by default.

  * `--log-level=`*name*:
    Specifiy a logging level.

  * `--no-progress`:
    Don't show progress output while building filesystem.

  * `--help`:
    Show program help, including defaults, compression level detail and
    supported compression algorithms.

If experimental Python support was compiled into `mkdwarfs`, you can use the
following option to enable customizations via the scripting interface:

  * `--script=`*file*[`:`*class*[`(`arguments`...)`]]:
    Specify the Python script to load. The class name is optional if there's
    a class named `mkdwarfs` in the script. It is also possible to pass
    arguments to the constuctor.

## TIPS & TRICKS

### Compression Ratio vs Decompression Speed

If high compression ratio is your primary goal, definitely go for lzma
compression. However, I've found that it's only about 10% better than
zstd at the highest level. The big advantage of zstd over lzma is that
its decompression speed is about an order of magnitude faster. So if
you're extensively using the compressed file system, you'll probably
find that it's much faster with zstd.

### Block, Schema and Metadata Compression

DwarFS filesystems consist of three distinct parts of data. Many blocks,
which store actual file data and are decompressed on demand, as well as
one schema and one metadata section. The schema is tiny, typically less
than 1000 bytes, and holds the details for how to interpret the metadata.
The schema needs to be read into memory once and is subsequently never
accessed again. The metadata itself is usually not compressed, although
it can be if you want to squeeze a few more kilobytes out of the file
system. If it is compressed, it will be fully decompressed into memory.
Otherwise, the metadata part of the file will simply be mapped into memory.
The main difference is that compressed metadata, which being smaller, will
potentially consume more memory and it will definitely take longer to
mount the filesystem initially.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

dwarfs(1)

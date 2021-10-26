# mkdwarfs(1) -- create highly compressed read-only file systems

## SYNOPSIS

`mkdwarfs` `-i` *path* `-o` *file* [*options*...]  
`mkdwarfs` `-i` *file* `-o` *file* `--recompress` [*options*...]

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

- `-i`, `--input=`*path*|*file*:
  Path to the root directory containing the files from which you want to
  build a filesystem. If the `--recompress` option is given, this argument
  is the source filesystem.

- `-o`, `--output=`*file*:
  File name of the output filesystem.

Most other options are concerned with compression tuning:

- `-l`, `--compress-level=`*value*:
  Compression level to use for the filesystem. **If you are unsure, please
  stick to the default level of 7.** This is intended to provide some
  sensible defaults and will depend on which compression libraries were
  available at build time. **The default level has been chosen to provide
  you with the best possible compression while still keeping the file
  system very fast to access.** Levels 8 and 9 will switch to LZMA
  compression (when available), which will likely reduce the file system
  image size, but will make it about an order of magnitude slower to
  access, so reserve these levels for cases where you only need to access
  the data infrequently. This `-l` option is meant to be the "easy"
  interface to configure `mkdwarfs`, and it will actually pick defaults
  for seven distinct options: `--block-size-bits`, `--compression`,
  `--schema-compression`, `--metadata-compression`, `--window-size`,
  `--window-step` and `--order`. See the output of `mkdwarfs --help` for
  a table listing the exact defaults used for each compression level.

- `-S`, `--block-size-bits=`*value*:
  The block size used for the compressed filesystem. The actual block size
  is two to the power of this value. Larger block sizes will offer better
  overall compression ratios, but will be slower and consume more memory
  when actually using the filesystem, as blocks will have to be fully or at
  least partially decompressed into memory. Values between 20 and 26, i.e.
  between 1MiB and 64MiB, usually work quite well.

- `-N`, `--num-workers=`*value*:
  Number of worker threads used for building the filesystem. This defaults
  to the number of processors available on your system. Use this option if
  you want to limit the resources used by `mkdwarfs`.
  This option affects both the scanning phase and the compression phase.
  In the scanning phase, the worker threads are used to scan files in the
  background as they are discovered. File scanning includes checksumming
  for de-duplication as well as (optionally) checksumming for similarity
  computation, depending on the `--order` option. File discovery itself
  is single-threaded and runs independently from the scanning threads.
  In the compression phase, the worker threads are used to compress the
  individual filesystem blocks in the background. Ordering, segmenting
  and block building are, again, single-threaded and run independently.

- `-B`, `--max-lookback-blocks=`*value*:
  Specify how many of the most recent blocks to scan for duplicate segments.
  By default, only the current block will be scanned. The larger this number,
  the more duplicate segments will likely be found, which may further improve
  compression. Impact on compression speed is minimal, but this could cause
  resulting filesystem to be slightly less efficient to use, as single small
  files can now potentially span multiple filesystem blocks. Passing `-B0`
  will completely disable duplicate segment search.

- `-W`, `--window-size=`*value*:
  Window size of cyclic hash used for segmenting. This is again an exponent
  to a base of two. Cyclic hashes are used by `mkdwarfs` for finding
  identical segments across multiple files. This is done on top of duplicate
  file detection. If a reasonable amount of duplicate segments is found,
  this means less blocks will be used in the filesystem and potentially
  less memory will be used when accessing the filesystem. It doesn't
  necessarily mean that the filesystem will be much smaller, as this removes
  redundany that cannot be exploited by the block compression any longer.
  But it shouldn't make the resulting filesystem any bigger. This option
  is used along with `--window-step` to determine how extensive this
  segment search will be. The smaller the window sizes, the more segments
  will obviously be found. However, this also means files will become more
  fragmented and thus the filesystem can be slower to use and metadata
  size will grow. Passing `-W0` will completely disable duplicate segment
  search.

- `-w`, `--window-step=`*value*:
  This option specifies how often cyclic hash values are stored for lookup.
  It is specified relative to the window size, as a base-2 exponent that
  divides the window size. To give a concrete example, if `--window-size=16`
  and `--window-step=1`, then a cyclic hash across 65536 bytes will be stored
  at every 32768 bytes of input data. If `--window-step=2`, then a hash value
  will be stored at every 16384 bytes. This means that not every possible
  65536-byte duplicate segment will be detected, but it is guaranteed that
  all duplicate segments of (`window_size` + `window_step`) bytes or more
  will be detected (unless they span across block boundaries, of course).
  If you use a larger value for this option, the increments become *smaller*,
  and `mkdwarfs` will be slightly slower and use more memory.

- `--bloom-filter-size`=*value*:
  The segmenting algorithm uses a bloom filter to determine quickly if
  there is *no* match at a given position. This will filter out more than
  90% of bad matches quickly with the default bloom filter size. The default
  is pretty much where the sweet spot lies. If you have copious amounts of
  RAM and CPU power, feel free to increase this by one or two and you *might*
  be able to see some improvement. If you're tight on memory, then decreasing
  this will potentially save a few MiBs.

- `-L`, `--memory-limit=`*value*:
  Approximately how much memory you want `mkdwarfs` to use during filesystem
  creation. Note that currently this will only affect the block manager
  component, i.e. the number of filesystem blocks that are in flight but
  haven't been compressed and written to the output file yet. So the memory
  used by `mkdwarfs` can certainly be larger than this limit, but it's a
  good option when building large filesystems with expensive compression
  algorithms. Also note that most memory is likely used by the compression
  algorithms, so if you're short on memory it might be worth tweaking the
  compression options.

- `-C`, `--compression=`*algorithm*[`:`*algopt*[`=`*value*][`,`...]]:
  The compression algorithm and configuration used for file system data.
  The value for this option is a colon-separated list. The first item is
  the compression algorithm, the remaining item are its options. Options
  can be either boolean or have a value. For details on which algorithms
  and options are available, see the output of `mkdwarfs --help`. `zstd`
  will give you the best compression while still keeping decompression
  *very* fast. `lzma` will compress even better, but decompression will
  be around ten times slower.

- `--schema-compression=`*algorithm*[`:`*algopt*[`=`*value*][`,`...]]:
  The compression algorithm and configuration used for the metadata schema.
  Takes the same arguments as `--compression` above. The schema is *very*
  small, in the hundreds of bytes, so this is only relevant for extremely
  small file systems. The default (`zstd`) has shown to give considerably
  better results than any other algorithms.

- `--metadata-compression=`*algorithm*[`:`*algopt*[`=`*value*][`,`...]]:
  The compression algorithm and configuration used for the metadata.
  Takes the same arguments as `--compression` above. The metadata has been
  optimized for very little redundancy and leaving it uncompressed, the
  default for all levels below 7, has the benefit that it can be mapped
  to memory and used directly. This improves mount time for large file
  systems compared to e.g. an lzma compressed metadata block. If you don't
  care about mount time, you can safely choose `lzma` compression here, as
  the data will only have to be decompressed once when mounting the image.

- `--recompress`[`=all`|`=block`|`=metadata`|`=none`]:
  Take an existing DwarFS file system and recompress it using different
  compression algorithms. If no argument or `all` is given, all sections
  in the file system image will be recompressed. Note that *only* the
  compression algorithms, i.e. the `--compression`, `--schema-compression`
  and `--metadata-compression` options, have an impact on how the new file
  system is written. Other options, e.g. `--block-size-bits` or `--order`,
  have no impact. If `none` is given as an argument, none of the sections
  will be recompressed, but the file system is still rewritten in the
  latest file system format. This is an easy way of upgrading an old file
  system image to a new format. If `block` or `metadata` is given, only
  the block sections (i.e. the actual file data) or the metadata sections
  are recompressed. This can be useful if you want to switch from compressed
  metadata to uncompressed metadata without having to rebuild or recompress
  all the other data.

- `-P`, `--pack-metadata=auto`|`none`|[`all`|`chunk_table`|`directories`|`shared_files`|`names`|`names_index`|`symlinks`|`symlinks_index`|`force`|`plain`[`,`...]]:
  Which metadata information to store in packed format. This is primarily
  useful when storing metadata uncompressed, as it allows for smaller
  metadata block size without having to turn on compression. Keep in mind,
  though, that *most* of the packed data must be unpacked into memory when
  reading the file system. If you want a purely memory-mappable metadata
  block, leave this at the default (`auto`), which will turn on `names` and
  `symlinks` packing if these actually help save data.
  Tweaking these options is mostly interesting when dealing with file
  systems that contain hundreds of thousands of files.
  See [Metadata Packing](#metadata-packing) for more details.

- `--set-owner=`*uid*:
  Set the owner for all entities in the file system. This can reduce the
  size of the file system. If the input only has a single owner already,
  setting this won't make any difference.

- `--set-group=`*gid*:
  Set the group for all entities in the file system. This can reduce the
  size of the file system. If the input only has a single group already,
  setting this won't make any difference.

- `--set-time=`*time*|`now`:
  Set the time stamps for all entities to this value. This can significantly
  reduce the size of the file system. You can pass either a unix time stamp
  or `now`.

- `--keep-all-times`:
  As of release 0.3.0, by default, `mkdwarfs` will only save the contents of
  the `mtime` field in order to save metadata space. If you want to save
  `atime` and `ctime` as well, use this option.

- `--time-resolution=`*sec*|`sec`|`min`|`hour`|`day`:
  Specify the resolution with which time stamps are stored. By default,
  time stamps are stored with second resolution. You can specify "odd"
  resolutions as well, e.g. something like 15 second resolution is
  entirely possible. Moving from second to minute resolution, for example,
  will save roughly 6 bits per file system entry in the metadata block.

- `--order=none`|`path`|`similarity`|`nilsimsa`[`:`*limit*[`:`*depth*[`:`*mindepth*]]]|`script`:
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
  To avoid `nilsimsa` ordering to become a bottleneck when ordering lots of
  small files, the *depth* is adjusted dynamically to keep the input queue
  to the segmentation/compression stages adequately filled. You can specify
  how much the *depth* can be adjusted by also specifying *mindepth*.
  The default if you omit these values is a *limit* of 255, a *depth*
  of 20000 and a *mindepth* of 1000. Note that if you want reproducible
  results, you need to set *depth* and *mindepth* to the same value. Also
  note that when you're compressing lots (as in hundreds of thousands) of
  small files, ordering them by `similarity` instead of `nilsimsa` is likely
  going to speed things up significantly without impacting compression too much.
  Last but not least, if scripting support is built into `mkdwarfs`, you can
  choose `script` to let the script determine the order.

- `--remove-empty-dirs`:
  Removes all empty directories from the output file system, recursively.
  This is particularly useful when using scripts that filter out a lot of
  file system entries.

- `--with-devices`:
  Include character and block devices in the output file system. These are
  not included by default, and due to security measures in FUSE, they will
  never work in the mounted file system. However, they can still be copied
  out of the mounted file system, for example using `rsync`.

- `--with-specials`:
  Include named fifos and sockets in the output file system. These are not
  included by default.

- `--header=`*file*:
  Read header from file and place it before the output filesystem image.
  Can be used with `--recompress` to add or replace a header.

- `--remove-header`:
  Remove header from a filesystem image. Only useful with `--recompress`.

- `--log-level=`*name*:
  Specifiy a logging level.

- `--no-progress`:
  Don't show progress output while building filesystem.

- `--progress=none`|`simple`|`ascii`|`unicode`:
  Choosing `none` is equivalent to specifying `--no-progress`. `simple`
  will print a single line of progress information whenever the progress
  has significantly changed, but at most once every 2 seconds. This is
  also the default when the output is not a tty. `unicode` is the default
  behaviour, which shows a nice progress bar and lots of additional
  information. If your terminal cannot deal with unicode characters,
  you can switch to `ascii`, which is like `unicode`, but looks less
  fancy.

- `--help`:
  Show program help, including defaults, compression level detail and
  supported compression algorithms.

If experimental Python support was compiled into `mkdwarfs`, you can use the
following option to enable customizations via the scripting interface:

- `--script=`*file*[`:`*class*[`(`arguments`...)`]]:
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

DwarFS filesystems consist of three distinct parts of data: A potentially
large number of blocks, which store actual file data and are decompressed
on demand, as well as one schema and one metadata section. The schema is
tiny, typically less than 1000 bytes, and holds the details for how to
interpret the metadata. The schema needs to be read into memory once and
is subsequently never accessed again. The metadata itself is compressed
by default, but it doesn't have to be. Actually, if you drop the compression
level from 7 (the default) to 6, the only difference is that the metadata
is left uncompressed. This can be useful if mounting speed of the file
system is important, as the uncompressed metadata part of the file can
then simply be mapped into memory.

### Metadata Packing

The filesystem metadata is stored in [Frozen](https://github.com/facebook/fbthrift/blob/master/thrift/lib/cpp2/frozen/Frozen.h),
a library that allows serialization of structures defined in
[Thrift IDL](https://github.com/facebook/fbthrift/) into an extremely
compact representation that can be used in-place without the need for
deserialization. It is very well suited for persistent, memory-mappable
data. With Frozen, you essentially only pay for what you use: if fields
are defined in the IDL, but they always hold the same value (or are not
used at all), not a single bit will be allocated for this field even if
you have a list of millions of items.

Frozen metadata has relatively low redundancy and doesn't compress well,
but you can still save around 30-50% by enabling compression. However,
this means that upon reading the filesystem, you will first have to
fully decompress the metadata block and keep it in memory. An uncompressed
block could simply be mapped into memory and would be instantly usable.
So if e.g. mounting speed is a concern, it would make sense to disable
metadata compression, in particular for large filesystems.

However, there are several options to choose from that allow you to
further reduce metadata size without having to compress the metadata.
These options are controlled by the `--pack-metadata` option.

- `auto`:
  This is the default. It will enable both `names` and `symlinks`.

- `none`:
  Don't enable any packing. However, string tables (i.e. names and
  symlinks) will still be stored in "compact" rather than "plain"
  format. In order to force storage in plain format, use `plain`.

- `all`:
  Enable all packing options. This does *not* force packing of
  string tables (i.e. names and symlinks) if the packing would
  actually increase the size, which can happen if the string tables
  are actually small. In order to force string table packing, use
  `all,force`.

- `chunk_table`:
  Delta-compress chunk tables. This can reduce the size of the
  chunk tables for large file systems and help compression, however,
  it will likely require a lot of memory when unpacking the tables
  again. Only use this if you know what you're doing.

- `directories`:
  Pack directories table by storing first entry pointers delta-
  compressed and completely removing parent directory pointers.
  The parent directory pointers can be rebuilt by tree traversal
  when the filesystem is loaded. If you have a large number of
  directories, this can reduce the metadata size, however, it
  will likely require a lot of memory when unpacking the tables
  again. Only use this if you know what you're doing.

- `shared_files`:
  Pack shared files table. This is only useful if the filesystem
  contains lots of non-hardlinked duplicates. It gets more efficient
  the more copies of a file are in the filesystem.

- `names`,`symlinks`:
  Compress the names and symlink targets using the
  [fsst](https://github.com/cwida/fsst) compression scheme. This
  compresses each individual entry separately using a small,
  custom symbol table, and it's surprisingly efficient. It is
  not uncommon for names to make up for 50-70% of the metadata,
  and fsst compression typically reduces the size by a factor
  of two. The entries can be decompressed individually, so no
  extra memory is used when accessing the filesystem (except for
  the symbol table, which is only a few hundred bytes). This is
  turned on by default. For small filesystems, it's possible that
  the compressed strings plus symbol table are actually larger
  than the uncompressed strings. If this is the case, the strings
  will be stored uncompressed, unless `force` is also specified.

- `names_index`,`symlinks_index`:
  Delta-compress the names and symlink targets indices. The same
  caveats apply as for `chunk_table`.

- `force`:
  Forces the compression of the `names` and `symlinks` tables,
  even if that would make them use more memory than the
  uncompressed tables. This is really only useful for testing
  and development.

- `plain`:
  Store string tables in "plain" format. The plain format uses
  Frozen thrift arrays and was used in earlier metadata versions.
  It is useful for debugging, but wastes up to one byte per string.

To give you an idea of the metadata size using different packing options,
here's the size of the metadata block for the Ubuntu 20.04.2.0 Desktop
ISO image. There are just over 200,000 files in this image. The ZSTD
and LZMA columns show to what fraction it is possible to reduce the
metadata size by additional compression. That fraction is relative to
the corresponding packing option.

    ---------|---------------|-----------|---------|---------
     Packing | Metadata Size | Relative  | ZSTD    | LZMA
    ---------|---------------|-----------|---------|---------
     auto    |     5,301,177 |   100.00% |  57.33% |  49.29%
     all     |     4,952,859 |    93.43% |  50.46% |  46.45%
     none    |     6,337,294 |   119.55% |  47.70% |  41.37%
     plain   |     6,430,275 |   121.30% |  48.36% |  41.37%
    ---------|---------------|-----------|---------|---------

So the default (`auto`) is roughly 20% smaller than not using any
packing (`none` or `plain`). Enabling `all` packing options doesn't
reduce the size much more. However, it *does* help if you want to
further compress the block. So if you're really desperately trying
to reduce the image size, enabling `all` packing would be an option
at the cost of using a lot more memory when using the filesystem.

## INTERNAL OPERATION

Internally, `mkdwarfs` runs in two completely separate phases. The first
phase is scanning the input data, the second phase is building the file
system.

### Scanning

The scanning process is driven by the main thread which traverses the
input directory recursively and builds an internal representation of the
directory structure. Traversal is breadth-first and single-threaded.

When a regular file is discovered, its hardlink count is checked and
if non-zero, its inode is looked up in a hardlink cache. If the inode
has not been scanned yet, a scanning job will be added to a pool of
`--num-workers` worker threads. These will perform a SHA1 checksum scan
first, which is then used to determine duplicate files, as these will
share the same data in the final DwarFS image. If a file is found not
to be a duplicate, it will now potentially be scanned again (by the
same worker threads and using the same memory mapping) to generate a
similarity hash value. This only happens if `--order` is set to one
of the two similary order modes.

Once all file contents have been scanned by the worker threads, all
unique files will be assigned an internal inode number.

### Building

Building the filesystem image uses a number of separate threads. If
`nilsimsa` ordering is selected, the ordering algorithm runs in its
own thread and continuously emits file inodes. These will be picked
up by the segmenter thread, which scans the inode contents using a
cyclic hash and determines overlapping segments between previously
written data and new incoming data. The segmenter can look at up to
`--max-lookback-block` previous filesystem blocks to find overlaps.

Once the segmenter has produced enough data to fill a filesystem
block, the block is added to a queue where from which the blocks
will be picked up by a pool of `--num-workers` worker threads whose
only job is to compress the block using the `--compression` algorithm.

Blocks that have been compressed will be added to the next queue,
in the original order, and will be picked up by the filesystem writer
thread that will ultimately produce the final filesystem image.

When all data has been segmented, the filesystem metadata is being
finalized and frozen into a compact representation. If metadata
compression is enabled, the metadata is sent to the worker thread
pool.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

dwarfs(1), dwarfsextract(1), dwarfsck(1), dwarfs-format(5)

# mkdwarfs(1) -- create highly compressed read-only file systems

## SYNOPSIS

`mkdwarfs` `-i` *path* `-o` *file*\|`-` [*options*...]  
`mkdwarfs` `--input-list=`*file*\|`-` `-o` *file*\|`-` [*options*...]  
`mkdwarfs` `-i` *file* `-o` *file*\|`-` `--recompress` [*options*...]

## DESCRIPTION

**mkdwarfs** allows you to create highly compressed, read-only file systems
in the DwarFS format. DwarFS is similar to file systems like SquashFS,
cramfs or CromFS, but it has some distinct features. For more detail,
see dwarfs(1).

In its simplest usage form, you can create a file system containing the
full contents of `/path/dir` with:

    mkdwarfs -i /path/dir -o image.dwarfs

After that, you can mount it using dwarfs(1):

    dwarfs image.dwarfs /path/to/mountpoint

## OPTIONS

There are two mandatory options for specifying the input and output:

- `-i`, `--input=`*path*|*file*:
  Path to the root directory containing the files from which you want to
  build a file system. If the `--recompress` option is given, this argument
  is the source filesystem.

- `--input-list=`*file*|`-`:
  Read list of file paths to add to the file system from this file or from stdin.
  The path names will be interpreted relative to the path given with `--input`.
  If `--input` is omitted, the path names will be interpreted relative to the
  current directory. If you want files to be stored in the exact same order
  as read from this list (because, for example, you have already sorted them
  by similarity or access frequency), you must also pass `--order=none`.
  This option implicitly enables both `--with-devices` and `--with-specials`.

- `-o`, `--output=`*file*|`-`:
  File name of the output filesystem or `-` to write the filesystem to stdout.

- `-f`, `--force`:
  Force the output file to be overwritten if it already exists.

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

- `--categorize`[`=`*categorizer*[`,`...]]:
  Enable one or more categorizers in the given order.
  See [CATEGORIZERS](#categorizers) for more details.

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
  you want to limit the resources used by `mkdwarfs` or to optimize build
  speed. This option affects only the compression phase.
  During the compression phase, the worker threads are used to compress the
  individual filesystem blocks in the background. Ordering, segmenting
  and block building are single-threaded and run independently.

- `--compress-niceness=`*value*:
  Set the niceness of compression worker threads. Defaults to 5. This
  ensures the ordering and segmenting threads are prioritised over compression
  as they provide the data to the compression workers. On Windows, the values
  are mapped as follows: 0 (zero) is mapped to "normal" priority, 1 to 5 are
  mapped to "below normal" priority, 6 to 10 are mapped to "lowest" priority
  and values greater than 10 are mapped to "background" priority.

- `--num-scanner-workers=`*value*:
  Number of worker threads used for scanning the filesystem. Use this option
  if you want to limit the resources used by `mkdwarfs` or to optimize build
  speed. This option affects only the scanning phase. By default, the same
  value is used as for `--num-workers`.
  In the scanning phase, the worker threads are used to scan files in the
  background as they are discovered. File scanning includes checksumming
  for de-duplication as well as (optionally) checksumming for similarity
  computation, depending on the `--order` option. File discovery itself
  is single-threaded and runs independently from the scanning threads.

- `--num-segmenter-workers=`*value*:
  Number of worker threads used for segmenting the input data. By default,
  the same value is used as for `--num-workers`.
  Segmenting the input data is one of the most time consuming tasks when
  building a file system, and cannot easily be parallelized. However, when
  using the categorizer, a separate segmenter will be used for each category
  (and subcategory, if present). This option controls how many segmenters
  can run simultaneously. When `--compress-niceness` is set to the default,
  segmenter threads will always have a higher priority than compression
  threads, making sure that compression doesn't slow down segmentation.
  This option also controls the number of threads used for ordering the
  input to the segmenter.

- `-B`, `--max-lookback-blocks=[*category*`::`]`*value*:
  Specify how many of the most recent blocks to scan for duplicate segments.
  By default, only the current block will be scanned. The larger this number,
  the more duplicate segments will likely be found, which may further improve
  compression. Impact on compression speed is minimal, but this could cause
  resulting filesystem to be slightly less efficient to use, as single small
  files can now potentially span multiple filesystem blocks. Passing `-B0`
  will completely disable duplicate segment search.

- `-W`, `--window-size=[*category*`::`]`*value*:
  Window size of cyclic hash used for segmenting. This is an exponent
  to a base of two. Cyclic hashes are used by `mkdwarfs` for finding
  identical segments across multiple files. This is done on top of duplicate
  file detection. If a reasonable amount of duplicate segments is found,
  this means less blocks will be used in the filesystem and potentially
  less memory will be used when accessing the filesystem. It doesn't
  necessarily mean that the filesystem will be much smaller, as this removes
  redundancy that cannot be exploited by the block compression any longer.
  But it shouldn't make the resulting filesystem any bigger. This option
  is used along with `--window-step` to determine how extensive this
  segment search will be. The smaller the window sizes, the more segments
  will obviously be found. However, this also means files will become more
  fragmented and thus the filesystem can be slower to use and metadata
  size will grow. Passing `-W0` will completely disable duplicate segment
  search.

- `-w`, `--window-step=[*category*`::`]`*value*:
  This option specifies how often cyclic hash values are stored for lookup.
  It is specified relative to the window size, as a base-2 exponent that
  divides the window size. As a concrete example, if `--window-size=16`
  and `--window-step=1`, then a cyclic hash across 65536 bytes will be stored
  at every 32768 bytes of input data. If `--window-step=2`, then a hash value
  will be stored at every 16384 bytes. This means that not every possible
  65536-byte duplicate segment will be detected, but it is guaranteed that
  all duplicate segments of (`window_size` + `window_step`) bytes or more
  will be detected (unless they span across block boundaries, of course).
  If you use a larger value for this option, the increments become *smaller*,
  and `mkdwarfs` will be slightly slower and use more memory.

- `--bloom-filter-size`=[*category*`::`]*value*:
  The segmenting algorithm uses a bloom filter to determine quickly if
  there is *no* match at a given position. This will filter out more than
  90% of bad matches quickly with the default bloom filter size. The default
  is pretty much where the sweet spot lies. If you have copious amounts of
  RAM and CPU power, feel free to increase this by one or two and you *might*
  be able to see some improvement. If your system is tight on memory, then
  decreasing this will potentially save a few MiBs.

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

- `-C`, `--compression=`[*category*`::`]*algorithm*[`:`*algopt*[`=`*value*][`:`...]]:
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
  small file systems. The default (`zstd`) has shown to provide considerably
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

- `--history-compression=`*algorithm*[`:`*algopt*[`=`*value*][`,`...]]:
  The compression algorithm and configuration used for the file system
  history. Takes the same arguments as `--compression` above. Like the
  schema, history blocks are typically very small, so the default is the
  same as for schema compression. This is irrelevant if `--no-history`
  is present.

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

- `--recompress-categories=`[`!`]*category*[`,`...]:
  When `--recompress` is set to `all` or `block`, this option controls
  which categories of blocks will be recompressed. Adding a `!` in front
  of the list allows you to specify which categories will *not* be
  recompressed.

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

- `--set-time=now`|*iso-8601-string*|*unix-timestamp*:
  Set the time stamps for all entities to this value. This can significantly
  reduce the size of the file system. You can pass the string `now` for the
  current time, an ISO 8601 string, or a unix timestamp (seconds since epoch).
  The ISO 8601 string supports a space character instead of the `T` between
  date and time and supports dashes as separators. Seconds or the full time
  part may be omitted as long as this doesn't turn the whole string into a
  single number (i.e. `2008-03-17` is supported, but `20080317` is not).

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

- `--chmod=`*mode*[`,`*mode*]...|`norm`:
  Recursively changes the mode bits of all entries in the generated file system.
  Accepts the same mode specifications as the `chmod` utility and additionally
  supports the `D` and `F` modifiers to limit a specification to directories
  or non-directories. As a special case, you can pass `--chmod=norm` to
  "normalize" the permissions across the file system; this is equivalent to
  using `--chmod=ug-st,=Xr`.

- `--order=`[*category*`::`]`none`|`path`|`revpath`|`similarity`|`nilsimsa`[`:`*opt*[`=`*value*][`:`...]]:
  The order in which inodes will be written to the file system. Choosing `none`,
  the inodes will be stored in the order in which they are discovered. With
  `path`, they will be sorted asciibetically by path name of the first file
  representing this inode. With `revpath`, they will also be ordered by path
  name, but the path is being traversed from the leaf to the root, i.e. files
  with the same name will be sorted next to each other. With `similarity`, they
  will be ordered using a simple, yet fast and efficient, similarity hash
  function.
  `nilsimsa` ordering uses a more sophisticated similarity function that is
  typically better than `similarity`, but can be significantly slower to
  determine a good ordering.
  However, the new implementation of this algorithm can be parallelized and
  will perform much better on huge numbers of files. `nilsimsa` ordering can
  be tweaked by specifying `max-children` and `max-cluster-size`. Both options
  determine how the set of files will be split into clusters, each of which will
  be further split recursively. `max-children` is the maximum number of child
  nodes resulting from a clustering step. If `max-children` distinct clusters
  have been found, new files will be added to the closest cluster. `max-cluster-size`
  determines at which point a cluster will no longer be split further. Typically,
  larger values will result in better ordering, but will also make the algorithm
  slower. Unlike the old implementation, `nilsimsa` ordering is now completely
  deterministic.

- `--max-similarity-size=`*value*:
  Don't perform similarity ordering for fragments (or files if they are not split
  into fragments by a categorizer) larger than this size. This helps speed up
  scanning, especially on slow file systems. For large fragments, the gains from
  similarity ordering are relatively small. When this option is set to a non-zero
  value, fragments larger than the limit will be stored first, ordered by size in
  descending order.

- `-F`, `--filter=`*rule*:
  Add a filter rule. This option can be specified multiple times.
  See [FILTER RULES](#filter-rules) for more details.

- `--debug-filter`[`=all`|`=excluded`|`=excluded-files`|`=files`|`=included`|`=included-files`]:
  Show the effect of the filter rules without creating a file system.
  If no argument is passed to the option, all included/excluded files and
  directories are shown (same as with `all`). `files` will omit all
  directories. `included` and `excluded` will only show the corresponding
  set of files/directories. `included-files` and `excluded-files` work
  as before, but again omit all directories.

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

- `--no-section-index`:
  Don't add section index to file system. The section index is usually tiny
  and is used to speed up mount times for large file systems, as it avoids
  a full scan through the file system blocks to figure out their location.

- `--no-history`:
  Don't add any history information to a file system.

- `--no-history-timestamps`:
  Don't add timestamps to history entries. This is necessary, along with
  `--no-create-timestamp`, when trying to produce bit-identical file system
  images.

- `--no-history-command-line`:
  Don't record command line arguments in history entries.

- `--no-create-timestamp`:
  Don't add a creation timestamp. This is useful when bit-identical file
  system images are required to be produced from the same input.

- `--file-hash=none`|*name*:
  Select the hashing function to be used for file deduplication. If `none`
  is chosen, file deduplication is disabled. By default, the built-in
  `XXH3-128` hash is used. This is not a secure hash function, but it is
  significantly faster. The full list of supported hash function depends
  on the version of OpenSSL that the binary is linked against and is shown
  in the output of `mkdwarfs -h`.

- `--log-level=`*name*:
  Specifiy a logging level.

- `--log-with-context`:
  Enable logging context regardless of level. By default, context is enabled
  if the level is `verbose`, `debug` or `trace`.

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

- `--incompressible-min-input-size=`*value*:
  The minimum size of a file to be checked for incompressibility when
  the `incompressible` categorizer is active.

- `--incompressible-block-size=`*value*:
  The block size used to test data for compressibility. This will also
  be the size of the fragments when `--incompressible-fragments` is used.

- `--incompressible-fragments`:
  Categorize individual fragments of a file as incompressible instead of
  only the file as a whole.

- `--incompressible-ratio=`*value*:
  The ratio above which a file or fragment is categorized as `incompressible`.

- `--incompressible-zstd-level=`*value*:
  The ZSTD compression level used for incompressible categorization.

- `-h`, `--help`:
  Show usage and the most common basic options.

- `-H`, `--long-help`:
  Show full usage with all options, including defaults, compression level
  detail and supported compression algorithms.

- `--man`:
  If the project was built with support for built-in manual pages, this
  option will show the manual page. If supported by the terminal and a
  suitable pager (e.g. `less`) is found, the manual page is displayed
  in the pager.

## EXIT CODES

Upon successful completion, `mkdwarfs` will exit with exit code 0. If an
unrecoverable error occurs (i.e. no valid file system image has been produced),
it will exit with exit code 1. If any errors have occurred (e.g. not all input
files could be read), the exit code will be 2.

## CATEGORIZERS

Categorizers will inspect the input files in the scanning phase and try to
assign them a category. Each categorizer can define a set of categories,
and each of these categories can optionally support subcategories.

Running `mkdwarfs` with the `-H` or `--long-help` option will display the
list of available categorizers and the categories they emit. At the moment,
`mkdwarfs` supports two categorizers, `incompressible` and `pcmaudio`. The
`incompressible` categorizer comes with its own set of options while the
`pcmaudio` categorizer doesn't need any further configuration.

Categorizers are only useful if at least some of the `mkdwarfs` configuration
is category-dependent. The options that can be configured per category are
`--compression`, `--order`, `--max-lookback-blocks`, `--window-size`,
`--window-step`, and `--bloom-filter-size`.

The resulting configuration matrix can be quite overwhelming, which is why
`mkdwarfs` will run with a reasonable set of defaults if you specify the
`--categorize` option with no arguments. These defaults are also dependent
on the chosen compression level.

Note that in case of the `pcmaudio` categorizer, you can override each option
per category (in this case `pcmaudio/waveform` and `pcmaudio/metadata`).

It's also worth noting that the order in which the categorizers are given
is important. The first categorizer that will successfully categorize a
file wins and, if possible, no other categorizers will run on the same
file.

### "incompressible" Categorizer

The `incompressible` categorizer will try to compress each input with a
very fast compression algorithm (`zstd` using a negative compression level
by default). If it turns out that this doesn't reduce the size of the input
significantly, the input will be categorized as `incompressible`.

You can use the `incompressible` categorizer in two modes: whole-file or
fragmented categorization. In the former, the whole input file will be
categorized, whereas in the latter, each file can be further broken down
into compressible and incompressible fragments. The size of these fragments
will be equal to the block size used for categorization.

It makes sense to use this categorizer as the last in a list of multiple
categorizers. Not only because it'll likely have the biggest overhead, but
also because it can wrongly classify data as incompressible that can be
compressed properly with a specialized algorithm (e.g. audio data).

### "pcmaudio" Categorizer

The `pcmaudio` categorizer can identify and categorize a wide range of
uncompressed audio data such as `.wav`, `.aiff` and more obscure formats.

It produces two different categories: `pcmaudio/waveform` for the actual
waveform data, and `pcmaudio/metadata` for all other data in the file such
as the file header. The `pcmaudio/waveform` category is again divided into
many subcategories depending on the type of waveform data (e.g. number of
channels, bit depth, byte order, etc.).

In order to efficiently compress `pcmaudio/waveform` data, a suitable
compression algorithm must be selected for this category. `mkdwarfs`
currently supports `flac` compression, which offers the best ratio of
compression speed and achievable compression ratio.

It is worth noting that options such as `--window-size` will operate on
*sample* granularity instead of *byte* granularity when processing
`pcmaudio/waveform` data, where *sample* granularity means one sample
for each channel. For example, a 16-bit stereo file would have a
granularity of 4 bytes and thus `--window-size=10` would refer to a
4 KiB window instead of a 1 KiB windows.

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
data. With Frozen, you essentially only "pay for what you use": if fields
are defined in the IDL, but they always hold the same value (or are not
used at all), not a single bit will be allocated for this field even if
you have a list of millions of items.

Frozen metadata has relatively low redundancy and doesn't compress well,
but you can still save around 30-50% by enabling compression. However,
this means that upon reading the filesystem, you will first have to
fully decompress the metadata block and keep it in memory. An uncompressed
block could simply be mapped into memory and would be instantly usable.
So, if e.g. mounting speed is a concern, it would make sense to disable
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
  enabled by default. For small filesystems, it's possible that
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

So, the default (`auto`) is roughly 20% smaller than not using any
packing (`none` or `plain`). Enabling `all` packing options doesn't
reduce the size much more. However, it *does* help if you want to
further compress the block. So, if you're really desperately trying
to reduce the image size, enabling `all` packing would be an option
at the cost of using a lot more memory when using the filesystem.

### Producing bit-identical images

By default, images produced by `mkdwarfs` will not be identical
over multiple runs. This is due to time stamps that are being added
to the file system image.

In order to produce bit-identical images, you need to pass
`--no-create-timestamp` and either `--no-history-timestamps` or
`--no-history`.

## FILTER RULES

The filter rules have been inspired by the `rsync` utility. These
look very similar, though there are differences. These rules are quite
powerful, yet they're somewhat hard to get used to.

There are only 3 different kinds of rules:

- `+ `*pattern*:
  An "include" rule.

- `- `*pattern*:
  An "exclude" rule.

- `. `*file*:
  A merge file rule. Rules are read (recursively) from the
  specified file.

Ultimately, only include and exclude rules remain in the rule set
as file rules are merged in at the place where they occur.

The most important rule to remember when building a rule set is that
all rules are applied strictly in order and processing stops at the
first matching rule. If no rules match, the default is to include the
entry.

Patterns can be anchored or floating. Anchored patterns are patterns
that start with a `/`. These patterns match relative to the file
system root (i.e. the `--input` path). Floating patterns match in
any directory in the hierarchy.

Patterns ending with a `/` only match directories. All other patterns
only match non-directories.

Note that regardless of the preferred native directory separator,
filter rules will always use UNIX-style directory separators (`/`).
The only exception is the root path component in floating matches,
which will always use native directory separators.

Patterns support `?` and `*` wildcards matching a single character
and any number of characters, respectively. These patterns don't match
across directory separators (`/`).

Patterns also support the `**` wildcard, which matches across directory
separators.

Patterns also support character classes.

Here's an exemplary rule set:

```
+ File/Spec/[EM]*.pm
- unicore/**.pl
+ *.pl
- *
```

This set of rules will include all files matching `File/Spec/[EM]*.pm`
anywhere in the hierarchy. It will also include all `*.pl` files, except
for those anywhere below a `unicore` directory. The last rule excludes
all other files.

This will likely leave a lot of empty directories around, but these can
be removed using `--remove-empty-dirs`.

You can use the `--debug-filter` option to show the sets of included
and excluded files without building an actual file system.

## INTERNAL OPERATION

Internally, `mkdwarfs` runs in two completely separate phases. The first
phase is scanning the input data, the second phase is building the file
system. Both phases try to do as little work as possible, and try to run
as much of the remaining work as possible in parallel, while still making
sure that the file system images produced are reproducible (see the
`--order` option documentation for details on reproducible images).

### Scanning

The scanning process is driven by the main thread which traverses the
input directory recursively and builds an internal representation of the
directory structure. Traversal is breadth-first and single-threaded.
Filter rules as specified by `--filter` are handled immediately during
traversal.

When a regular file is discovered, its hardlink count is checked and
if greater than one, its inode is looked up in a hardlink cache. Another
lookup is performed to see if this is the first file/inode of a particular
size. If it's the first file, we just keep track of the file. If it's not
the first file, we add a job to a pool of `--num-scanner-workers` worker
threads to compute a hash (which hash function is used is determined by
the the `--file-hash` option) of the file. We also add a hash-computing
job for the first file we found with this size earlier. These hashes will
then be used for de-duplicating files. If `--order` is set to one of the
similarity order modes, for each unique file, a further job is added to
the pool to compute a similarity hash. This happens immediately for each
inode of a unique size, but it is guaranteed that duplicates don't trigger
another similarity hash scan (the implementation for this is actually a bit
tricky).

Once all file contents have been scanned by the worker threads, all
unique files will be assigned an internal inode number.

This behaviour can be customized. When using `--file-hash=none`,
de-duplication is completely disabled. Using `--max-similarity-size`,
it is possible to prevent computation of similarity hashes for huge
files. These huge files will then be stored separately before all other
files in the image.

### Building

Building the filesystem image uses a `--num-workers` separate threads.

If `nilsimsa` ordering is selected, the ordering algorithm runs in its
own thread and continuously emits file inodes. These will be picked up
by the segmenter thread, which scans the inode contents using a cyclic
hash and determines overlapping segments between previously written
data and new incoming data. The segmenter will look at up to
`--max-lookback-blocks` previous filesystem blocks to find overlaps.

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
pool for compression.

When using different ordering schemes, the file inodes will be
either sorted upfront, or just sent to the segmenter in the order
in which they were discovered.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

dwarfs(1), dwarfsextract(1), dwarfsck(1), dwarfs-format(5)

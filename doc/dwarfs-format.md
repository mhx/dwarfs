# dwarfs-format(5) -- DwarFS File System Format v2.5

## DESCRIPTION

This document describes the DwarFS file system format, version 2.5.

## TERMINOLOGY

### High-Level Terms

- **DwarFS image** or **DwarFS file system image**: A file that contains
  a DwarFS file system. **DwarFS archive** is another commonly used term.

- **Section**: A contiguous part of a DwarFS image that contains either
  file system data or metadata. Each section has a header with a magic,
  version number, type, length and hashes for integrity checking.

- **Header**: An optional, arbitrary prefix before the first section of a
  DwarFS image. Typically, this is a shell script or other executable that
  intends to use the bundled DwarFS image. The DwarFS binary tools use an
  efficient algorithm to automatically skip this header.

- **Block**: A section of type `BLOCK` that contains file data. There can be
  an arbitrary number of `BLOCK` sections in a DwarFS image.

- **Metadata**: A section of type `METADATA_V2` that contains all metadata
  to interpret the file system. Without the metadata, the structure of the
  file system is completely lost and all you will have is a collection of
  shredded bits and pieces of file data with no association to the original
  files.

- **Inode**: An entry in the `inodes` list in the metadata. Each inode
  represents a file system object, i.e. a directory, regular file, symlink,
  device, socket or pipe.

- **Directory Entry**: An entry in the `dir_entries` list in the metadata.
  Each directory entry associates a name with an inode number. Directory
  entries are grouped by directory using the `directories` list. Also, within
  a single directory, the entries are sorted asciibetically by name.

- **Chunk**: A part of a file that references a contiguous range of bytes
  in a single `BLOCK`. Each regular file inode references a list of chunks
  that, when concatenated, make up the contents of the file.

- **Shared File**: A regular file that shares its contents with one or more
  other regular files. This is similar to hardlinks, but on a "file level"
  instead of an "inode level". Each shared file has its own inode, but the
  inodes reference the same list of chunks through the `shared_files_table`.

### Internal Terms and Types

- **`file_view`**: An abstraction representing a single file in a file system
  and allowing read access to its contents. The exact mechanism to access the
  file contents is implementation-defined. Different implementation have
  different trade-offs in terms of memory usage, speed and error handling.
  For example, the default on 64-bit systems is to memory-map the entire file.
  While this is usually *extremely* fast, there are no means for gracefully
  handling I/O errors and they will typically result in the process crashing
  with a bus error (`SIGBUS`). On 32-bit systems, the default is to only
  memory-map "segments" of the file at a time. This limits the amount of
  address space used, bus uses more system calls and is thus slower. On
  either platform, it is also possible to use an implementation that reads
  data into allocated buffers. This is the slowest option and the one that
  will use the most process memory, but it is the only option that allows for
  graceful handling of I/O errors.

- **`file_extent`**: Using a `file_view`, you can iterate over the "extents"
  of a file. Extents are contiguous ranges of either data or holes. In sparse
  files, holes are ranges of zeros that do not actually occupy any space in the
  file system. This allows the code to efficiently skip over these holes if
  possible.

- **`file_segment`**: A contiguous range of data within a file. You can get
  a `file_segment` either directly from a `file_view` (using offset and size),
  or by using `file_extent::segments()` to iterate over a range of segments
  given the preferred segment size and an optional overlap between segments.
  A `file_segment` is *always* backed by contiguous memory representing the
  corresponding range of data in the file.

- **Fragments** are contiguous ranges of categorized file data. A categorizer
  can split each file into a sequence of fragments, each of which will be
  assigned a category. For example, the `pcmaudio_categorizer` will typically
  split an audio file into a `pcmaudio/metadata` fragment at the start, followed
  by a `pcmaudio/waveform` fragment, optionally followed by `pcmaudio/metadata`
  if there's any trailing metadata. The fragments from each category will be
  processed as separate streams: that is, all `pcmaudio/metadata` fragments will
  be ordered (e.g. by similarity) and the fed into the segmenter, which will
  further split each fragment into "chunks" (these are the chunks that will
  eventually be stored in the `chunk_table` in the metadata).

- **Chunks** are the smallest entity of contiguous file data that DwarFS
  manages. The segmenter splits data into chunks, adding references to data
  it has already seen if possible.

## FILE STRUCTURE

A DwarFS file system image is just a sequence of sections, optionally
prefixed by a "header", which is typically some sort of shell script
or other executable that intends to use the "bundled" DwarFS image.

Each section in the DwarFS image has the following format:

         ┌───┬───┬───┬───┬───┬───┬───┬───┐
    0x00 │'D'│'W'│'A'│'R'│'F'│'S'│MAJ│MIN│  MAJ=0x02, MIN=0x05 for v2.5
         ├───┴───┴───┴───┴───┴───┴───┴───┤
    0x08 │                               │  Used for full (slow) integrity
         ├─ SHA-512/256 integrity hash  ─┤  check with `dwarfsck`.
    0x10 │  over the remainder of the    │
         ├─ section data, starting at   ─┤
    0x18 │  offset 0x28.                 │
         ├─                             ─┤
    0x20 │                               │
         ├───────────────────────────────┤
    0x28 │  XXH3-64 hash over remainder  │  Used for fast integrity check.
         ├───────────────┬───────┬───────┤
    0x30 │Section Number │SecType│CompAlg│  All integer fields are in LE
         ├───────────────┴───────┴───────┤  byte order.
    0x38 │   Length of remaining data    │
         ├───────────────────────────────┤
    0x40 │                               │
         │ Section data compressed using │
         │ CompAlg algorithm.            │
         │                               │
         │                               │
         │                               │
         └───────────────────────────────┘

A couple of notes:

- No padding is added between sections.

- The list of sections can easily be traversed by using the length field
  to skip to the start of the next section.

- Corruption can easily be detected using the XXH3-64 hash. Computation
  of this hash is so fast that it is in fact checked every single time a
  file system section is loaded.

- Integrity can furthermore be checked using the SHA-512/256 hash. This
  is much slower, but should rarely be needed.

- All header fields, except for the magic and version number, are
  protected by the hashes.

- In case of corruption, sections can easily be retrieved by scanning
  for the magic. The version number can be recovered by looking at all
  sections and choosing the majority. The explicit section number helps
  to recover data if multiple sections are missing.

- A major version number change will render the format incompatible.

- A minor version number change will be backwards compatible, i.e. an
  old program will refuse to read a file system with a minor version
  larger than the one it supports. However, a new program can still
  read all file systems with a smaller minor version number, although
  very old versions may at some point no longer be supported.

### Header Detection

In order to access the file system data when it is prefixed by a header,
the size of the header must be known. It can either be given to the
tools or the FUSE driver explicitly (using e.g. the `--image-offset` or
`-o offset` options), or it can be determined automatically (by passing
`auto` as the argument to the aforementioned options).

Automatic detection works by scanning the file for the section header
magic (`DWARFS`) and validating the match by looking up the second
section header using the length of the first section and also checking
its magic. It is rather unlikely that a file is created accidentally
that would pass this check, although one could be crafted manually
without any problems.

### Section Types

Currently, the following different section types are defined:

- `BLOCK` (0):
  A block of data. This is where all file data is stored. There can be an
  arbitrary number of sections of this type. The file data in these `BLOCK`s
  can only be interpreted using the metadata section. The metadata contains
  a list of chunks for each file, each of which references a small part of
  the data in a single `BLOCK`.

- `METADATA_V2_SCHEMA` (7):
  The [schema](https://github.com/facebook/fbthrift/blob/main/thrift/lib/thrift/frozen.thrift)
  used to layout the `METADATA_V2` section contents. This is stored in
  "compact" thrift encoding. The metadata cannot be read without the
  schema, as it defines the exact bit widths used to store each metadata
  field.

- `METADATA_V2` (8):
  This section contains the bulk of the metadata. It's essentially just
  a collection of bit-packed arrays and structures. The exact layout of
  each list and structure depends on the actual data and is stored
  separately in `METADATA_V2_SCHEMA`. The metadata format is defined in
  [metadata.thrift](../thrift/metadata.thrift) and the binary format that
  derives from that definition uses
  [Frozen2](https://github.com/facebook/fbthrift/blob/main/thrift/lib/cpp2/frozen/Frozen.h).
  Frozen2 is not only extremely space efficient, it also allows accessing
  huge data structures directly through memory-mapping.

- `SECTION_INDEX` (9):
  The section index is, well, an index of all sections in the file
  system. If present (creation of the index can be suppressed with
  `--no-section-index`), this is *required* to be the last section.
  Each entry in the section index is a 64-bit value with the upper
  16 bits being the section type and the lower 48 bits being the
  offset relative to the first section. That is, the section index
  is independent of whether or not a header is present before the
  first section. The whole point of the section index is to avoid
  having to build an index by visiting all section headers. Since
  the offsets in the index are sorted, the section index is *always*
  stored uncompressed, and the section index *must* be the last
  section, you can find the start of the section index by reading
  the last 64-bit value from the image file, checking if the upper
  16 bits match the `SECTION_INDEX` type, and then add the image
  offset (header size) to the lower 48 bits. At that position in
  the file, you should find a valid section header for the section
  index.

- `HISTORY` (10):
  File system history information as defined `thrift/history.thrift`.
  This is stored in "compact" thrift encoding. Zero or more history
  sections are supported. This section type is purely informational
  and not needed to read the DwarFS image.

### Compression Algorithms

DwarFS supports a wide range of section compression algorithms, some of
which require additional metadata. The full list of supported algorithms
is defined in [`dwarfs/compression.h`](../include/dwarfs/compression.h).

For compression algorithms with metadata, the metadata is defined in
[`thrift/compression.thrift`](../thrift/compression.thrift). The metadata
is stored in "compact" thrift encoding at the beginning of the section,
just after the header.

## METADATA FORMAT

Here is a high-level overview of how all the bits and pieces relate
to each other:

    ═════════════           ┌─────────────────────────────────────────────────────────────────────────┐
     DwarFS v2.5            │                                                                         │
    ═════════════           │         ┌───────────────────────────────────────────┐                   │
                            │         │                                           │                   │
              dir_entries[] ▼         │              inodes[]                     │   directories[]   │
    ╔════╗   ┌────────────────┐       │  S_IFDIR ──►┌───────────────────┐         │  ┌────────────────┴─┐
    ║root╟──►│ name_index:  0 │       │             │ mode_index:     0 ├──────┐  └─►│ parent_entry:  0 │
    ╚════╝   │ inode_num:   0 ├───────┴────────────►│ owner_index:    0 │      │     │ first_entry:   1 │
             ├────────────────┤                     │ group_index:    0 │      │     | self_entry:    0 |
         ┌───┤ name_index:  2 │                     │ *time_offset: 417 │      │     ├──────────────────┤
    ┌────┼───┤ inode_num:   5 ├───────┐             │ *time_subsec:  13 │      │     │ parent_entry:  0 │
    │    │   ├────────────────┤       │             │ nlink_minus_1:  0 │      │     │ first_entry:  11 │
    │ ┌──┼───┤ name_index:  3 │       │             ├───────────────────┤      │     | self_entry:    1 |
    │ │  │   │ inode_num:   9 ├────┐  │             │        ...        │      │     ├──────────────────┤
    │ │  │   ├────────────────┤    │  │  S_IFLNK ──►├───────────────────┤      │     │ parent_entry:  5 │
    │ │  │   │                │    │  │             │ mode_index:     2 │      │     │ first_entry:  12 │
    │ │  │   │      ...       │    │  └────────────►│ owner_index:    2 │      │     | self_entry:    7 |
    │ │  │   │                │    │                │ group_index:    0 │      │     ├──────────────────┤
    │ │  │   └────────────────┘    │                │ *time_offset: 298 │      │     │       ...        │
    │ │  │                         │                │ *time_subsec:  88 │      │     └──────────────────┘
    │ │  │                         │                │ nlink_minus_1:  0 │      │
    │ │  │    names[]              │                ├───────────────────┤      │      modes[]
    │ │  │   ┌────────────┐        │                │        ...        │      │     ┌─────────────┐
    │ │  │   │ "usr"      │        │     S_IFREG ──►├───────────────────┤      └────►│   0040775   │
    │ │  │   ├────────────┤        │     (unique)   │ mode_index:     1 │            ├─────────────┤
    │ │  │   │ "share"    │        ├───────────────►│ owner_index:    0 ├──────┐     │   0100644   │
    │ │  │   ├────────────┤        │                │ group_index:    0 │      │     ├─────────────┤
    │ │  └──►│ "words"    │        │                │ *time_offset: 298 │      │     │     ...     │
    │ │      ├────────────┤        │                │ *time_subsec:  94 │      │     └─────────────┘
    │ └─────►│ "lib"      │        │                │ nlink_minus_1:  2 │      │
    │        ├────────────┤        │                ├───────────────────┤      │      uids[]
    │        │ "ls"       │        │                │        ...        │      │     ┌─────────────┐
    │        ├────────────┤        │     S_IFREG ──►├───────────────────┤      └────►│       0     │
    │        │    ...     │        │  ┌──(shared)   │ mode_index:     4 │            ├─────────────┤
    ▼        └────────────┘        │  │             │ owner_index:    2 │            │    1000     │
    (inode-off)                    │  │             │ group_index:    1 ├──────┐     ├─────────────┤
    │                              │  │             │ *time_offset: 298 │      │     │     ...     │
    │         symlink_table[]      │  │             │ *time_subsec:  38 │      │     └─────────────┘
    │        ┌────────────┐        │  │             │ nlink_minus_1:  0 │      │
    │        │      1     ├───┐    │  │             ├───────────────────┤      │      gids[]
    │        ├────────────┤   │    │  │             │        ...        │      │     ┌─────────────┐
    └───────►│      0     │   │    │  │  S_IFBLK ──►├───────────────────┤      │     │       0     │
             ├────────────┤   │    │  │  S_IFCHR    │                   │      │     ├─────────────┤
             │    ...     │   │  ┌─┼──┼─────────────┤        ...        │      └────►│     100     │
             └────────────┘   │  │ │  │             │                   │            ├─────────────┤
                              │  │ │  │ S_IFSOCK ──►├───────────────────┤            │     ...     │
                              │  │ │  │  S_IFIFO    │                   │            └─────────────┘
              symlinks[]      │  │ │  │             │        ...        │
             ┌────────────┐   │  │ │  │             │                   │
             │ "../foo"   │   │  │ │  │             └───────────────────┘                 chunks[]
             ├────────────┤   │  │ │  │                                                  ┌──────────────┐
             │ "foo/bar"  │◄──┘  │ │  │                                            ┌────►│ block:     0 │
             ├────────────┤      │ └──┼──────────►(inode-off)                      │     │ offset: 1698 │
             │    ...     │      │    │                │            chunk_table[]  │     │ size:   1012 │
             └────────────┘      ▼    ▼                │           ┌─────────────┐ │     ├──────────────┤
                       (inode-off)    (inode-off)      └──────────►│      0      ├─┘ ┌──►│ block:     0 │
                                 │    │                            ├─────────────┤   │   │ offset: 1604 │
              devices[]          │    │      shared_files_table[]  │      1      ├───┘   │ size:     94 │
             ┌────────────┐      │    │     ┌───────────┐          ├─────────────┤       ├──────────────┤
             │   0x0107   │      │    └────►│     0     ├───┬─────►│      2      ├───┬──►│ block:     0 │
             ├────────────┤      │          ├───────────┤   │      ├─────────────┤   │   │ offset:    0 │
             │   0x0502   │◄─────┘          │     0     ├───┘      │      2      ├───┘   │ size:   1517 │
             ├────────────┤                 ├───────────┤          ├─────────────┤       ├──────────────┤
             │    ...     │                 │    ...    │          │     ...     │       │     ...      │
             └────────────┘                 └───────────┘          └─────────────┘       └──────────────┘

Thanks to the bit-packing, fields that are unused or only contain a
single (zero) value, e.g. a `group_index` that's always zero because
all files belong to the same group, does not occupy any space in the
metadata section.

### Features

Up until v0.7.2, there were quite regular bumps to the minor version
of the DwarFS format. This was a bit annoying, since it meant you could
not upgrade `mkdwarfs` without also upgrading the FUSE driver, as an
old driver would reject new file system images. The v0.7.3 release
made two changes to address this:

- Any new section types would be ignored as long as the minor version
  number is unchanged. This means that new features that require new
  sections can be added without breaking backwards compatibility.

- New features, even ones that break compatibility, can be added without
  the need to bump the minor version number by adding a feature flag in
  the metadata (the `features` set). Each release knows which features
  it can support, and if it encounters a feature it doesn't know, it will
  refuse to mount the file system.

There's one small version gap that needs to be mentioned for completeness.
Since v0.7.3 did *not* increment the minor version, releases v0.7.0 to
v0.7.2 cat accept file system images with any features set. In practice,
however, they will likely bail out because these releases do not accept
unknown section types, and by default `mkdwarfs` has been adding `HISTORY`
sections since v0.7.3. In hindsight, we should have bumped the *accepted*
minor version in v0.7.3, but keep the *written* minor version the same
until actually adding a feature. v0.14.0 finally increments the accepted
minor version, and v0.16.0 will increment the written minor version, so
long term this gap will be closed.

### Determining Inode Offsets

Before you can start traversing the metadata, you need to determine
the offsets for symlinks, regular files, devices etc. in the `inodes`
list. The index into this list is the `inode_num` from `dir_entries`,
but you can perform direct lookups based on the inode number as well.
The `inodes` list is strictly in the following order:

- directory inodes (`S_IFDIR`)
- symlink inodes (`S_IFLNK`)
- regular *unique* file inodes (`S_IFREG`)
- regular *shared* file inodes (`S_IFREG`)
- character/block device inodes (`S_IFCHR`, `S_IFBLK`)
- socket/pipe inodes (`S_IFSOCK`, `S_IFIFO`)

The offsets can thus be found by using a binary search with a
predicate on the inode mode. The shared file offset can be found
by subtracting the length of `shared_files_table` from the total
number of regular files.

### Unique and Shared File Inodes

The difference between *unique* and *shared* file inodes is that
there is only one *unique* file inode that references a particular
index in the `chunk_table`, whereas there are multiple *shared*
file inodes that will reference the same index. This is how DwarFS
implements file-level de-duplication beyond hardlinks. Hardlinks
share the same inode. Duplicate files that are not hardlinked each
have a unique inode, but still reference the same content through
the `chunk_table`.

The `shared_files_table` provides the necessary indirection that
maps a *shared* file inode to a `chunk_table` index.

### Sparse Files

DwarFS can optionally support sparse files since v0.14.0. However, images
created with sparse file support will not be backwards compatible.

Sparse file data is stored as usual. Holes are stored in the `chunk_table`
using a "special" block index. In a file system with N blocks, the highest
valid block index is N-1. Holes are stored using the block index N. This
is cheaper than having to allocate an extra bit per chunk to indicate
whether it's a data or a hole chunk. The block index used to encode a hole
chunk is stored in the metadata as `hole_block_index`.

Since holes can get quite large, their size is encoded in both the `size`
and `offset` fields of the corresponding `chunk_table` entry. While the
`offset` field values are typically randomly distributed, the `size` field
value distribution is usually skewed towards small values. Thus it makes
sense to store the hole size as follows:

```
chunk.offset = hole_size % BLOCK_SIZE
chunk.size   = hole_size / BLOCK_SIZE
```

With large `BLOCK_SIZE` values, the `chunk.size` field will usually occupy
a few bits less than the `chunk.offset` field. In order to prevent a single
large hole from excessively widening the storage used for `chunk.size`, we
use a separate list `large_hole_size` to store the sizes of holes that are
larger than a certain threshold (that is derived from the block size). The
index into this list will be stored in `chunk.size`, indicated by setting
`chunk.offset` to `BLOCK_SIZE - 1`.

### Traversing the Metadata

You typically start at the root directory which is at `dir_entries[0]`,
`inodes[0]` and `directories[0]`. Note that the root directory
implicitly has no name, so that `dir_entries[0].name_index`
should not be used.

To determine the contents of a directory, we determine the range
of entries from `directories[inode_num].first_entry` to
`directories[inode_num + 1].first_entry`. If both values are equal,
the directory is empty. Otherwise, we can look up the entries in
`dir_entries[]`.

So for directory inodes, you can directly index into `directories`
using the inode number.

For link inodes, you can index into `symlink_table`, but you have
to adjust the index for the link inode offset determined before:

    link_index = symlink_table[inode_num - link_inode_offset]

With that, you can look up the contents of the symlink:

    contents = symlinks[link_index]

For *unique* regular file inodes, you can index into `chunk_table`
after adjusting the index:

    chunk_index = inode_num - file_inode_offset

For *shared* regular file inodes, you can index into the (unpacked)
`shared_files_table`:

    shared_index = shared_files[inode_num - file_inode_offset - num_unique_files]

Then, you can index into `chunk_table`, but you need to adjust the
index once more:

    chunk_index = shared_index + num_unique_files

The range of chunks that make up a regular file inode is
`chunk_table[chunk_index]` to `chunk_table[chunk_index + 1]`. If
these values are equal, the file is empty. Otherwise, you need
to look up the range of chunks in `chunks`.

Each chunk references a range of bytes in one file system `BLOCK`.
These need to be concatenated to produce the file contents.

Both `chunk_table` and `directories` have a sentinel entry at the
end to make sure you can perform range lookups for all indices.

Last but not least, to read the device id for a device inode, you
can index into `devices`:

    device_id = devices[inode_num - device_inode_offset]

## OPTIONALLY PACKED STRUCTURES

The overview above assumes metadata without any additional packing,
which can be produced using:

    mkdwarfs --pack-metadata=none,plain

However, this isn't the default, and parts of the metadata are
likely stored in a packed format. These are mostly easy to unpack.

### Shared Files Table Packing

The `shared_files_table` can be stored in a packed format that
only encodes the number of shared links to a `chunk_table` index.
As the minimum number of links is always 2 (otherwise it wouldn't
be shared), the numbers in the packed format are additionally
offset by 2. So for example, a packed table like

    [0, 3, 1, 0, 1]

would unpack to:

    [0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4]

The packed format is used when `options.packed_shared_files_table`
is true.

### Directories Packing

The `directories` table, when stored in packed format, omits
all `parent_entry` fields and uses delta compression for the
`first_entry` fields.

In order to unpack all information, you first have to delta-
decompress the `first_entry` fields, then traverse the whole
directory tree once to fill in the `parent_entry` fields.
This sounds like a lot of work, but it's actually reasonably
fast. For example, for a file system with 15 million entries
in 90,000 directories, reconstructing the `directories` takes
only about 50 milliseconds.

The packed format is used when `options.packed_directories`
is true.

### Chunk Table Packing

The `chunk_table` can also be stored delta-compressed and
must be unpacked accordingly.

The packed format is used when `options.packed_chunk_table`
is true.

### Hardlink Count Table

This isn't really "packing" per se, but more of an optimization.
Hardlink counts (i.e. `stat.st_nlink`) can always be reconstructed
by traversing the `dir_entries` and counting how many times each
inode is referenced. This is reasonably fast, but can still cause
a noticeable delay when mounting file systems with millions of
entries (e.g. about 300ms for 10 million entries). It also uses
4 bytes per inode temporarily to store these counts.

However, correct hardlink counts are necessary, otherwise the fact
that two directory entries reference the same inode can be very
confusing to applications.

So by default, unless `--no-hardlink-table` is passed to `mkdwarfs`,
the `nlink_minus_one` field will be populated for each inode. The
`inodes_have_nlink` option will signal whether or not this field
is valid. If it is *not* valid, the hardlink count table will be
built each time the file system is mounted.

The reason for calling the field `nlink_minus_one` is that inodes
must have at least one link, and storing *only* zeroes for a field
requires no space at all. So, if the input to `mkdwarfs` has no
hardlinks, there's no metadata overhead at all. (And that's what
the `inodes_have_nlink` option is for: it helps determine if the
fields are all zero because they were never set, or because the
hardlink counts are actually all one.)

### Names and Symlinks String Table Packing

Both the `names` and `symlinks` tables can be stored in a
packed format in `compact_names` and `compact_symlinks`.

There are two separate packing schemes which can be combined.
If none of these schemes is active, the difference between
e.g. `names` and `compact_names` is that the former is stored
as a "proper" list, whereas the latter is stored as a single
string plus an index of offsets. As lists of strings store
both offset and length for each element, this already saves
the storage for the length fields, which can easily be
determined from the offsets at run-time.

If the `packed_index` scheme is used in addition, the index
is stored delta-compressed.

Last but not least, the individual strings can be compressed
as well. The [fsst library](https://github.com/cwida/fsst)
allows for compression of short strings with random access
and is typically able to reduce the overall size of the
string tables by 50%, using a dictionary that is only a few
hundred bytes long. If a `symtab` is set for the string table,
this compression is used. Note that in this case, the index refers
to the position and length in the compressed string.

It's recommended to decompress the strings using the official FSST
code, which is highly optimized and very fast. The rest of this
section explains how to decompress the names manually, just to
document all parts of the DwarFS format; you can skip to the next
section if you're using the `fsst` library.

Decoding an FSST compressed string is very simple: each byte is a
code, codes `00` to `FE` are an index into a string table, with each
entry being a string 1-8 bytes in length, which are copied into the
target buffer; code `FF` is an escape character, indicating that the
following byte should be copied into the target buffer as-is. For
example, if the contents of the string table were
`["FS", "war", "!", "D"]`, the byte sequence `03 01 00 02` decodes
to `"DwarFS!"`.

The string table is constructed from the `symtab` record as follows:

Bytes 0 to 7 are the header and are of the form `01 ll xx xx 0A 14 34 01`;
bytes 2 and 3 store encoder parameters; they can be ignored. Byte 1
(`ll`) stores the number of codes, this information is redundant and
can also be ignored, but it could be used as an additional sanity check
if so desired. The rest is fixed and if any of those bytes are
different, `symtab` should be considered to be corrupt.

Byte 8 should always be `00`. If it's `01`, it means the data was
compressed as zero-terminated strings and requires slightly different
decoding of `symtab` (described at the end of this section for
completeness); DwarFS doesn't use zero-terminated strings; hence, in
the following paragraphs we assume it's zero.

Bytes 9 to 16 are the histogram of string lengths in the string table:
byte 9 is the number of 1-byte strings, byte 10 the number of 2-byte
strings and so on. The sum of those values is always identical to
byte 1 in the header.

The remaining bytes are the contents of the string table, but it
starts with all the 2-byte strings concatenated, followed by all
3-byte strings, and so on until the 8-byte strings are followed by all
1-byte strings. The indices are assigned sequentially: the first entry
read gets index 0, the second index 1, and so on.

For example, the string table used earlier would be stored as follows:
the histogram would start with `02 01 01` with the rest being zeros,
followed by the ASCII string `"FSwar!D"`.

This concludes the description of FSST decompression. As mentioned
earlier, byte 8 in the header should always be `00`, but if it were
`01`, there are only 2 differences that need to be noted: entry zero
in the string table is fixed to the 1-byte `"\0"` string, and that
value is not stored in the `symtab`. However it is still counted in
the histogram; so to construct the string table, the first code
assigned has to be 1, not 0 and the first entry in the histogram has
to be decremented by 1.

### Binary Metadata Format Details

The binary metadata is stored using
[Frozen2](https://github.com/facebook/fbthrift/blob/main/thrift/lib/cpp2/frozen/Frozen.h).
This format is, unfortunately, not really documented. Also, as of now,
there is only a C++ implementation to read or write this format.

To interpret the binary data in the `METADATA_V2` section, both the thrift
definitions in [`metadata.thrift`](../thrift/metadata.thrift) and the
[schema](https://github.com/facebook/fbthrift/blob/main/thrift/lib/thrift/frozen.thrift)
from the `METADATA_V2_SCHEMA` section are needed.

You can inspect the schema using `dwarfsck` in two different ways.
First, as a "raw" schema dump:

```
$ dwarfsck image.dwarfs -d schema_raw_dump
Schema {
  4: fileVersion (i32) = 1,
  1: relaxTypeChecks (bool) = true,
  2: layouts (map) = map<i16,struct>[44] {
    0 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 6,
      3: fields (map) = map<i16,struct>[0] {
      },
      4: typeName (string) = "",
    },
    1 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 5,
      3: fields (map) = map<i16,struct>[0] {
      },
      4: typeName (string) = "",
    },
    2 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 12,
      3: fields (map) = map<i16,struct>[0] {
      },
      4: typeName (string) = "",
    },
    3 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 11,
      3: fields (map) = map<i16,struct>[0] {
      },
      4: typeName (string) = "",
    },
    4 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 23,
      3: fields (map) = map<i16,struct>[2] {
        2 -> Field {
          1: layoutId (i16) = 2,
          2: offset (i16) = 0,
        },
        3 -> Field {
          1: layoutId (i16) = 3,
          2: offset (i16) = -12,
        },
      },
      4: typeName (string) = "",
    },
    5 -> Layout {
      1: size (i32) = 0,
      2: bits (i16) = 11,
      3: fields (map) = map<i16,struct>[3] {
        1 -> Field {
          1: layoutId (i16) = 0,
          2: offset (i16) = -5,
        },
        2 -> Field {
          1: layoutId (i16) = 1,
          2: offset (i16) = 0,
        },
        3 -> Field {
          1: layoutId (i16) = 4,
          2: offset (i16) = 0,
        },
      },
      4: typeName (string) = "",
    },
[...]
    43 -> Layout {
      1: size (i32) = 36,
      2: bits (i16) = 282,
      3: fields (map) = map<i16,struct>[19] {
        1 -> Field {
          1: layoutId (i16) = 5,
          2: offset (i16) = 0,
        },
        2 -> Field {
          1: layoutId (i16) = 8,
          2: offset (i16) = -11,
        },
        3 -> Field {
          1: layoutId (i16) = 12,
          2: offset (i16) = -23,
        },
[...]
      },
      4: typeName (string) = "",
    },
  },
  3: rootLayout (i16) = 43,
}
```

To make *any* sense of this, you need to look at the
[`metadata.thrift`](../thrift/metadata.thrift) with the explicit knowledge
that the `rootLayout` in the schema refers to the `struct metadata` in the
thrift IDL. With that in mind, you can now see that the `struct metadata`
itself uses 36 bytes (or 282 bits) of storage. By definition, these bytes
are located at the start of the `METADATA_V2` section data. Note that these
sizes are *solely* defined by the schema; another DwarFS image may store
the `struct metadata` in fewer or more bits.

You can also line up the `fields` map in the `Layout` of `struct metadata`
with the fields from the thrift IDL. While the *names* of the struct members
can change, the numeric id *never* changes. So you can see that field `1`
refers to the `chunks` member. You can also see that the layout for that
field is `5`, which can be looked up again in the `layouts` map of the schema.

The tricky bit is that layout `5` does *not* refer to the `struct chunk` in
the IDL, but *actually* to the `list<chunk>`. A `list` (or an `ArrayLayout`
in Frozen2) is represented using 3 fields: `distance` (`1`), `count` (`2`)
and `item` (`3`). `count` is just the actual length of the list/array/vector.
`distance` is the offset at which the data for the list starts. And `item`
finally refers to the layout for the `struct chunk`, in this case `4`.

Layout `4` contains 2 out of the 3 members of `struct chunk`: `offset` (`2`)
and `size` (`3`). The first member, `block`, is missing simply because there
is only one block in the DwarFS image we're looking at. Thus, no bits are
used to represent the `block` member in `struct chunk`. For `offset`, 12 bits
are allocated per item and for `size`, 11 bits are allocated.

Now, if we look at a hex dump of the `METADATA_V2` section, we have enough
context to navigate the data:

```
            v offset 0
            91 ac 55 b6  3e 2b 1a b2 c8 24 69 92  |......U.>+...$i.|
             |  |
             |  `-- 0b10101100
             |     vvv     ^^^ -> 0b100100 = distance = 36
             `-- 0b10010001
                      ^^^^^ count = 17

be 82 f7 0b 00 00 73 fa  c3 2e db 6e 4b 7e 17 3e  |......s....nK~.>|

                         v offset 36
6c 0d 77 b9 51 ef eb 02  a6 2a 00 4b 15 40 2d d0  |l.w.Q....*.K.@-.|
                          |  |  |
                          |  |  `- 0b00000000
                          |  `---- 0b00101010  0b00000000010 = size = 2
                          `------- 0b10100110  0b101010100110 = offset = 2726

0f 53 05 80 aa 02 70 55  04 88 aa 00 3c 55 00 aa  |.S....pU....<U..|
```

The bits are read starting from the LSB of the first byte (i.e. little-
endian). We know that the data starts with the root layout, and the
root layout starts with the `ArrayLayout` for `list<chunk>`. We know
that the `count` is represented using 5 bits starting at offset 0.
Reading the actual bits, we find that there are 17 chunks stored in
the metadata. Reading the 6 `distance` bits starting at an offset of
5 bits (negative offsets are "bits", while positive offsets are "bytes"),
we find that the 17 chunks are stored starting at the 36th byte.

If we move to that location and read 12 bits for the chunk `offset` and
11 bits of the chunk `size`, we find that the first chunk is 2 bytes
from offset 2726 in block 0.

Another option to look at the schema is via `frozen_layout`:

```
$ dwarfsck image.dwarfs -d frozen_layout
36 byte (with 282 bits) ::dwarfs::thrift::metadata::metadata
  chunks @ start
    11 bit range of std::vector<dwarfs::thrift::metadata::chunk, std::allocator<dwarfs::thrift::metadata::chunk> >
      distance @ bit 5
        6 bit packed unsigned unsigned long
      count @ start
        5 bit packed unsigned unsigned long
      item @ start
        23 bit ::dwarfs::thrift::metadata::chunk
          block @ start
            empty packed unsigned unsigned int
          offset @ start
            12 bit packed unsigned unsigned int
          size @ bit 12
            11 bit packed unsigned unsigned int
  directories @ bit 11
    12 bit range of std::vector<dwarfs::thrift::metadata::directory, std::allocator<dwarfs::thrift::metadata::directory> >
      distance @ bit 5
        7 bit packed unsigned unsigned long
      count @ start
        5 bit packed unsigned unsigned long
      item @ start
        12 bit ::dwarfs::thrift::metadata::directory
          parent_entry @ start
            6 bit packed unsigned unsigned int
          first_entry @ bit 6
            6 bit packed unsigned unsigned int
          self_entry @ start
            empty packed unsigned unsigned int
[...]
```

This makes a lot more sense now that we've already looked at the raw schema
dump. This representation already associates the types from the thrift IDL
with the layouts in the schema.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

[mkdwarfs(1)](mkdwarfs.md), [dwarfs(1)](dwarfs.md), [dwarfsextract(1)](dwarfsextract.md), [dwarfsck(1)](dwarfsck.md)

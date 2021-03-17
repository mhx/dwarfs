# DwarFS File System Format v2.3

## File Structure

A DwarFS file system image is just a sequence of blocks. Each block has the
following format:

         ┌───┬───┬───┬───┬───┬───┬───┬───┐
    0x00 │'D'│'W'│'A'│'R'│'F'│'S'│MAJ│MIN│  MAJ=0x02, MIN=0x03 for v2.3
         ├───┴───┴───┴───┴───┴───┴───┴───┤
    0x08 │                               │  Used for full (slow) integrity
         ├─ SHA-512/256 integrity hash  ─┤  check with `dwarfsck`.
    0x10 │  over the remainder of the    │
         ├─ block data, starting at     ─┤
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

- No padding is added between blocks.

- The list of blocks can easily be traversed by using the length field
  to skip to the start of the next section.

- Corruption can easily be detected using the XXH3-64 hash. Computation
  of this hash is so fast that it is in fact checked every single time a
  file system block is loaded.

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
  larger than the one it supports. However, a new program will still
  read all file systems with a smaller minor version number.


### Section Types

There are currently 3 different section types.

#### `BLOCK` (0)

A block of data. This is where all file data is stored. There can be
an arbitrary number of blocks of this type.

#### `METADATA_V2_SCHEMA` (7)

The schema used to layout the `METADATA_V2` block contents. This is
stored in "compact" thrift encoding.

#### `METADATA_V2` (8)

This section contains the bulk of the metadata. It's essentially just
a collection of bit-packed arrays and structures. The exact layout of
each list and structure depends on the actual data and is stored
separately in `METADATA_V2_SCHEMA`.

Here is a high-level overview of how all the bits and pieces relate
to each other:

    ═════════════           ┌─────────────────────────────────────────────────────────────────────────┐
     DwarFS v2.3            │                                                                         │
    ═════════════           │         ┌───────────────────────────────────────────┐                   │
                            │         │                                           │                   │
              dir_entries[] ▼         │              inodes[]                     │   directories[]   │
    ╔════╗   ┌────────────────┐       │  S_IFDIR ──►┌───────────────────┐         │  ┌────────────────┴─┐
    ║root╟──►│ name_index:  0 │       │             │ mode_index:     0 ├──────┐  └─►│ parent_entry:  0 │
    ╚════╝   │ inode_num:   0 ├───────┴────────────►│ owner_index:    0 │      │     │ first_entry:   1 │
             ├────────────────┤                     │ group_index:    0 │      │     ├──────────────────┤
         ┌───┤ name_index:  2 │                     │ atime_offset:   0 │      │     │ parent_entry:  0 │
    ┌────┼───┤ inode_num:   5 ├───────┐             │ mtime_offset: 417 │      │     │ first_entry:  11 │
    │    │   ├────────────────┤       │             │ ctime_offset:   0 │      │     ├──────────────────┤
    │ ┌──┼───┤ name_index:  3 │       │             ├───────────────────┤      │     │ parent_entry:  5 │
    │ │  │   │ inode_num:   9 ├────┐  │             │        ...        │      │     │ first_entry:  12 │
    │ │  │   ├────────────────┤    │  │  S_IFLNK ──►├───────────────────┤      │     ├──────────────────┤
    │ │  │   │                │    │  │             │ mode_index:     2 │      │     │                  │
    │ │  │   │      ...       │    │  └────────────►│ owner_index:    2 │      │     │       ...        │
    │ │  │   │                │    │                │ group_index:    0 │      │     │                  │
    │ │  │   └────────────────┘    │                │ atime_offset:   0 │      │     └──────────────────┘
    │ │  │                         │                │ mtime_offset: 298 │      │
    │ │  │                         │                │ ctime_offset:   0 │      │
    │ │  │    names[]              │                ├───────────────────┤      │      modes[]
    │ │  │   ┌────────────┐        │                │        ...        │      │     ┌─────────────┐
    │ │  │   │ "usr"      │        │     S_IFREG ──►├───────────────────┤      └────►│   0040775   │
    │ │  │   ├────────────┤        │     (unique)   │ mode_index:     1 │            ├─────────────┤
    │ │  │   │ "share"    │        ├───────────────►│ owner_index:    0 ├──────┐     │   0100644   │
    │ │  │   ├────────────┤        │                │ group_index:    0 │      │     ├─────────────┤
    │ │  └──►│ "words"    │        │                │ atime_offset:   0 │      │     │     ...     │
    │ │      ├────────────┤        │                │ mtime_offset: 298 │      │     └─────────────┘
    │ └─────►│ "lib"      │        │                │ ctime_offset:   0 │      │
    │        ├────────────┤        │                ├───────────────────┤      │      uids[]
    │        │ "ls"       │        │                │        ...        │      │     ┌─────────────┐
    │        ├────────────┤        │     S_IFREG ──►├───────────────────┤      └────►│       0     │
    │        │    ...     │        │  ┌──(shared)   │ mode_index:     4 │            ├─────────────┤
    ▼        └────────────┘        │  │             │ owner_index:    2 │            │    1000     │
    (inode-off)                    │  │             │ group_index:    1 ├──────┐     ├─────────────┤
    │                              │  │             │ atime_offset:   0 │      │     │     ...     │
    │         symlink_table[]      │  │             │ mtime_offset: 298 │      │     └─────────────┘
    │        ┌────────────┐        │  │             │ ctime_offset:   0 │      │
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
             └────────────┘      │    ▼                │           ┌─────────────┐ │     ├──────────────┤
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
all files belong to the same group, do not occupy any space.

Before you can start traversing the metadata, you need to determine
the offsets for symlinks, regular files, devices etc. in the `inodes`
list. The index into this list is the `inode_num` from `dir_entries`,
but you can perform direct lookups based on the inode number as well.
The `inodes` list is strictly in the following order:

* directory inodes (`S_IFDIR`)

* symlink inodes (`S_IFLNK`)

* regular *unique* file inodes (`S_IREG`)

* regular *shared* file inodes (`S_IREG`)

* character/block device inodes

* socket/pipe inodes

The offsets can thus be found using a simple binary search.

The difference between *unique* and *shared* file inodes is that
there is only one *unique* file inode that references a particular
index in the `chunk_table`, whereas there are multiple *shared*
file inodes that will reference the same index. This is how DwarFS
implements file-level de-duplication beyond hardlinks. Hardlinks
share the same inode. Duplicate files that are not hardlinked all
have a unique inode, but still reference the same content through
the `chunk_table`.

The `shared_files_table` provides the necessary indirection that
maps a *shared* file inode to a `chunk_table` index. However, the
`shared_files_table` is stored in a packed format that only encodes
the number of shared links to a `chunk_table` index, so it must be
unpacked first.

Once the offsets have been determined and the `shared_files_table`
is unpacked, you can start traversing the metadata. Typically, you
would start a the root directory which is at `dir_entries[0]`,
`inodes[0]` and `directories[0]`. Note that the root directory
implicitly has no name, so that `dir_entries[0].name_index`
shouldn't be used.

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

For *shared* regular file inodes, you can index into the unpacked
`shared_files_table`:

    shared_index = shared_files[inode_num - file_inode_offset - num_unique_files]

The, you can index into `chunk_table`, but you need to adjust the
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

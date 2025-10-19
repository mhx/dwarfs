# dwarfs-env(7) -- DwarFS Environment Variables

## DESCRIPTION

The DwarFS tools use the following environment variables to configure
certain aspects of their behavior.

### DWARFS\_IOLAYER\_OPTS

The `DWARFS_IOLAYER_OPTS` environment variable can be used to configure
certain aspects of the I/O layer used by all DwarFS tools. The value
consists of a comma-separated list of key-value pairs (or just keys for
boolean options). The following options are supported:

- `max_eager_map_size=`*value*:
  The maximum size of a file that will be eagerly mapped into memory
  when opened. Larger files will be accessed using on-demand mappings.
  This is mostly relevant for 32-bit systems, where the address space
  is limited. *value* can be either `unlimited`, a size in bytes, or
  an integer value with a suffix of `k`, `m`, or `g` to indicate
  kibibytes, mebibytes, or gibibytes, respectively. The default is
  `unlimited` on 64-bit systems and 32 MiB on 32-bit systems.

- `open_mode=mmap`|`read`:
  Chooses between memory-mapped I/O (`mmap`) and using "standard" read
  calls (`pread`) to access file data. The default is `mmap`, which is
  generally more efficient. If you have trouble with e.g. unreliable
  hardware or unstable network filesystems, this can easily cause a
  process accessing a memory-mapped file to crash with a bus error
  (`SIGBUS`). In such cases, you can try switching to `read` mode, which
  should prevent such crashes and deliver proper I/O error messages that
  can be useful for finding the root cause of the problem.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

[mkdwarfs(1)](mkdwarfs.md), [dwarfsextract(1)](dwarfsextract.md), [dwarfsck(1)](dwarfsck.md), [dwarfs(1)](dwarfs.md)

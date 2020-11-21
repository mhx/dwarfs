dwarfs(1) -- mount highly compressed read-only file system
==========================================================

## SYNOPSIS

`dwarfs` image mountpoint [<options>...]

## DESCRIPTION

`dwarfs` is the FUSE driver for DwarFS, a highly compressed, read-only file
system. As such, it's similar to file systems like SquashFS, cramfs or CromFS,
but it has some distinct features. For a comparison, see [COMPARISON].

Other than that, it's pretty straightforward to use. Once you've created a
file system image using mkdwarfs(1), you can mount it with:

    dwarfs image.dwarfs /path/to/mountpoint

## OPTIONS

In addition to the regular FUSE options, `dwarfs` supports the following
options:

  * `-o cachesize=`<value>:
    Size of the block cache, in bytes. You can append suffixes
    (`k`, `m`, `g`) to specify the size in KiB, MiB and GiB,
    respectively. Note that this is not the upper memory limit
    of the process, as there may be blocks in flight that are
    not stored in the cache. Also, each block that hasn't been
    fully decompressed yet will carry decompressor state along
    with it, which can use a significant amount of additional
    memory. For more details, see mkdwarfs(1).

  * `-o workers=`<value>:
    Number of worker threads to use for decompressing blocks.
    If you have a lot of CPUs, increasing this number can help
    speed up access to files in the filesystem.

  * `-o decratio=`<value>:
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

  * `-o debuglevel=`<name>:
    Use this for different levels of verbosity along with either
    the `-f` or `-d` FUSE options. This can give you some insight
    over what the file system driver is doing internally, but it's
    mainly meant for debugging and the `debug` and `trace` levels
    in particular will slow down the driver.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

mkdwarfs(1), dwarfsck(1)

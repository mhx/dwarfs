# DwarFS

A high compression read-only file system

## Overview

DwarFS is a read-only file system with a focus on achieving very
high compression ratios in particular for very redundant data.

This probably doesn't sound very exciting, because if it's redundant,
it *should* compress well. However, I found that other read-only,
compressed file systems don't do a very good job at making use of
this redundancy.

Distinct features of DwarFS are:

* Clustering of files by similarity using a similarity hash function.
  This makes it easier to exploit the redundancy across file boundaries.

* Segmentation analysis across file system blocks in order to reduce
  the size of the uncompressed file system. This saves memory when
  using the compressed file system and thus potentially allows for
  higher cache hit rates as more data can be kept in the cache.

* Highly multi-threaded implementation. Both the file
  [system creation tool](man/mkdwarfs.md) as well as the
  [FUSE driver](man/dwarfs.md) are able to make good use of the
  many cores of your system.

## History

I started working on DwarFS in 2013 and my main use case and major
motivation was that I had several hundred different versions of Perl
that were taking up something around 30 gigabytes of disk space, and
I was unwilling to spend more than 10% of my hard drive keeping them
around for when I happened to need them.

Up until then, I had been using [Cromfs](https://bisqwit.iki.fi/source/cromfs.html)
for squeezing them into a manageable size. However, I was getting more
and more annoyed by the time it took to build the filesystem image
and, to make things worse, more often than not it was crashing after
about an hour or so.

I had obviously also looked into [SquashFS](https://en.wikipedia.org/wiki/SquashFS),
but never got anywhere close to the compression rates of Cromfs.

This alone wouldn't have been enough to get me into writing DwarFS,
but at around the same time, I was pretty obsessed with the recent
developments and features of newer C++ standards and really wanted
a C++ hobby project to work on. Also, I've wanted to do something
with [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace)
for quite some time. Last but not least, I had been thinking about
the problem of compressed file systems for a bit and had some ideas
that I definitely wanted to try.

The majority of the code was written in 2013, then I did a couple
of cleanups, bugfixes and refactors every once in a while, but I
never really got it to a state where I would feel happy releasing
it. It was too awkward to build with its dependency on Facebook's
(quite awesome) [folly](https://github.com/facebook/folly) library
and it didn't have any documentation.

Digging out the project again this year, things didn't look as grim
as they used to. Folly now builds with CMake and so I just pulled
it in as a submodule. Most other dependencies can be satisfied
from packages that should be widely available. And I've written
some rudimentary docs as well.

## Building and Installing

### Dependencies

DwarFS uses [CMake](https://cmake.org/) as a build tool.

It uses both [Boost](https://www.boost.org/) and
[Folly](https://github.com/facebook/folly), though the latter is
included as a submodule since very few distributions actually
offer packages for it. Folly itself has a number of dependencies,
so please check [here](https://github.com/facebook/folly#dependencies)
for an up-to-date list.

Other than that, DwarFS really only depends on FUSE3 and on a set
of compression libraries that Folly already depends on (namely
[lz4](https://github.com/lz4/lz4), [zstd](https://github.com/facebook/zstd)
and [liblzma](https://github.com/kobolabs/liblzma)).

The dependency on [googletest](https://github.com/google/googletest)
will be automatically resolved if you build with tests.

### Building

Firstly, either clone the repository...

    # git clone --recurse-submodules https://github.com/mhx/dwarfs
    # cd dwarfs

...or unpack the release archive:

    # tar xvf dwarfs-0.1.0.tar.bz2
    # cd dwarfs-0.1.0

Once all dependencies have been installed, you can build DwarFS
using:

    # mkdir build
    # cd build
    # cmake .. -DWITH_TESTS
    # make -j$(nproc)

If possible, try building with clang as your compiler, this will
make DwarFS significantly faster. If you have both gcc and clang
installed, use:

    # cmake .. -DWITH_TESTS -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

You can then run tests with:

    # make test

### Installing

Installing is as easy as:

    # sudo make install

Though you don't have to install the tools to play with them.

## Usage

Please check out the man pages for [mkdwarfs](man/mkdwarfs.md)
and [dwarfs](man/dwarfs.md). `dwarfsck` will be built and installed
as well, but it's still work in progress.

## Comparison

### With SquashFS

These tests were done on an Intel(R) Xeon(R) CPU D-1528 @ 1.90GHz
6-core CPU with 64 GiB of RAM. The system was mostly idle during
all of the tests.

The source directory contained 863 different Perl installations from
284 distinct releases, a total of 40.73 GiB of data in 1453691 files
and 248850 directories.

I'm using the same compression type and level with SquashFS that is
the default setting for DwarFS:

    $ time mksquashfs /tmp/perl/install perl.squashfs -comp zstd -Xcompression-level 22
    Parallel mksquashfs: Using 12 processors
    Creating 4.0 filesystem on perl.squashfs, block size 131072.
    [===========================================================-] 1624691/1624691 100%
    
    Exportable Squashfs 4.0 filesystem, zstd compressed, data block size 131072
            compressed data, compressed metadata, compressed fragments,
            compressed xattrs, compressed ids
            duplicates are removed
    Filesystem size 4731800.19 Kbytes (4620.90 Mbytes)
            11.09% of uncompressed filesystem size (42661479.48 Kbytes)
    Inode table size 14504766 bytes (14164.81 Kbytes)
            26.17% of uncompressed inode table size (55433554 bytes)
    Directory table size 14426288 bytes (14088.17 Kbytes)
            46.30% of uncompressed directory table size (31161014 bytes)
    Number of duplicate files found 1342877
    Number of inodes 1700692
    Number of files 1451842
    Number of fragments 24739
    Number of symbolic links  0
    Number of device nodes 0
    Number of fifo nodes 0
    Number of socket nodes 0
    Number of directories 248850
    Number of ids (unique uids + gids) 2
    Number of uids 1
            mhx (1000)
    Number of gids 1
            users (100)
    
    real    70m25.543s
    user    672m37.049s
    sys     2m15.321s

For DwarFS, I'm allowing the same amount of memory (16 GiB) to be
used during compression that SquashFS is using.

    $ time mkdwarfs -i /tmp/perl/install -o perl.dwarfs --no-owner -L 16g
    00:34:29.398178 scanning /tmp/perl/install
    00:34:43.746747 waiting for background scanners...
    00:36:31.692714 finding duplicate files...
    00:36:38.016250 saved 23.75 GiB / 40.73 GiB in 1344725/1453691 duplicate files
    00:36:38.016349 ordering 108966 inodes by similarity...
    00:36:38.311288 108966 inodes ordered [294.9ms]
    00:36:38.311373 numbering file inodes...
    00:36:38.313455 building metadata...
    00:36:38.313540 building blocks...
    00:36:38.313577 saving links...
    00:36:38.364396 saving names...
    00:36:38.364478 compressing names table...
    00:36:38.400903 names table: 111.4 KiB (9.979 KiB saved) [36.36ms]
    00:36:38.400977 updating name offsets...
    00:52:27.966740 saving chunks...
    00:52:27.993112 saving chunk index...
    00:52:27.993268 saving directories...
    00:52:28.294630 saving inode index...
    00:52:28.295636 saving metadata config...
    00:52:54.331409 compressed 40.73 GiB to 1.062 GiB (ratio=0.0260797)
    00:52:54.748237 filesystem created without errors [1105s]
    -------------------------------------------------------------------------------
    found/scanned: 248850/248850 dirs, 0/0 links, 1453691/1453691 files
    original size: 40.73 GiB, dedupe: 23.75 GiB (1344725 files), segment: 8.364 GiB
    filesystem: 8.614 GiB in 552 blocks (357297 chunks, 108966/108966 inodes)
    compressed filesystem: 552 blocks/1.062 GiB written
    |=============================================================================|
    
    real    18m25.440s
    user    134m59.088s
    sys     3m22.310s

So in this comparison, `mkdwarfs` is almost 4 times faster than `mksquashfs`.
In total CPU time, it's actually 5 times faster.

    $ ls -l perl.*fs
    -rw-r--r-- 1 mhx users 4845367296 Nov 22 00:31 perl.squashfs
    -rw-r--r-- 1 mhx users 1140619512 Nov 22 00:52 perl.dwarfs

In terms of compression ratio, the DwarFS file system is more than 4 times
smaller than the SquashFS file system. With DwarFS, the content has been
compressed down to 2.6% of its original size.

The use of the `--no-owner` option with the `mkdwarfs` only makes the file
system about 0.1% smaller, so this can safely be ignored here.

DwarFS also features an option to recompress an existing file system with
a different compression algorithm. This can be useful as it allows relatively
fast experimentation with different algorithms and options without requiring
a full rebuild of the file system. For example, recompressing the above file
system with the best possible compression (`lzma:level=9:extreme`):

    $ time mkdwarfs --recompress -i perl.dwarfs -o perl-lzma.dwarfs -C lzma:level=9:extreme
    01:10:05.609649 filesystem rewritten [807.6s]
    -------------------------------------------------------------------------------
    found/scanned: 0/0 dirs, 0/0 links, 0/0 files
    original size: 40.73 GiB, dedupe: 0 B (0 files), segment: 0 B
    filesystem: 8.614 GiB in 552 blocks (0 chunks, 0/0 inodes)
    compressed filesystem: 552 blocks/974.2 MiB written
    |=============================================================================|
    
    real    13m27.617s
    user    146m11.055s
    sys     2m3.924s

    $ ls -l perl*.dwarfs
    -rw-r--r-- 1 mhx users 1021483264 Nov 22 01:10 perl-lzma.dwarfs
    -rw-r--r-- 1 mhx users 1140619512 Nov 22 00:52 perl.dwarfs

This reduces the file system size by another 11%.

In terms of how fast the file system is when using it, a quick test
I've done is to freshly mount the filesystem created above and run
each of the 863 `perl` executables to print their version. Mounting
works like this:

    $ dwarfs perl.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4

Then I've run the following command twice to show the effect of the
block cache:

    $ time ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P12 sh -c '$0 -v >/dev/null'
    
    real    0m2.193s
    user    0m1.557s
    sys     0m2.937s
    $ time ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P12 sh -c '$0 -v >/dev/null'
    
    real    0m0.563s
    user    0m1.409s
    sys     0m2.351s

Even the first time this is run, the result is pretty decent. Also
notice that through the use of `xargs -P12`, 12 `perl` processes
are being executed concurrently, so this also exercises the ability
of DwarFS to deal with concurrent file system accesses.

Using the lzma-compressed file system, the metrics look considerably
worse:

    $ time ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P12 sh -c '$0 -v >/dev/null'
    
    real    0m12.036s
    user    0m1.701s
    sys     0m3.176s
    $ time ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P12 sh -c '$0 -v >/dev/null'
    
    real    0m0.538s
    user    0m1.404s
    sys     0m2.160s

So you might want to consider preferring zstd over lzma if you'd
like to optimize for file system performance.

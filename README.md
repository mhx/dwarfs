[![Build Status](https://travis-ci.com/mhx/dwarfs.svg?branch=main)](https://travis-ci.com/mhx/dwarfs)

# DwarFS

A fast high compression read-only file system

## Table of contents

- [Overview](#overview)
- [History](#history)
- [Building and Installing](#building-and-installing)
  - [Dependencies](#dependencies)
  - [Building](#building)
  - [Installing](#installing)
  - [Experimental Python Scripting Support](#experimental-python-scripting-support)
- [Usage](#usage)
- [Comparison](#comparison)
  - [With SquashFS](#with-squashfs)
  - [With SquashFS &amp; xz](#with-squashfs--xz)
  - [With lrzip](#with-lrzip)
  - [With zpaq](#with-zpaq)
  - [With wimlib](#with-wimlib)
  - [With Cromfs](#with-cromfs)
  - [With EROFS](#with-erofs)

## Overview

![Alt text](doc/screenshot.gif?raw=true "DwarFS Screenshot")

DwarFS is a read-only file system with a focus on achieving **very
high compression ratios** in particular for very redundant data.

This probably doesn't sound very exciting, because if it's redundant,
it *should* compress well. However, I found that other read-only,
compressed file systems don't do a very good job at making use of
this redundancy. See [here](#comparison) for a comparison with other
compressed file systems.

DwarFS also **doesn't compromise on speed** and for my use cases I've
found it to be on par with or perform better than SquashFS. For my
primary use case, **DwarFS compression is an order of magnitude better
than SquashFS compression**, it's **6 times faster to build the file
system**, it's typically faster to access files on DwarFS and it uses
less CPU resources.

Distinct features of DwarFS are:

- Clustering of files by similarity using a similarity hash function.
  This makes it easier to exploit the redundancy across file boundaries.

- Segmentation analysis across file system blocks in order to reduce
  the size of the uncompressed file system. This saves memory when
  using the compressed file system and thus potentially allows for
  higher cache hit rates as more data can be kept in the cache.

- Highly multi-threaded implementation. Both the file
  [system creation tool](doc/mkdwarfs.md) as well as the
  [FUSE driver](doc/dwarfs.md) are able to make good use of the
  many cores of your system.

- Optional experimental Python scripting support to provide custom
  filtering and ordering functionality.

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

It also uses [Facebook Thrift](https://github.com/facebook/fbthrift),
in particular the `frozen` library, for storing metadata in a highly
space-efficient, memory-mappable and well defined format. It's also
included as a submodule, and we only build the compiler and a very
reduced library that contains just enough for DwarFS to work.

Other than that, DwarFS really only depends on FUSE3 and on a set
of compression libraries that Folly already depends on (namely
[lz4](https://github.com/lz4/lz4), [zstd](https://github.com/facebook/zstd)
and [liblzma](https://github.com/kobolabs/liblzma)).

The dependency on [googletest](https://github.com/google/googletest)
will be automatically resolved if you build with tests.

A good starting point for apt-based systems is probably:

```
$ apt install \
    g++ \
    clang \
    cmake \
    make \
    bison \
    flex \
    ronn \
    fuse3 \
    pkg-config \
    binutils-dev \
    libarchive-dev \
    libboost-context-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-python-dev \
    libboost-regex-dev \
    libboost-system-dev \
    libboost-thread-dev \
    libevent-dev \
    libjemalloc-dev \
    libdouble-conversion-dev \
    libiberty-dev \
    liblz4-dev \
    liblzma-dev \
    libssl-dev \
    libunwind-dev \
    libdwarf-dev \
    libelf-dev \
    libfmt-dev \
    libfuse3-dev \
    libgoogle-glog-dev
```

Note that when building with `gcc`, the optimization level will be
set to `-O2` instead of the CMake default of `-O3` for release
builds. At least with versions up to `gcc-10`, the `-O3` build is
[up to 70% slower](https://github.com/mhx/dwarfs/issues/14) than a
build with `-O2`.

### Building

Firstly, either clone the repository...

```
$ git clone --recurse-submodules https://github.com/mhx/dwarfs
$ cd dwarfs
```

...or unpack the release archive:

```
$ tar xvf dwarfs-x.y.z.tar.bz2
$ cd dwarfs-x.y.z
```

Once all dependencies have been installed, you can build DwarFS
using:

```
$ mkdir build
$ cd build
$ cmake .. -DWITH_TESTS=1
$ make -j$(nproc)
```

You can then run tests with:

```
$ make test
```

All binaries use [jemalloc](https://github.com/jemalloc/jemalloc)
as a memory allocator by default, as it is typically uses much less
system memory compared to the `glibc` or `tcmalloc` allocators.
To disable the use of `jemalloc`, pass `-DUSE_JEMALLOC=0` on the
`cmake` command line.

### Installing

Installing is as easy as:

```
$ sudo make install
```

Though you don't have to install the tools to play with them.

### Experimental Python Scripting Support

You can build `mkdwarfs` with experimental support for Python
scripting:

```
$ cmake .. -DWITH_TESTS=1 -DWITH_PYTHON=1
```

This also requires Boost.Python. If you have multiple Python
versions installed, you can explicitly specify the version to
build against:

```
$ cmake .. -DWITH_TESTS=1 -DWITH_PYTHON=1 -DWITH_PYTHON_VERSION=3.8
```

Note that only Python 3 is supported. You can take a look at
[scripts/example.py](scripts/example.py) to get an idea for
what can currently be done with the interface.

## Usage

Please check out the man pages for [mkdwarfs](doc/mkdwarfs.md),
[dwarfs](doc/dwarfs.md), [dwarfsck](doc/dwarfsck.md) and
[dwarfsextract](doc/dwarfsextract.md).

The [dwarfs](doc/dwarfs.md) man page also shows an example for setting
up DwarFS with [overlayfs](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt)
in order to create a writable file system mount on top a read-only
DwarFS image.

A description of the DwarFS filesystem format can be found in
[dwarfs-format](doc/dwarfs-format.md).

## Comparison

The SquashFS, `xz`, `lrzip`, `zpaq` and `wimlib` tests were all done on
an 8 core Intel(R) Xeon(R) E-2286M CPU @ 2.40GHz with 64 GiB of RAM.

The Cromfs and EROFS tests were done with an older version of DwarFS
on a 6 core Intel(R) Xeon(R) CPU D-1528 @ 1.90GHz with 64 GiB of RAM.

The systems were mostly idle during all of the tests.

### With SquashFS

The source directory contained **1139 different Perl installations**
from 284 distinct releases, a total of 47.65 GiB of data in 1,927,501
files and 330,733 directories. The source directory was freshly
unpacked from a tar archive to an XFS partition on a 970 EVO Plus 2TB
NVME drive, so most of its contents were likely cached.

I'm using the same compression type and compression level for
SquashFS that is the default setting for DwarFS:

```
$ time mksquashfs install perl-install.squashfs -comp zstd -Xcompression-level 22
Parallel mksquashfs: Using 16 processors
Creating 4.0 filesystem on perl-install-zstd.squashfs, block size 131072.
[=========================================================/] 2107401/2107401 100%

Exportable Squashfs 4.0 filesystem, zstd compressed, data block size 131072
        compressed data, compressed metadata, compressed fragments,
        compressed xattrs, compressed ids
        duplicates are removed
Filesystem size 4637597.63 Kbytes (4528.90 Mbytes)
        9.29% of uncompressed filesystem size (49922299.04 Kbytes)
Inode table size 19100802 bytes (18653.13 Kbytes)
        26.06% of uncompressed inode table size (73307702 bytes)
Directory table size 19128340 bytes (18680.02 Kbytes)
        46.28% of uncompressed directory table size (41335540 bytes)
Number of duplicate files found 1780387
Number of inodes 2255794
Number of files 1925061
Number of fragments 28713
Number of symbolic links  0
Number of device nodes 0
Number of fifo nodes 0
Number of socket nodes 0
Number of directories 330733
Number of ids (unique uids + gids) 2
Number of uids 1
        mhx (1000)
Number of gids 1
        users (100)

real    32m54.713s
user    501m46.382s
sys     0m58.528s
```

For DwarFS, I'm sticking to the defaults:

```
$ time mkdwarfs -i install -o perl-install.dwarfs
I 11:33:33.310931 scanning install
I 11:33:39.026712 waiting for background scanners...
I 11:33:50.681305 assigning directory and link inodes...
I 11:33:50.888441 finding duplicate files...
I 11:34:01.120800 saved 28.2 GiB / 47.65 GiB in 1782826/1927501 duplicate files
I 11:34:01.122608 waiting for inode scanners...
I 11:34:12.839065 assigning device inodes...
I 11:34:12.875520 assigning pipe/socket inodes...
I 11:34:12.910431 building metadata...
I 11:34:12.910524 building blocks...
I 11:34:12.910594 saving names and links...
I 11:34:12.910691 bloom filter size: 32 KiB
I 11:34:12.910760 ordering 144675 inodes using nilsimsa similarity...
I 11:34:12.915555 nilsimsa: depth=20000 (1000), limit=255
I 11:34:13.052525 updating name and link indices...
I 11:34:13.276233 pre-sorted index (660176 name, 366179 path lookups) [360.6ms]
I 11:35:44.039375 144675 inodes ordered [91.13s]
I 11:35:44.041427 waiting for segmenting/blockifying to finish...
I 11:37:38.823902 bloom filter reject rate: 96.017% (TPR=0.244%, lookups=4740563665)
I 11:37:38.823963 segmentation matches: good=454708, bad=6819, total=464247
I 11:37:38.824005 segmentation collisions: L1=0.008%, L2=0.000% [2233254 hashes]
I 11:37:38.824038 saving chunks...
I 11:37:38.860939 saving directories...
I 11:37:41.318747 waiting for compression to finish...
I 11:38:56.046809 compressed 47.65 GiB to 430.9 MiB (ratio=0.00883101)
I 11:38:56.304922 filesystem created without errors [323s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
330733 dirs, 0/2440 soft/hard links, 1927501/1927501 files, 0 other
original size: 47.65 GiB, dedupe: 28.2 GiB (1782826 files), segment: 15.19 GiB
filesystem: 4.261 GiB in 273 blocks (319178 chunks, 144675/144675 inodes)
compressed filesystem: 273 blocks/430.9 MiB written [depth: 20000]
█████████████████████████████████████████████████████████████████████████████▏100% |

real    5m23.030s
user    78m7.554s
sys     1m47.968s
```

So in this comparison, `mkdwarfs` is **more than 6 times faster** than `mksquashfs`,
both in terms of CPU time and wall clock time.

```
$ ll perl-install.*fs
-rw-r--r-- 1 mhx users  447230618 Mar  3 20:28 perl-install.dwarfs
-rw-r--r-- 1 mhx users 4748902400 Mar  3 20:10 perl-install.squashfs
```

In terms of compression ratio, the **DwarFS file system is more than 10 times
smaller than the SquashFS file system**. With DwarFS, the content has been
**compressed down to less than 0.9% (!) of its original size**. This compression
ratio only considers the data stored in the individual files, not the actual
disk space used. On the original XFS file system, according to `du`, the
source folder uses 52 GiB, so **the DwarFS image actually only uses 0.8% of
the original space**.

Here's another comparison using `lzma` compression instead of `zstd`:

```
$ time mksquashfs install perl-install-lzma.squashfs -comp lzma

real    13m42.825s
user    205m40.851s
sys     3m29.088s
```

```
$ time mkdwarfs -i install -o perl-install-lzma.dwarfs -l9

real    3m43.937s
user    49m45.295s
sys     1m44.550s
```

```
$ ll perl-install-lzma.*fs
-rw-r--r-- 1 mhx users  315482627 Mar  3 21:23 perl-install-lzma.dwarfs
-rw-r--r-- 1 mhx users 3838406656 Mar  3 20:50 perl-install-lzma.squashfs
```

It's immediately obvious that the runs are significantly faster and the
resulting images are significantly smaller. Still, `mkdwarfs` is about
**4 times faster** and produces and image that's **12 times smaller** than
the SquashFS image. The DwarFS image is only 0.6% of the original file size.

So why not use `lzma` instead of `zstd` by default? The reason is that `lzma`
is about an order of magnitude slower to decompress than `zstd`. If you're
only accessing data on your compressed filesystem occasionally, this might
not be a big deal, but if you use it extensively, `zstd` will result in
better performance.

The comparisons above are not completely fair. `mksquashfs` by default
uses a block size of 128KiB, whereas `mkdwarfs` uses 16MiB blocks by default,
or even 64MiB blocks with `-l9`. When using identical block sizes for both
file systems, the difference, quite expectedly, becomes a lot less dramatic:

```
$ time mksquashfs install perl-install-lzma-1M.squashfs -comp lzma -b 1M

real    15m43.319s
user    139m24.533s
sys     0m45.132s
```

```
$ time mkdwarfs -i install -o perl-install-lzma-1M.dwarfs -l9 -S20 -B3

real    4m25.973s
user    52m15.100s
sys     7m41.889s
```

```
$ ll perl-install*.*fs
-rw-r--r-- 1 mhx users  935953866 Mar 13 12:12 perl-install-lzma-1M.dwarfs
-rw-r--r-- 1 mhx users 3407474688 Mar  3 21:54 perl-install-lzma-1M.squashfs
```

Even this is *still* not entirely fair, as it uses a feature (`-B3`) that allows
DwarFS to reference file chunks from up to two previous filesystem blocks.

But the point is that this is really where SquashFS tops out, as it doesn't
support larger block sizes or back-referencing. And as you'll see below, the
larger blocks that DwarFS is using by default don't necessarily negatively
impact performance.

DwarFS also features an option to recompress an existing file system with
a different compression algorithm. This can be useful as it allows relatively
fast experimentation with different algorithms and options without requiring
a full rebuild of the file system. For example, recompressing the above file
system with the best possible compression (`-l 9`):

```
$ time mkdwarfs --recompress -i perl-install.dwarfs -o perl-lzma-re.dwarfs -l9
I 20:28:03.246534 filesystem rewrittenwithout errors [148.3s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
filesystem: 4.261 GiB in 273 blocks (0 chunks, 0 inodes)
compressed filesystem: 273/273 blocks/372.7 MiB written
████████████████████████████████████████████████████████████████████▏100% \

real    2m28.279s
user    37m8.825s
sys     0m43.256s
```

```
$ ll perl-*.dwarfs
-rw-r--r-- 1 mhx users 447230618 Mar  3 20:28 perl-install.dwarfs
-rw-r--r-- 1 mhx users 390845518 Mar  4 20:28 perl-lzma-re.dwarfs
-rw-r--r-- 1 mhx users 315482627 Mar  3 21:23 perl-install-lzma.dwarfs
```

Note that while the recompressed filesystem is smaller than the original image,
it is still a lot bigger than the filesystem we previously build with `-l9`.
The reason is that the recompressed image still uses the same block size, and
the block size cannot be changed by recompressing.

In terms of how fast the file system is when using it, a quick test
I've done is to freshly mount the filesystem created above and run
each of the 1139 `perl` executables to print their version.

```
$ hyperfine -c "umount mnt" -p "umount mnt; dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P5 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      1.810 s ±  0.013 s    [User: 1.847 s, System: 0.623 s]
  Range (min … max):    1.788 s …  1.825 s    10 runs

Benchmark #2: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      1.333 s ±  0.009 s    [User: 1.993 s, System: 0.656 s]
  Range (min … max):    1.321 s …  1.354 s    10 runs

Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P15 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      1.181 s ±  0.018 s    [User: 2.086 s, System: 0.712 s]
  Range (min … max):    1.165 s …  1.214 s    10 runs

Benchmark #4: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      1.149 s ±  0.015 s    [User: 2.128 s, System: 0.781 s]
  Range (min … max):    1.136 s …  1.186 s    10 runs
```

These timings are for *initial* runs on a freshly mounted file system,
running 5, 10, 15 and 20 processes in parallel. 1.1 seconds means that
it takes only about 1 millisecond per Perl binary.

Following are timings for *subsequent* runs, both on DwarFS (at `mnt`)
and the original XFS (at `install`). DwarFS is around 15% slower here:

```
$ hyperfine -P procs 10 20 -D 10 -w1 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'" "ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):     347.0 ms ±   7.2 ms    [User: 1.755 s, System: 0.452 s]
  Range (min … max):   341.3 ms … 365.2 ms    10 runs

Benchmark #2: ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):     302.5 ms ±   3.3 ms    [User: 1.656 s, System: 0.377 s]
  Range (min … max):   297.1 ms … 308.7 ms    10 runs

Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):     342.2 ms ±   4.1 ms    [User: 1.766 s, System: 0.451 s]
  Range (min … max):   336.0 ms … 349.7 ms    10 runs

Benchmark #4: ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):     302.0 ms ±   3.0 ms    [User: 1.659 s, System: 0.374 s]
  Range (min … max):   297.0 ms … 305.4 ms    10 runs

Summary
  'ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'' ran
    1.00 ± 0.01 times faster than 'ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null''
    1.13 ± 0.02 times faster than 'ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null''
    1.15 ± 0.03 times faster than 'ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null''
```

Using the lzma-compressed file system, the metrics for *initial* runs look
considerably worse (about an order of magnitude):

```
$ hyperfine -c "umount mnt" -p "umount mnt; dwarfs perl-install-lzma.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P5 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):     10.660 s ±  0.057 s    [User: 1.952 s, System: 0.729 s]
  Range (min … max):   10.615 s … 10.811 s    10 runs

Benchmark #2: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      9.092 s ±  0.021 s    [User: 1.979 s, System: 0.680 s]
  Range (min … max):    9.059 s …  9.126 s    10 runs

Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P15 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      9.012 s ±  0.188 s    [User: 2.077 s, System: 0.702 s]
  Range (min … max):    8.839 s …  9.277 s    10 runs

Benchmark #4: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
  Time (mean ± σ):      9.004 s ±  0.298 s    [User: 2.134 s, System: 0.736 s]
  Range (min … max):    8.611 s …  9.555 s    10 runs
```

So you might want to consider using `zstd` instead of `lzma` if you'd
like to optimize for file system performance. It's also the default
compression used by `mkdwarfs`.

Now here's a comparison with the SquashFS filesystem:

```
$ hyperfine -c 'sudo umount mnt' -p 'umount mnt; dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1' -n dwarfs-zstd "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'" -p 'sudo umount mnt; sudo mount -t squashfs perl-install.squashfs mnt; sleep 1' -n squashfs-zstd "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'"
Benchmark #1: dwarfs-zstd
  Time (mean ± σ):      1.151 s ±  0.015 s    [User: 2.147 s, System: 0.769 s]
  Range (min … max):    1.118 s …  1.174 s    10 runs

Benchmark #2: squashfs-zstd
  Time (mean ± σ):      6.733 s ±  0.007 s    [User: 3.188 s, System: 17.015 s]
  Range (min … max):    6.721 s …  6.743 s    10 runs

Summary
  'dwarfs-zstd' ran
    5.85 ± 0.08 times faster than 'squashfs-zstd'
```

So DwarFS is almost six times faster than SquashFS. But what's more,
SquashFS also uses significantly more CPU power. However, the numbers
shown above for DwarFS obviously don't include the time spent in the
`dwarfs` process, so I repeated the test outside of hyperfine:

```
$ time dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4 -f

real    0m4.569s
user    0m2.154s
sys     0m1.846s
```

So in total, DwarFS was using 5.7 seconds of CPU time, whereas
SquashFS was using 20.2 seconds, almost four times as much. Ignore
the 'real' time, this is only how long it took me to unmount the
file system again after mounting it.

Another real-life test was to build and test a Perl module with 624
different Perl versions in the compressed file system. The module I've
used, [Tie::Hash::Indexed](https://github.com/mhx/Tie-Hash-Indexed),
has an XS component that requires a C compiler to build. So this really
accesses a lot of different stuff in the file system:

- The `perl` executables and its shared libraries

- The Perl modules used for writing the Makefile

- Perl's C header files used for building the module

- More Perl modules used for running the tests

I wrote a little script to be able to run multiple builds in parallel:

```bash
#!/bin/bash
set -eu
perl=$1
dir=$(echo "$perl" | cut -d/ --output-delimiter=- -f5,6)
rsync -a Tie-Hash-Indexed/ $dir/
cd $dir
$1 Makefile.PL >/dev/null 2>&1
make test >/dev/null 2>&1
cd ..
rm -rf $dir
echo $perl
```

The following command will run up to 16 builds in parallel on the 8 core
Xeon CPU, including debug, optimized and threaded versions of all Perl
releases between 5.10.0 and 5.33.3, a total of 624 `perl` installations:

```
$ time ls -1 /tmp/perl/install/*/perl-5.??.?/bin/perl5* | sort -t / -k 8 | xargs -d $'\n' -P 16 -n 1 ./build.sh
```

Tests were done with a cleanly mounted file system to make sure the caches
were empty. `ccache` was primed to make sure all compiler runs could be
satisfied from the cache. With SquashFS, the timing was:

```
real    0m52.385s
user    8m10.333s
sys     4m10.056s
```

And with DwarFS:

```
real    0m50.469s
user    9m22.597s
sys     1m18.469s
```

So, frankly, not much of a difference, with DwarFS being just a bit faster.
The `dwarfs` process itself used:

```
real    0m56.686s
user    0m18.857s
sys     0m21.058s
```

So again, DwarFS used less raw CPU power overall, but in terms of wallclock
time, the difference is really marginal.

### With SquashFS & xz

This test uses slightly less pathological input data: the root filesystem of
a recent Raspberry Pi OS release. This file system also contains device inodes,
so in order to preserve those, we pass `--with-devices` to `mkdwarfs`:

```
$ time sudo mkdwarfs -i raspbian -o raspbian.dwarfs --with-devices
I 21:30:29.812562 scanning raspbian
I 21:30:29.908984 waiting for background scanners...
I 21:30:30.217446 assigning directory and link inodes...
I 21:30:30.221941 finding duplicate files...
I 21:30:30.288099 saved 31.05 MiB / 1007 MiB in 1617/34582 duplicate files
I 21:30:30.288143 waiting for inode scanners...
I 21:30:31.393710 assigning device inodes...
I 21:30:31.394481 assigning pipe/socket inodes...
I 21:30:31.395196 building metadata...
I 21:30:31.395230 building blocks...
I 21:30:31.395291 saving names and links...
I 21:30:31.395374 ordering 32965 inodes using nilsimsa similarity...
I 21:30:31.396254 nilsimsa: depth=20000 (1000), limit=255
I 21:30:31.407967 pre-sorted index (46431 name, 2206 path lookups) [11.66ms]
I 21:30:31.410089 updating name and link indices...
I 21:30:38.178505 32965 inodes ordered [6.783s]
I 21:30:38.179417 waiting for segmenting/blockifying to finish...
I 21:31:06.248304 saving chunks...
I 21:31:06.251998 saving directories...
I 21:31:06.402559 waiting for compression to finish...
I 21:31:16.425563 compressed 1007 MiB to 287 MiB (ratio=0.285036)
I 21:31:16.464772 filesystem created without errors [46.65s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
4435 dirs, 5908/0 soft/hard links, 34582/34582 files, 7 other
original size: 1007 MiB, dedupe: 31.05 MiB (1617 files), segment: 47.23 MiB
filesystem: 928.4 MiB in 59 blocks (38890 chunks, 32965/32965 inodes)
compressed filesystem: 59 blocks/287 MiB written [depth: 20000]
████████████████████████████████████████████████████████████████████▏100% |

real    0m46.711s
user    10m39.038s
sys     0m8.123s
```

Again, SquashFS uses the same compression options:

```
$ time sudo mksquashfs raspbian raspbian.squashfs -comp zstd -Xcompression-level 22
Parallel mksquashfs: Using 16 processors
Creating 4.0 filesystem on raspbian.squashfs, block size 131072.
[===============================================================\] 39232/39232 100%

Exportable Squashfs 4.0 filesystem, zstd compressed, data block size 131072
        compressed data, compressed metadata, compressed fragments,
        compressed xattrs, compressed ids
        duplicates are removed
Filesystem size 371934.50 Kbytes (363.22 Mbytes)
        35.98% of uncompressed filesystem size (1033650.60 Kbytes)
Inode table size 399913 bytes (390.54 Kbytes)
        26.53% of uncompressed inode table size (1507581 bytes)
Directory table size 408749 bytes (399.17 Kbytes)
        42.31% of uncompressed directory table size (966174 bytes)
Number of duplicate files found 1618
Number of inodes 44932
Number of files 34582
Number of fragments 3290
Number of symbolic links  5908
Number of device nodes 7
Number of fifo nodes 0
Number of socket nodes 0
Number of directories 4435
Number of ids (unique uids + gids) 18
Number of uids 5
        root (0)
        mhx (1000)
        unknown (103)
        shutdown (6)
        unknown (106)
Number of gids 15
        root (0)
        unknown (109)
        unknown (42)
        unknown (1000)
        users (100)
        unknown (43)
        tty (5)
        unknown (108)
        unknown (111)
        unknown (110)
        unknown (50)
        mail (12)
        nobody (65534)
        adm (4)
        mem (8)

real    0m50.124s
user    9m41.708s
sys     0m1.727s
```

The difference in speed is almost negligible. SquashFS is just a bit
slower here. In terms of compression, the difference also isn't huge:

```
$ ls -lh raspbian.* *.xz
-rw-r--r-- 1 mhx  users 297M Mar  4 21:32 2020-08-20-raspios-buster-armhf-lite.img.xz
-rw-r--r-- 1 root root  287M Mar  4 21:31 raspbian.dwarfs
-rw-r--r-- 1 root root  364M Mar  4 21:33 raspbian.squashfs
```

Interestingly, `xz` actually can't compress the whole original image
better than DwarFS.

We can even again try to increase the DwarFS compression level:

```
$ time sudo mkdwarfs -i raspbian -o raspbian-9.dwarfs --with-devices -l9

real    0m54.161s
user    8m40.109s
sys     0m7.101s
```

Now that actually gets the DwarFS image size well below that of the
`xz` archive:

```
$ ls -lh raspbian-9.dwarfs *.xz
-rw-r--r-- 1 root root  244M Mar  4 21:36 raspbian-9.dwarfs
-rw-r--r-- 1 mhx  users 297M Mar  4 21:32 2020-08-20-raspios-buster-armhf-lite.img.xz
```

Even if you actually build a tarball and compress that (instead of
compressing the EXT4 file system itself), `xz` isn't quite able to
match the DwarFS image size:

```
$ time sudo tar cf - raspbian | xz -9 -vT 0 >raspbian.tar.xz
  100 %     246.9 MiB / 1,037.2 MiB = 0.238    13 MiB/s       1:18

real    1m18.226s
user    6m35.381s
sys     0m2.205s
```

```
$ ls -lh raspbian.tar.xz
-rw-r--r-- 1 mhx users 247M Mar  4 21:40 raspbian.tar.xz
```

DwarFS also comes with the [dwarfsextract](doc/dwarfsextract.md) tool
that allows extraction of a filesystem image without the FUSE driver.
So here's a comparison of the extraction speed:

```
$ time sudo tar xf raspbian.tar.xz -C out1

real    0m12.846s
user    0m12.313s
sys     0m1.616s
```

```
$ time sudo dwarfsextract -i raspbian-9.dwarfs -o out2

real    0m3.825s
user    0m13.234s
sys     0m1.382s
```

So `dwarfsextract` is almost 4 times faster thanks to using multiple
worker threads for decompression. It's writing about 300 MiB/s in this
example.

Another nice feature of `dwarfsextract` is that it allows you to directly
output data in an archive format, so you could create a tarball from
your image without extracting the files to disk:

```
$ dwarfsextract -i raspbian-9.dwarfs -f ustar | xz -9 -T0 >raspbian2.tar.xz
```

This has the interesting side-effect that the resulting tarball will
likely be smaller than the one built straight from the directory:

```
$ ls -lh raspbian*.tar.xz
-rw-r--r-- 1 mhx users 247M Mar  4 21:40 raspbian.tar.xz
-rw-r--r-- 1 mhx users 240M Mar  4 23:52 raspbian2.tar.xz
```

That's because `dwarfsextract` writes files in inode-order, and by
default inodes are ordered by similarity for the best possible
compression.

### With lrzip

[lrzip](https://github.com/ckolivas/lrzip) is a compression utility
targeted especially at compressing large files. From its description,
it looks like it does something very similar to DwarFS, i.e. it looks
for duplicate segments before passsing the de-duplicated data on to
an `lzma` compressor.

When I first read about `lrzip`, I was pretty certain it would easily
beat DwarFS. So let's take a look. `lrzip` operates on a single file,
so it's necessary to first build a tarball:

```
$ time tar cf perl-install.tar install

real    2m9.568s
user    0m3.757s
sys     0m26.623s
```

Now we can run `lrzip`:

```
$ time lrzip -vL9 -o perl-install.tar.lrzip perl-install.tar
The following options are in effect for this COMPRESSION.
Threading is ENABLED. Number of CPUs detected: 16
Detected 67106172928 bytes ram
Compression level 9
Nice Value: 19
Show Progress
Verbose
Output Filename Specified: perl-install.tar.lrzip
Temporary Directory set as: ./
Compression mode is: LZMA. LZO Compressibility testing enabled
Heuristically Computed Compression Window: 426 = 42600MB
File size: 52615639040
Will take 2 passes
Beginning rzip pre-processing phase
Beginning rzip pre-processing phase
perl-install.tar - Compression Ratio: 100.378. Average Compression Speed: 14.536MB/s.
Total time: 00:57:32.47

real    57m32.472s
user    81m44.104s
sys     4m50.221s
```

That definitely took a while. This is about an order of magnitude
slower than `mkdwarfs` and it barely makes use of the 8 cores.

```
$ ll -h perl-install.tar.lrzip
-rw-r--r-- 1 mhx users 500M Mar  6 21:16 perl-install.tar.lrzip
```

This is a surprisingly disappointing result. The archive is 65% larger
than a DwarFS image at `-l9` that takes less than 4 minutes to build.
Also, you can't just access the files in the `.lrzip` without fully
unpacking the archive first.

That being said, it *is* better than just using `xz` on the tarball:

```
$ time xz -T0 -v9 -c perl-install.tar >perl-install.tar.xz
perl-install.tar (1/1)
  100 %      4,317.0 MiB / 49.0 GiB = 0.086    24 MiB/s      34:55

real    34m55.450s
user    543m50.810s
sys     0m26.533s
```

```
$ ll perl-install.tar.xz -h
-rw-r--r-- 1 mhx users 4.3G Mar  6 22:59 perl-install.tar.xz
```

### With zpaq

[zpaq](http://mattmahoney.net/dc/zpaq.html) is a journaling backup
utility and archiver. Again, it appears to share some of the ideas in
DwarFS, like segmentation analysis, but it also adds some features on
top that make it useful for incremental backups. However, it's also
not usable as a file system, so data needs to be extracted before it
can be used.

Anyway, how does it fare in terms of speed and compression performance?

```
$ time zpaq a perl-install.zpaq install -m5
```

After a few million lines of output that (I think) cannot be turned off:

```
2258234 +added, 0 -removed.

0.000000 + (51161.953159 -> 8932.000297 -> 490.227707) = 490.227707 MB
2828.082 seconds (all OK)

real    47m8.104s
user    714m44.286s
sys     3m6.751s
```

So it's an order of magnitude slower than `mkdwarfs` and uses 14 times
as much CPU resources as `mkdwarfs -l9`. The resulting archive it pretty
close in size to the default configuration DwarFS image, but it's more
than 50% bigger than the image produced by `mkdwarfs -l9`.

```
$ ll perl-install*.*
-rw-r--r-- 1 mhx users 490227707 Mar  7 01:38 perl-install.zpaq
-rw-r--r-- 1 mhx users 315482627 Mar  3 21:23 perl-install-l9.dwarfs
-rw-r--r-- 1 mhx users 447230618 Mar  3 20:28 perl-install.dwarfs
```

What's *really* surprising is how slow it is to extract the `zpaq`
archive again:

```
$ time zpaq x perl-install.zpaq
2798.097 seconds (all OK)

real    46m38.117s
user    711m18.734s
sys     3m47.876s
```

That's 700 times slower than extracting the DwarFS image.

### With wimlib

[wimlib](https://wimlib.net/) is a really interesting project that is
a lot more mature than DwarFS. While DwarFS at its core has a library
component that could potentially be ported to other operating systems,
wimlib already is available on many platforms. It also seems to have
quite a rich set of features, so it's definitely worth taking a look at.

I first tried `wimcapture` on the perl dataset:

```
$ time wimcapture --unix-data --solid --solid-chunk-size=16M install perl-install.wim
Scanning "install"
47 GiB scanned (1927501 files, 330733 directories)
Using LZMS compression with 16 threads
Archiving file data: 19 GiB of 19 GiB (100%) done

real    15m23.310s
user    174m29.274s
sys     0m42.921s
```

```
$ ll perl-install.*
-rw-r--r-- 1 mhx users  447230618 Mar  3 20:28 perl-install.dwarfs
-rw-r--r-- 1 mhx users  315482627 Mar  3 21:23 perl-install-l9.dwarfs
-rw-r--r-- 1 mhx users 4748902400 Mar  3 20:10 perl-install.squashfs
-rw-r--r-- 1 mhx users 1016981520 Mar  6 21:12 perl-install.wim
```

So wimlib is definitely much better than squashfs, in terms of both
compression ratio and speed. DwarFS is however about 3 times faster to
create the file system and the DwarFS file system less than half the size.
When switching to LZMA compression, the DwarFS file system is more than
3 times smaller (wimlib uses LZMS compression by default).

What's a bit surprising is that mounting a *wim* file takes quite a bit
of time:

```
$ time wimmount perl-install.wim mnt
[WARNING] Mounting a WIM file containing solid-compressed data; file access may be slow.

real    0m2.038s
user    0m1.764s
sys     0m0.242s
```

Mounting the DwarFS image takes almost no time in comparison:

```
$ time git/github/dwarfs/build-clang-11/dwarfs perl-install-default.dwarfs mnt
I 00:23:39.238182 dwarfs (v0.4.0, fuse version 35)

real    0m0.003s
user    0m0.003s
sys     0m0.000s
```

That's just because it immediately forks into background by default and
initializes the file system in the background. However, even when
running it in the foreground, initializing the file system takes only
about 60 milliseconds:

```
$ dwarfs perl-install.dwarfs mnt -f
I 00:25:03.186005 dwarfs (v0.4.0, fuse version 35)
I 00:25:03.248061 file system initialized [60.95ms]
```

If you actually build the DwarFS file system with uncompressed metadata,
mounting is basically instantaneous:

```
$ dwarfs perl-install-meta.dwarfs mnt -f
I 00:27:52.667026 dwarfs (v0.4.0, fuse version 35)
I 00:27:52.671066 file system initialized [2.879ms]
```

I've tried running the benchmark where all 1139 `perl` executables
print their version with the wimlib image, but after about 10 minutes,
it still hadn't finished the first run (with the DwarFS image, one run
took slightly more than 2 seconds). I then tried the following instead:

```
$ ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P1 sh -c 'time $0 -v >/dev/null' 2>&1 | grep ^real
real    0m0.802s
real    0m0.652s
real    0m1.677s
real    0m1.973s
real    0m1.435s
real    0m1.879s
real    0m2.003s
real    0m1.695s
real    0m2.343s
real    0m1.899s
real    0m1.809s
real    0m1.790s
real    0m2.115s
```

Judging from that, it would have probably taken about half an hour
for a single run, which makes at least the `--solid` wim image pretty
much unusable for actually working with the file system.

The `--solid` option was suggested to me because it resembles the way
that DwarFS actually organizes data internally. However, judging by the
warning when mounting a solid image, it's probably not ideal when using
the image as a mounted file system. So I tried again without `--solid`:

```
$ time wimcapture --unix-data install perl-install-nonsolid.wim
Scanning "install"
47 GiB scanned (1927501 files, 330733 directories)
Using LZX compression with 16 threads
Archiving file data: 19 GiB of 19 GiB (100%) done

real    8m39.034s
user    64m58.575s
sys     0m32.003s
```

This is still more than 3 minutes slower than `mkdwarfs`. However, it
yields an image that's almost 10 times the size of the DwarFS image
and comparable in size to the SquashFS image:

```
$ ll perl-install-nonsolid.wim -h
-rw-r--r-- 1 mhx users 4.6G Mar  6 23:24 perl-install-nonsolid.wim
```

This *still* takes surprisingly long to mount:

```
$ time wimmount perl-install-nonsolid.wim mnt

real    0m1.603s
user    0m1.327s
sys     0m0.275s
```

However, it's really usable as a file system, even though it's about
4-5 times slower than the DwarFS image:

```
$ hyperfine -c 'umount mnt' -p 'umount mnt; dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1' -n dwarfs "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'" -p 'umount mnt; wimmount perl-install-nonsolid.wim mnt; sleep 1' -n wimlib "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'"
Benchmark #1: dwarfs
  Time (mean ± σ):      1.149 s ±  0.019 s    [User: 2.147 s, System: 0.739 s]
  Range (min … max):    1.122 s …  1.187 s    10 runs

Benchmark #2: wimlib
  Time (mean ± σ):      7.542 s ±  0.069 s    [User: 2.787 s, System: 0.694 s]
  Range (min … max):    7.490 s …  7.732 s    10 runs

Summary
  'dwarfs' ran
    6.56 ± 0.12 times faster than 'wimlib'
```

### With Cromfs

I used [Cromfs](https://bisqwit.iki.fi/source/cromfs.html) in the past
for compressed file systems and remember that it did a pretty good job
in terms of compression ratio. But it was never fast. However, I didn't
quite remember just *how* slow it was until I tried to set up a test.

Here's a run on the Perl dataset, with the block size set to 16 MiB to
match the default of DwarFS, and with additional options suggested to
speed up compression:

```
$ time mkcromfs -f 16777216 -qq -e -r100000 install perl-install.cromfs
Writing perl-install.cromfs...
mkcromfs: Automatically enabling --24bitblocknums because it seems possible for this filesystem.
Root pseudo file is 108 bytes
Inotab spans 0x7f3a18259000..0x7f3a1bfffb9c
Root inode spans 0x7f3a205d2948..0x7f3a205d294c
Beginning task for Files and directories: Finding identical blocks
2163608 reuse opportunities found. 561362 unique blocks. Block table will be 79.4% smaller than without the index search.
Beginning task for Files and directories: Blockifying
Blockifying:  0.04% (140017/2724970) idx(siz=80423,del=0) rawin(20.97 MB)rawout(20.97 MB)diff(1956 bytes)
Termination signalled, cleaning up temporaries

real    29m9.634s
user    201m37.816s
sys     2m15.005s
```

So it processed 21 MiB out of 48 GiB in half an hour, using almost
twice as much CPU resources as DwarFS for the *whole* file system.
At this point I decided it's likely not worth waiting (presumably)
another month (!) for `mkcromfs` to finish. I double checked that
I didn't accidentally build a debugging version, `mkcromfs` was
definitely built with `-O3`.

I then tried once more with a smaller version of the Perl dataset.
This only has 20 versions (instead of 1139) of Perl, and obviously
a lot less redundancy:

```
$ time mkcromfs -f 16777216 -qq -e -r100000 install-small perl-install.cromfs
Writing perl-install.cromfs...
mkcromfs: Automatically enabling --16bitblocknums because it seems possible for this filesystem.
Root pseudo file is 108 bytes
Inotab spans 0x7f00e0774000..0x7f00e08410a8
Root inode spans 0x7f00b40048f8..0x7f00b40048fc
Beginning task for Files and directories: Finding identical blocks
25362 reuse opportunities found. 9815 unique blocks. Block table will be 72.1% smaller than without the index search.
Beginning task for Files and directories: Blockifying
Compressing raw rootdir inode (28 bytes)z=982370,del=2) rawin(641.56 MB)rawout(252.72 MB)diff(388.84 MB)
 compressed into 35 bytes
INOTAB pseudo file is 839.85 kB
Inotab inode spans 0x7f00bc036ed8..0x7f00bc036ef4
Beginning task for INOTAB: Finding identical blocks
0 reuse opportunities found. 13 unique blocks. Block table will be 0.0% smaller than without the index search.
Beginning task for INOTAB: Blockifying
mkcromfs: Automatically enabling --packedblocks because it is possible for this filesystem.
Compressing raw inotab inode (52 bytes)
 compressed into 58 bytes
Compressing 9828 block records (4 bytes each, total 39312 bytes)
 compressed into 15890 bytes
Compressing and writing 16 fblocks...

16 fblocks were written: 35.31 MB = 13.90 % of 254.01 MB
Filesystem size: 35.33 MB = 5.50 % of original 642.22 MB
End

real    27m38.833s
user    277m36.208s
sys     11m36.945s
```

And repeating the same task with `mkdwarfs`:

```
$ time mkdwarfs -i install-small -o perl-install-small.dwarfs
21:13:38.131724 scanning install-small
21:13:38.320139 waiting for background scanners...
21:13:38.727024 assigning directory and link inodes...
21:13:38.731807 finding duplicate files...
21:13:38.832524 saved 267.8 MiB / 611.8 MiB in 22842/26401 duplicate files
21:13:38.832598 waiting for inode scanners...
21:13:39.619963 assigning device inodes...
21:13:39.620855 assigning pipe/socket inodes...
21:13:39.621356 building metadata...
21:13:39.621453 building blocks...
21:13:39.621472 saving names and links...
21:13:39.621655 ordering 3559 inodes using nilsimsa similarity...
21:13:39.622031 nilsimsa: depth=20000, limit=255
21:13:39.629206 updating name and link indices...
21:13:39.630142 pre-sorted index (3360 name, 2127 path lookups) [8.014ms]
21:13:39.752051 3559 inodes ordered [130.3ms]
21:13:39.752101 waiting for segmenting/blockifying to finish...
21:13:53.250951 saving chunks...
21:13:53.251581 saving directories...
21:13:53.303862 waiting for compression to finish...
21:14:11.073273 compressed 611.8 MiB to 24.01 MiB (ratio=0.0392411)
21:14:11.091099 filesystem created without errors [32.96s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
3334 dirs, 0/0 soft/hard links, 26401/26401 files, 0 other
original size: 611.8 MiB, dedupe: 267.8 MiB (22842 files), segment: 121.5 MiB
filesystem: 222.5 MiB in 14 blocks (7177 chunks, 3559/3559 inodes)
compressed filesystem: 14 blocks/24.01 MiB written
██████████████████████████████████████████████████████████████████████▏100% \

real    0m33.007s
user    3m43.324s
sys     0m4.015s
```

So `mkdwarfs` is about 50 times faster than `mkcromfs` and uses 75 times
less CPU resources. At the same time, the DwarFS file system is 30% smaller:

```
$ ls -l perl-install-small.*fs
-rw-r--r-- 1 mhx users 35328512 Dec  8 14:25 perl-install-small.cromfs
-rw-r--r-- 1 mhx users 25175016 Dec 10 21:14 perl-install-small.dwarfs
```

I noticed that the `blockifying` step that took ages for the full dataset
with `mkcromfs` ran substantially faster (in terms of MiB/second) on the
smaller dataset, which makes me wonder if there's some quadratic complexity
behaviour that's slowing down `mkcromfs`.

In order to be completely fair, I also ran `mkdwarfs` with `-l 9` to enable
LZMA compression (which is what `mkcromfs` uses by default):

```
$ time mkdwarfs -i install-small -o perl-install-small-l9.dwarfs -l 9
21:16:21.874975 scanning install-small
21:16:22.092201 waiting for background scanners...
21:16:22.489470 assigning directory and link inodes...
21:16:22.495216 finding duplicate files...
21:16:22.611221 saved 267.8 MiB / 611.8 MiB in 22842/26401 duplicate files
21:16:22.611314 waiting for inode scanners...
21:16:23.394332 assigning device inodes...
21:16:23.395184 assigning pipe/socket inodes...
21:16:23.395616 building metadata...
21:16:23.395676 building blocks...
21:16:23.395685 saving names and links...
21:16:23.395830 ordering 3559 inodes using nilsimsa similarity...
21:16:23.396097 nilsimsa: depth=50000, limit=255
21:16:23.401042 updating name and link indices...
21:16:23.403127 pre-sorted index (3360 name, 2127 path lookups) [6.936ms]
21:16:23.524914 3559 inodes ordered [129ms]
21:16:23.525006 waiting for segmenting/blockifying to finish...
21:16:33.865023 saving chunks...
21:16:33.865883 saving directories...
21:16:33.900140 waiting for compression to finish...
21:17:10.505779 compressed 611.8 MiB to 17.44 MiB (ratio=0.0284969)
21:17:10.526171 filesystem created without errors [48.65s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
3334 dirs, 0/0 soft/hard links, 26401/26401 files, 0 other
original size: 611.8 MiB, dedupe: 267.8 MiB (22842 files), segment: 122.2 MiB
filesystem: 221.8 MiB in 4 blocks (7304 chunks, 3559/3559 inodes)
compressed filesystem: 4 blocks/17.44 MiB written
██████████████████████████████████████████████████████████████████████▏100% /

real    0m48.683s
user    2m24.905s
sys     0m3.292s
```

```
$ ls -l perl-install-small*.*fs
-rw-r--r-- 1 mhx users 18282075 Dec 10 21:17 perl-install-small-l9.dwarfs
-rw-r--r-- 1 mhx users 35328512 Dec  8 14:25 perl-install-small.cromfs
-rw-r--r-- 1 mhx users 25175016 Dec 10 21:14 perl-install-small.dwarfs
```

It takes about 15 seconds longer to build the DwarFS file system with LZMA
compression (this is still 35 times faster than Cromfs), but reduces the
size even further to make it almost half the size of the Cromfs file system.

I would have added some benchmarks with the Cromfs FUSE driver, but sadly
it crashed right upon trying to list the directory after mounting.

### With EROFS

[EROFS](https://github.com/hsiangkao/erofs-utils) is a new read-only
compressed file system that has recently been added to the Linux kernel.
Its goals are quite different from those of DwarFS, though. It is
designed to be lightweight (which DwarFS is definitely not) and to run
on constrained hardware like embedded devices or smartphones. It only
supports LZ4 compression.

I was feeling lucky and decided to run it on the full Perl dataset:

```
$ time mkfs.erofs perl-install.erofs install -zlz4hc,9 -d2
mkfs.erofs 1.2
        c_version:           [     1.2]
        c_dbg_lvl:           [       2]
        c_dry_run:           [       0]
^C

real    912m42.601s
user    903m2.777s
sys     1m52.812s
```

As you can tell, after more than 15 hours I just gave up. In those
15 hours, `mkfs.erofs` had produced a 13 GiB output file:

```
$ ll -h perl-install.erofs
-rw-r--r-- 1 mhx users 13G Dec  9 14:42 perl-install.erofs
```

I don't think this would have been very useful to compare with DwarFS.

Just as for Cromfs, I re-ran with the smaller Perl dataset:

```
$ time mkfs.erofs perl-install-small.erofs install-small -zlz4hc,9 -d2
mkfs.erofs 1.2
        c_version:           [     1.2]
        c_dbg_lvl:           [       2]
        c_dry_run:           [       0]

real    0m27.844s
user    0m20.570s
sys     0m1.848s
```

That was surprisingly quick, which makes me think that, again, there
might be some accidentally quadratic complexity hiding in `mkfs.erofs`.
The output file it produced is an order of magnitude larger than the
DwarFS image:

```
$ ls -l perl-install-small.*fs
-rw-r--r-- 1 mhx users  26928161 Dec  8 15:05 perl-install-small.dwarfs
-rw-r--r-- 1 mhx users 296488960 Dec  9 14:45 perl-install-small.erofs
```

Admittedly, this isn't a fair comparison. EROFS has a fixed block size
of 4 KiB, and it uses LZ4 compression. If we tweak DwarFS to the same
parameters, we get:

```
$ time mkdwarfs -i install-small -o perl-install-small-lz4.dwarfs -C lz4hc:level=9 -S 12
21:21:18.136796 scanning install-small
21:21:18.376998 waiting for background scanners...
21:21:18.770703 assigning directory and link inodes...
21:21:18.776422 finding duplicate files...
21:21:18.903505 saved 267.8 MiB / 611.8 MiB in 22842/26401 duplicate files
21:21:18.903621 waiting for inode scanners...
21:21:19.676420 assigning device inodes...
21:21:19.677400 assigning pipe/socket inodes...
21:21:19.678014 building metadata...
21:21:19.678101 building blocks...
21:21:19.678116 saving names and links...
21:21:19.678306 ordering 3559 inodes using nilsimsa similarity...
21:21:19.678566 nilsimsa: depth=20000, limit=255
21:21:19.684227 pre-sorted index (3360 name, 2127 path lookups) [5.592ms]
21:21:19.685550 updating name and link indices...
21:21:19.810401 3559 inodes ordered [132ms]
21:21:19.810519 waiting for segmenting/blockifying to finish...
21:21:26.773913 saving chunks...
21:21:26.776832 saving directories...
21:21:26.821085 waiting for compression to finish...
21:21:27.020929 compressed 611.8 MiB to 140.7 MiB (ratio=0.230025)
21:21:27.036202 filesystem created without errors [8.899s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
3334 dirs, 0/0 soft/hard links, 26401/26401 files, 0 other
original size: 611.8 MiB, dedupe: 267.8 MiB (22842 files), segment: 0 B
filesystem: 344 MiB in 88073 blocks (91628 chunks, 3559/3559 inodes)
compressed filesystem: 88073 blocks/140.7 MiB written
████████████████████████████████████████████████████████████████▏100% |

real    0m9.075s
user    0m37.718s
sys     0m2.427s
```

It finishes in less than half the time and produces an output image
that's half the size of the EROFS image.

I'm going to stop the comparison here, as it's pretty obvious that the
domains in which EROFS and DwarFS are being used have extremely little
overlap. DwarFS will likely never be able to run on embedded devices
and EROFS will likely never be able to achieve the compression ratios
of DwarFS.

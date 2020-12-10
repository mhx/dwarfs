[![Build Status](https://travis-ci.com/mhx/dwarfs.svg?branch=main)](https://travis-ci.com/mhx/dwarfs)

# DwarFS

A fast high compression read-only file system

## Table of contents

* [Overview](#overview)
* [History](#history)
* [Building and Installing](#building-and-installing)
   * [Dependencies](#dependencies)
   * [Building](#building)
   * [Installing](#installing)
   * [Experimental Python Scripting Support](#experimental-python-scripting-support)
* [Usage](#usage)
* [Comparison](#comparison)
   * [With SquashFS](#with-squashfs)
   * [With SquashFS &amp; xz](#with-squashfs--xz)
   * [With wimlib](#with-wimlib)
   * [With Cromfs](#with-cromfs)
   * [With EROFS](#with-erofs)

## Overview

![Alt text](doc/screenshot.png?raw=true "DwarFS Screenshot")

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
than SquashFS compression**, it's **4 times faster to build the file
system**, it's typically faster to access files on DwarFS and it uses
less CPU resources.

Distinct features of DwarFS are:

* Clustering of files by similarity using a similarity hash function.
  This makes it easier to exploit the redundancy across file boundaries.

* Segmentation analysis across file system blocks in order to reduce
  the size of the uncompressed file system. This saves memory when
  using the compressed file system and thus potentially allows for
  higher cache hit rates as more data can be kept in the cache.

* Highly multi-threaded implementation. Both the file
  [system creation tool](doc/mkdwarfs.md) as well as the
  [FUSE driver](doc/dwarfs.md) are able to make good use of the
  many cores of your system.

* Optional experimental Lua support to provide custom filtering and
  ordering functionality.

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

    $ apt install \
        g++ \
        clang \
        cmake \
        make \
        bison \
        flex \
        ronn \
        pkg-config \
        binutils-dev \
        libboost-context-dev \
        libboost-filesystem-dev \
        libboost-program-options-dev \
        libboost-python-dev \
        libboost-regex-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libevent-dev \
        libdouble-conversion-dev \
        libiberty-dev \
        liblz4-dev \
        liblzma-dev \
        libssl-dev \
        libunwind-dev \
        libfmt-dev \
        libfuse3-dev \
        libgoogle-glog-dev \
        libsparsehash-dev

You can pick either `clang` or `g++`, but at least recent `clang`
versions will produce substantially faster code:

    $ hyperfine ./dwarfs_test-*
    Benchmark #1: ./dwarfs_test-clang-O2
      Time (mean ± σ):      9.425 s ±  0.049 s    [User: 15.724 s, System: 0.773 s]
      Range (min … max):    9.373 s …  9.523 s    10 runs
     
    Benchmark #2: ./dwarfs_test-clang-O3
      Time (mean ± σ):      9.328 s ±  0.045 s    [User: 15.593 s, System: 0.791 s]
      Range (min … max):    9.277 s …  9.418 s    10 runs
     
    Benchmark #3: ./dwarfs_test-gcc-O2
      Time (mean ± σ):     13.798 s ±  0.035 s    [User: 20.161 s, System: 0.767 s]
      Range (min … max):   13.731 s … 13.852 s    10 runs
     
    Benchmark #4: ./dwarfs_test-gcc-O3
      Time (mean ± σ):     13.223 s ±  0.034 s    [User: 19.576 s, System: 0.769 s]
      Range (min … max):   13.176 s … 13.278 s    10 runs
     
    Summary
      './dwarfs_test-clang-O3' ran
        1.01 ± 0.01 times faster than './dwarfs_test-clang-O2'
        1.42 ± 0.01 times faster than './dwarfs_test-gcc-O3'
        1.48 ± 0.01 times faster than './dwarfs_test-gcc-O2'

    $ hyperfine -L prog $(echo mkdwarfs-* | tr ' ' ,) '{prog} --no-progress --log-level warn -i tree -o /dev/null -C null'
    Benchmark #1: mkdwarfs-clang-O2 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      4.358 s ±  0.033 s    [User: 6.364 s, System: 0.622 s]
      Range (min … max):    4.321 s …  4.408 s    10 runs
     
    Benchmark #2: mkdwarfs-clang-O3 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      4.282 s ±  0.035 s    [User: 6.249 s, System: 0.623 s]
      Range (min … max):    4.244 s …  4.349 s    10 runs
     
    Benchmark #3: mkdwarfs-gcc-O2 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      6.212 s ±  0.031 s    [User: 8.185 s, System: 0.638 s]
      Range (min … max):    6.159 s …  6.250 s    10 runs
     
    Benchmark #4: mkdwarfs-gcc-O3 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      5.740 s ±  0.037 s    [User: 7.742 s, System: 0.645 s]
      Range (min … max):    5.685 s …  5.796 s    10 runs
     
    Summary
      'mkdwarfs-clang-O3 --no-progress --log-level warn -i tree -o /dev/null -C null' ran
        1.02 ± 0.01 times faster than 'mkdwarfs-clang-O2 --no-progress --log-level warn -i tree -o /dev/null -C null'
        1.34 ± 0.01 times faster than 'mkdwarfs-gcc-O3 --no-progress --log-level warn -i tree -o /dev/null -C null'
        1.45 ± 0.01 times faster than 'mkdwarfs-gcc-O2 --no-progress --log-level warn -i tree -o /dev/null -C null'

These measurements were made with gcc-9.3.0 and clang-10.0.1.

### Building

Firstly, either clone the repository...

    $ git clone --recurse-submodules https://github.com/mhx/dwarfs
    $ cd dwarfs

...or unpack the release archive:

    $ tar xvf dwarfs-x.y.z.tar.bz2
    $ cd dwarfs-x.y.z

Once all dependencies have been installed, you can build DwarFS
using:

    $ mkdir build
    $ cd build
    $ cmake .. -DWITH_TESTS=1
    $ make -j$(nproc)

If possible, try building with clang as your compiler, this will
make DwarFS significantly faster. If you have both gcc and clang
installed, use:

    $ CC=clang CXX=clang++ cmake .. -DWITH_TESTS=1

To build with experimental Lua support, you need to install both
`lua` and `luabind`. The latter isn't very well maintained and I
hope to get rid of the dependency in the future. Add `-DWITH_LUA=1`
to the `cmake` command line to enable Lua support.

You can then run tests with:

    $ make test

### Installing

Installing is as easy as:

    $ sudo make install

Though you don't have to install the tools to play with them.

### Experimental Python Scripting Support

You can build `mkdwarfs` with experimental support for Python
scripting:

    $ cmake .. -DWITH_TESTS=1 -DWITH_PYTHON=1

This also requires Boost.Python. If you have multiple Python
versions installed, you can explicitly specify the version to
build against:

    $ cmake .. -DWITH_TESTS=1 -DWITH_PYTHON=1 -DWITH_PYTHON_VERSION=3.8

Note that only Python 3 is supported. You can take a look at
[scripts/example.py](scripts/example.py) to get an idea for
what can currently be done with the interface.

## Usage

Please check out the man pages for [mkdwarfs](doc/mkdwarfs.md)
and [dwarfs](doc/dwarfs.md). `dwarfsck` will be built and installed
as well, but it's still work in progress.

The [dwarfs](doc/dwarfs.md) man page also shows an example for setting
up DwarFS with [overlayfs](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt)
in order to create a writable file system mount on top a read-only
DwarFS image.

## Comparison

### With SquashFS

These tests were done on an Intel(R) Xeon(R) CPU D-1528 @ 1.90GHz
6 core CPU with 64 GiB of RAM. The system was mostly idle during
all of the tests.

The source directory contained **1139 different Perl installations**
from 284 distinct releases, a total of 47.65 GiB of data in 1,927,501
files and 330,733 directories. The source directory was freshly
unpacked from a tar archive to a 850 EVO 1TB SSD, so most of its
contents were likely cached.

I'm using the same compression type and compression level for
SquashFS that is the default setting for DwarFS:

    $ time mksquashfs install perl-install.squashfs -comp zstd -Xcompression-level 22
    Parallel mksquashfs: Using 12 processors
    Creating 4.0 filesystem on perl-install.squashfs, block size 131072.
    [=====================================================================-] 2107401/2107401 100%
    
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
    
    real    69m18.427s
    user    817m15.199s
    sys     1m38.237s

For DwarFS, I'm sticking to the defaults:

    $ time mkdwarfs -i install -o perl-install.dwarfs
    18:06:32.393073 scanning install
    18:06:50.157744 waiting for background scanners...
    18:07:24.659010 assigning directory and link inodes...
    18:07:25.056728 finding duplicate files...
    18:07:41.914170 saved 28.2 GiB / 47.65 GiB in 1782826/1927501 duplicate files
    18:07:41.914243 waiting for inode scanners...
    18:08:16.065580 assigning device inodes...
    18:08:16.126759 assigning pipe/socket inodes...
    18:08:16.185546 building metadata...
    18:08:16.185628 building blocks...
    18:08:16.185730 saving names and links...
    18:08:16.186987 ordering 144675 inodes using nilsimsa similarity...
    18:08:16.196982 nilsimsa: depth=20000, limit=255
    18:08:16.665114 updating name and link indices...
    18:08:17.134243 pre-sorted index (1016186 name, 639519 path lookups) [937.2ms]
    18:15:49.332792 144675 inodes ordered [453.1s]
    18:15:49.332890 waiting for segmenting/blockifying to finish...
    18:19:33.746355 saving chunks...
    18:19:33.779313 saving directories...
    18:19:38.284634 waiting for compression to finish...
    18:20:47.316245 compressed 47.65 GiB to 471.7 MiB (ratio=0.00966738)
    18:20:48.027411 filesystem created without errors [855.6s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    waiting for block compression to finish
    330733 dirs, 0/2440 soft/hard links, 1927501/1927501 files, 0 other
    original size: 47.65 GiB, dedupe: 28.2 GiB (1782826 files), segment: 12.62 GiB
    filesystem: 6.832 GiB in 438 blocks (477878 chunks, 144675/144675 inodes)
    compressed filesystem: 438 blocks/471.7 MiB written
    ███████████████████████████████████████████████████████████████████████▏100% /
    
    real    14m15.783s
    user    133m57.608s
    sys     2m52.546s

So in this comparison, `mkdwarfs` is almost 5 times faster than `mksquashfs`.
In total CPU time, it actually uses 6 times less CPU resources.

    $ ls -l perl-install.*fs
    -rw-r--r-- 1 mhx users  494602224 Dec 10 18:20 perl-install.dwarfs
    -rw-r--r-- 1 mhx users 4748902400 Nov 25 00:37 perl-install.squashfs

In terms of compression ratio, the **DwarFS file system is almost 10 times
smaller than the SquashFS file system**. With DwarFS, the content has been
**compressed down to less than 1% (!) of its original size**. This compression
ratio only considers the data stored in the individual files, not the actual
disk space used. On the original EXT4 file system, according to `du`, the
source folder uses 54 GiB, so **the DwarFS image actually only uses 0.85% of
the original space**.

When using identical block sizes for both file systems, the difference,
quite expectedly, becomes a lot less dramatic:

    $ time mksquashfs install perl-install-1M.squashfs -comp zstd -Xcompression-level 22 -b 1M
    
    real    41m55.004s
    user    340m30.012s
    sys     1m47.945s

    $ time mkdwarfs -i install -o perl-install-1M.dwarfs -S 20
    
    real    24m38.027s
    user    282m37.305s
    sys     2m37.558s

    $ ls -l perl-install-1M.*
    -rw-r--r-- 1 mhx users 2953052798 Dec 10 18:47 perl-install-1M.dwarfs
    -rw-r--r-- 1 mhx users 4198944768 Nov 30 10:05 perl-install-1M.squashfs

But the point is that this is really where SquashFS tops out, as it doesn't
support larger block sizes. And as you'll see below, the larger blocks that
DwarFS is using don't necessarily negatively impact performance.

DwarFS also features an option to recompress an existing file system with
a different compression algorithm. This can be useful as it allows relatively
fast experimentation with different algorithms and options without requiring
a full rebuild of the file system. For example, recompressing the above file
system with the best possible compression (`-l 9`):

    $ time mkdwarfs --recompress -i perl-install.dwarfs -o perl-lzma.dwarfs -l 9
    20:44:43.738823 filesystem rewritten [385.4s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    filesystem: 6.832 GiB in 438 blocks (0 chunks, 0 inodes)
    compressed filesystem: 438/438 blocks/408.9 MiB written
    █████████████████████████████████████████████████████████████████████▏100% |
    
    real    6m25.474s
    user    73m0.298s
    sys     1m37.701s

    $ ls -l perl-*.dwarfs
    -rw-r--r-- 1 mhx users 494602224 Dec 10 18:20 perl-install.dwarfs
    -rw-r--r-- 1 mhx users 428802416 Dec 10 20:44 perl-lzma.dwarfs

This reduces the file system size by another 13%, pushing the total
compression ratio to 0.84% (or 0.74% when considering disk usage).

In terms of how fast the file system is when using it, a quick test
I've done is to freshly mount the filesystem created above and run
each of the 1139 `perl` executables to print their version.

    $ hyperfine -c "umount mnt" -p "umount mnt; dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
    Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P5 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):      4.092 s ±  0.031 s    [User: 2.183 s, System: 4.355 s]
      Range (min … max):    4.022 s …  4.122 s    10 runs
     
    Benchmark #2: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):      2.698 s ±  0.027 s    [User: 1.979 s, System: 3.977 s]
      Range (min … max):    2.657 s …  2.732 s    10 runs
     
    Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P15 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):      2.341 s ±  0.029 s    [User: 1.883 s, System: 3.794 s]
      Range (min … max):    2.303 s …  2.397 s    10 runs
     
    Benchmark #4: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):      2.207 s ±  0.037 s    [User: 1.818 s, System: 3.673 s]
      Range (min … max):    2.163 s …  2.278 s    10 runs

These timings are for *initial* runs on a freshly mounted file system,
running 5, 10, 15 and 20 processes in parallel. 2.2 seconds means that
it takes only about 2 milliseconds per Perl binary.

Following are timings for *subsequent* runs, both on DwarFS (at `mnt`)
and the original EXT4 (at `install`). DwarFS is around 15% slower here:

    $ hyperfine -P procs 10 20 -D 10 -w1 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'" "ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
    Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     655.8 ms ±   5.5 ms    [User: 1.716 s, System: 2.784 s]
      Range (min … max):   647.6 ms … 664.3 ms    10 runs
     
    Benchmark #2: ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     583.9 ms ±   5.0 ms    [User: 1.715 s, System: 2.773 s]
      Range (min … max):   577.0 ms … 592.0 ms    10 runs
     
    Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     638.2 ms ±  10.7 ms    [User: 1.667 s, System: 2.736 s]
      Range (min … max):   629.1 ms … 658.4 ms    10 runs
     
    Benchmark #4: ls -1 install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     567.0 ms ±   3.2 ms    [User: 1.684 s, System: 2.719 s]
      Range (min … max):   561.5 ms … 570.5 ms    10 runs

Using the lzma-compressed file system, the metrics for *initial* runs look
considerably worse:

    $ hyperfine -c "umount mnt" -p "umount mnt; dwarfs perl-lzma.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
    Benchmark #1: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P5 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     20.372 s ±  0.135 s    [User: 2.338 s, System: 4.511 s]
      Range (min … max):   20.208 s … 20.601 s    10 runs
     
    Benchmark #2: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P10 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     13.015 s ±  0.094 s    [User: 2.148 s, System: 4.120 s]
      Range (min … max):   12.863 s … 13.144 s    10 runs
     
    Benchmark #3: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P15 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     11.533 s ±  0.058 s    [User: 2.013 s, System: 3.970 s]
      Range (min … max):   11.469 s … 11.649 s    10 runs
     
    Benchmark #4: ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '$0 -v >/dev/null'
      Time (mean ± σ):     11.402 s ±  0.095 s    [User: 1.906 s, System: 3.787 s]
      Range (min … max):   11.297 s … 11.568 s    10 runs

So you might want to consider using zstd instead of lzma if you'd
like to optimize for file system performance. It's also the default
compression used by `mkdwarfs`.

On a different system, Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz,
with 4 cores, I did more tests with both SquashFS and DwarFS
(just because on the 6 core box my kernel didn't have support
for zstd in SquashFS):

    hyperfine -c 'sudo umount /tmp/perl/install' -p 'umount /tmp/perl/install; dwarfs perl-install.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4; sleep 1' -n dwarfs-zstd "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'" -p 'sudo umount /tmp/perl/install; sudo mount -t squashfs perl-install.squashfs /tmp/perl/install; sleep 1' -n squashfs-zstd "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'"
    Benchmark #1: dwarfs-zstd
      Time (mean ± σ):      2.071 s ±  0.372 s    [User: 1.727 s, System: 2.866 s]
      Range (min … max):    1.711 s …  2.532 s    10 runs
     
    Benchmark #2: squashfs-zstd
      Time (mean ± σ):      3.668 s ±  0.070 s    [User: 2.173 s, System: 21.287 s]
      Range (min … max):    3.616 s …  3.846 s    10 runs
     
    Summary
      'dwarfs-zstd' ran
        1.77 ± 0.32 times faster than 'squashfs-zstd'

So DwarFS is almost twice as fast as SquashFS. But what's more,
SquashFS also uses significantly more CPU power. However, the numbers
shown above for DwarFS obviously don't include the time spent in the
`dwarfs` process, so I repeated the test outside of hyperfine:

    $ time dwarfs perl-install.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4 -f
    
    real    0m8.463s
    user    0m3.821s
    sys     0m2.117s

So in total, DwarFS was using 10.5 seconds of CPU time, whereas
SquashFS was using 23.5 seconds, more than twice as much. Ignore
the 'real' time, this is only how long it took me to unmount the
file system again after mounting it.

Another real-life test was to build and test a Perl module with 624
different Perl versions in the compressed file system. The module I've
used, [Tie::Hash::Indexed](https://github.com/mhx/Tie-Hash-Indexed),
has an XS component that requires a C compiler to build. So this really
accesses a lot of different stuff in the file system:

* The `perl` executables and its shared libraries

* The Perl modules used for writing the Makefile

* Perl's C header files used for building the module

* More Perl modules used for running the tests

I wrote a little script to be able to run multiple builds in parallel:

```bash
#!/bin/bash
set -eu
perl=$1
dir=$(echo "$perl" | cut -d/ --output-delimiter=- -f5,6)
rsync -a Tie-Hash-Indexed-0.08/ $dir/
cd $dir
$1 Makefile.PL >/dev/null 2>&1
make test >/dev/null 2>&1
cd ..
rm -rf $dir
echo $perl
```

The following command will run up to 8 builds in parallel on the 4 core
i7 CPU, including debug, optimized and threaded versions of all Perl
releases between 5.10.0 and 5.33.3, a total of 624 `perl` installations:

    $ time ls -1 /tmp/perl/install/*/perl-5.??.?/bin/perl5* | sort -t / -k 8 | xargs -d $'\n' -P 8 -n 1 ./build.sh

Tests were done with a cleanly mounted file system to make sure the caches
were empty. `ccache` was primed to make sure all compiler runs could be
satisfied from the cache. With SquashFS, the timing was:

    real    3m17.182s
    user    20m54.064s
    sys     4m16.907s

And with DwarFS:

    real    3m14.402s
    user    19m42.984s
    sys     2m49.292s

So, frankly, not much of a difference. The `dwarfs` process itself used:

    real    4m23.151s
    user    0m25.036s
    sys     0m35.216s

So again, DwarFS used less raw CPU power, but in terms of wallclock time,
the difference is really marginal.

### With SquashFS & xz

This test uses slightly less pathological input data: the root filesystem of
a recent Raspberry Pi OS release. This file system also contains device inodes,
so in order to preserve those, we pass `--with-devices` to `mkdwarfs`:

    $ time sudo mkdwarfs -i raspbian -o raspbian.dwarfs --with-devices
    20:49:45.099221 scanning raspbian
    20:49:45.395243 waiting for background scanners...
    20:49:46.019979 assigning directory and link inodes...
    20:49:46.035099 finding duplicate files...
    20:49:46.148490 saved 31.05 MiB / 1007 MiB in 1617/34582 duplicate files
    20:49:46.149221 waiting for inode scanners...
    20:49:48.518179 assigning device inodes...
    20:49:48.519512 assigning pipe/socket inodes...
    20:49:48.520322 building metadata...
    20:49:48.520425 building blocks...
    20:49:48.520473 saving names and links...
    20:49:48.520568 ordering 32965 inodes using nilsimsa similarity...
    20:49:48.522323 nilsimsa: depth=20000, limit=255
    20:49:48.554803 updating name and link indices...
    20:49:48.577389 pre-sorted index (55243 name, 26489 path lookups) [54.95ms]
    20:50:55.921085 32965 inodes ordered [67.4s]
    20:50:55.921179 waiting for segmenting/blockifying to finish...
    20:51:02.372233 saving chunks...
    20:51:02.376389 saving directories...
    20:51:02.492263 waiting for compression to finish...
    20:51:31.098179 compressed 1007 MiB to 286.6 MiB (ratio=0.284714)
    20:51:31.140186 filesystem created without errors [106s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    waiting for block compression to finish
    4435 dirs, 5908/473 soft/hard links, 34582/34582 files, 7 other
    original size: 1007 MiB, dedupe: 31.05 MiB (1617 files), segment: 46.9 MiB
    filesystem: 928.7 MiB in 59 blocks (39117 chunks, 32965/32965 inodes)
    compressed filesystem: 59 blocks/286.6 MiB written
    ███████████████████████████████████████████████████████████████████▏100% |
    
    real    1m46.153s
    user    18m7.973s
    sys     0m16.013s

Again, SquashFS uses the same compression options:

    $ time sudo mksquashfs raspbian raspbian.squashfs -comp zstd -Xcompression-level 22
    Parallel mksquashfs: Using 12 processors
    Creating 4.0 filesystem on raspbian.squashfs, block size 131072.
    [====================================================================-] 38644/38644 100%
    
    Exportable Squashfs 4.0 filesystem, zstd compressed, data block size 131072
            compressed data, compressed metadata, compressed fragments,
            compressed xattrs, compressed ids
            duplicates are removed
    Filesystem size 371931.65 Kbytes (363.21 Mbytes)
            36.89% of uncompressed filesystem size (1008353.15 Kbytes)
    Inode table size 398565 bytes (389.22 Kbytes)
            26.61% of uncompressed inode table size (1497593 bytes)
    Directory table size 408794 bytes (399.21 Kbytes)
            42.28% of uncompressed directory table size (966980 bytes)
    Number of duplicate files found 1145
    Number of inodes 44459
    Number of files 34109
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
            logitechmediaserver (103)
            shutdown (6)
            x2goprint (106)
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
    
    real    1m54.997s
    user    18m32.386s
    sys     0m2.627s

The difference in speed is almost negligible. SquashFS is just a bit
slower here. In terms of compression, the difference also isn't huge:

    $ ls -lh raspbian.* *.xz
    -rw-r--r-- 1 root root 287M Dec 10 20:51 raspbian.dwarfs
    -rw-r--r-- 1 root root 364M Dec  9 22:31 raspbian.squashfs
    -rw-r--r-- 1 mhx users 297M Aug 20 12:47 2020-08-20-raspios-buster-armhf-lite.img.xz

Interestingly, `xz` actually can't compress the whole original image
better than DwarFS.

We can even again try to increase the DwarFS compression level:

    $ time mkdwarfs -i raspbian.dwarfs -o raspbian-9.dwarfs -l 9 --recompress
    20:55:34.416488 filesystem rewritten [69.79s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    filesystem: 928.7 MiB in 59 blocks (0 chunks, 0 inodes)
    compressed filesystem: 59/59 blocks/257.7 MiB written
    ██████████████████████████████████████████████████████████████████▏100% \
    
    real    1m9.879s
    user    12m52.376s
    sys     0m14.315s

Now that actually gets the DwarFS image size well below that of the
`xz` archive:

    $ ls -lh raspbian-9.dwarfs *.xz
    -rw-r--r-- 1 mhx users 258M Dec 10 20:55 raspbian-9.dwarfs
    -rw-r--r-- 1 mhx users 297M Aug 20 12:47 2020-08-20-raspios-buster-armhf-lite.img.xz

However, if you actually build a tarball and compress that (instead of
compressing the EXT4 file system), `xz` is, unsurprisingly, able to take
the lead again:

    $ time sudo tar cf - raspbian | xz -9e -vT 0 >raspbian.tar.xz
      100 %     245.9 MiB / 1,012.3 MiB = 0.243   5.4 MiB/s       3:07
    
    real    3m8.088s
    user    14m16.519s
    sys     0m5.843s

    $ ls -lh raspbian.tar.xz
    -rw-r--r-- 1 mhx users 246M Nov 30 00:16 raspbian.tar.xz

In summary, DwarFS can get pretty close to an `xz` compressed tarball
in terms of size. It's also almsot three times faster to build the file
system than to build the tarball. At the same time, SquashFS really
isn't that much worse. It's really the cases where you *know* upfront
that your data is highly redundant where DwarFS can play out its full
strength.

### With wimlib

[wimlib](https://wimlib.net/) is a really interesting project that is
a lot more mature than DwarFS. While DwarFS at its core has a library
component that could potentially be ported to other operating systems,
wimlib already is available on many platforms. It also seems to have
quite a rich set of features, so it's definitely worth taking a look at.

I first tried `wimcapture` on the perl dataset:

    $ time wimcapture --unix-data --solid --solid-chunk-size=16M install perl-install.wim
    Scanning "install"
    47 GiB scanned (1927501 files, 330733 directories)    
    Using LZMS compression with 12 threads
    Archiving file data: 19 GiB of 19 GiB (100%) done
    
    real    21m38.857s
    user    191m53.452s
    sys     1m2.743s

    $ ll perl-install.*
    -rw-r--r-- 1 mhx users  494602224 Dec 10 18:20 perl-install.dwarfs
    -rw-r--r-- 1 mhx users 1016971956 Dec  6 00:12 perl-install.wim
    -rw-r--r-- 1 mhx users 4748902400 Nov 25 00:37 perl-install.squashfs

So wimlib is definitely much better than squashfs, in terms of both
compression ratio and speed. DwarFS is still about 35% faster to create
the file system and the DwarFS file system less than half the size.
When switching to LZMA compression, the DwarFS file system is almost
60% smaller (wimlib uses LZMS compression by default).

What's a bit surprising is that mounting a *wim* file takes quite a bit
of time:

    $ time wimmount perl-install.wim mnt
    [WARNING] Mounting a WIM file containing solid-compressed data; file access may be slow.
    
    real    0m2.371s
    user    0m2.034s
    sys     0m0.335s

Mounting the DwarFS image takes almost no time in comparison:

    $ time dwarfs perl-install.dwarfs mnt
    00:36:42.626580 dwarfs (0.3.0)
    
    real    0m0.010s
    user    0m0.001s
    sys     0m0.008s

That's just because it immediately forks into background by default and
initializes the file system in the background. However, even when
running it in the foreground, initializing the file system takes only
slightly longer than 100 milliseconds:

    $ dwarfs perl-install.dwarfs mnt -f
    21:01:34.554090 dwarfs (0.3.0)
    21:01:34.695661 file system initialized [137.9ms]

If you actually build the DwarFS file system with uncompressed metadata,
mounting is basically instantaneous:

    $ dwarfs perl-install-meta.dwarfs mnt -f
    00:35:44.975437 dwarfs (0.3.0)
    00:35:44.987450 file system initialized [5.064ms]

I've tried running the benchmark where all 1139 `perl` executables
print their version with the wimlib image, but after about 10 minutes,
it still hadn't finished the first run (with the DwarFS image, one run
took slightly more than 2 seconds). I then tried the following instead:

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

Judging from that, it would have probably taken about half an hour
for a single run, which makes at least the `--solid` wim image pretty
much unusable for actually working with the file system.

The `--solid` option was suggested to me because it resembles the way
that DwarFS actually organizes data internally. However, judging by the
warning when mounting a solid image, it's probably not ideal when using
the image as a mounted file system. So I tried again without `--solid`:

    $ time wimcapture --unix-data install perl-install-nonsolid.wim
    Scanning "install"
    47 GiB scanned (1927501 files, 330733 directories)
    Using LZX compression with 12 threads
    Archiving file data: 19 GiB of 19 GiB (100%) done
    
    real    12m14.515s
    user    107m14.962s
    sys     0m50.042s

This is actually about 3 minutes faster than `mkdwarfs`. However, it
yields an image that's almost 10 times the size of the DwarFS image
and comparable in size to the SquashFS image:

    $ ll perl-install-nonsolid.wim -h
    -rw-r--r-- 1 mhx users 4.6G Dec  6 00:58 perl-install-nonsolid.wim

This *still* takes surprisingly long to mount:

    $ time wimmount perl-install-nonsolid.wim /tmp/perl/install
    
    real    0m2.029s
    user    0m1.635s
    sys     0m0.383s

However, it's really usable as a file system, even though it's about
4-5 times slower than the DwarFS image:

    $ hyperfine -c 'umount /tmp/perl/install' -p 'umount /tmp/perl/install; dwarfs perl-install.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4; sleep 1' -n dwarfs "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'" -p 'umount /tmp/perl/install; wimmount perl-install-nonsolid.wim /tmp/perl/install; sleep 1' -n wimlib "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'"
    Benchmark #1: dwarfs
      Time (mean ± σ):      2.295 s ±  0.362 s    [User: 1.823 s, System: 3.173 s]
      Range (min … max):    1.813 s …  2.606 s    10 runs
     
    Benchmark #2: wimlib
      Time (mean ± σ):     10.418 s ±  0.286 s    [User: 1.510 s, System: 2.208 s]
      Range (min … max):   10.134 s … 10.854 s    10 runs
     
    Summary
      'dwarfs' ran
        4.54 ± 0.73 times faster than 'wimlib'

### With Cromfs

I used [Cromfs](https://bisqwit.iki.fi/source/cromfs.html) in the past
for compressed file systems and remember that it did a pretty good job
in terms of compression ratio. But it was never fast. However, I didn't
quite remember just *how* slow it was until I tried to set up a test.

Here's a run on the Perl dataset, with the block size set to 16 MiB to
match the default of DwarFS, and with additional options suggested to
speed up compression:

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

So it processed 21 MiB out of 48 GiB in half an hour, using almost
twice as much CPU resources as DwarFS for the *whole* file system.
At this point I decided it's likely not worth waiting (presumably)
another month (!) for `mkcromfs` to finish. I double checked that
I didn't accidentally build a debugging version, `mkcromfs` was
definitely built with `-O3`.

I then tried once more with a smaller version of the Perl dataset.
This only has 20 versions (instead of 1139) of Perl, and obviously
a lot less redundancy:

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

And repeating the same task with `mkdwarfs`:

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

So `mkdwarfs` is about 50 times faster than `mkcromfs` and uses 75 times
less CPU resources. At the same time, the DwarFS file system is 30% smaller:

    $ ls -l perl-install-small.*fs
    -rw-r--r-- 1 mhx users 35328512 Dec  8 14:25 perl-install-small.cromfs
    -rw-r--r-- 1 mhx users 25175016 Dec 10 21:14 perl-install-small.dwarfs

I noticed that the `blockifying` step that took ages for the full dataset
with `mkcromfs` ran substantially faster (in terms of MiB/second) on the
smaller dataset, which makes me wonder if there's some quadratic complexity
behaviour that's slowing down `mkcromfs`.

In order to be completely fair, I also ran `mkdwarfs` with `-l 9` to enable
LZMA compression (which is what `mkcromfs` uses by default):

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

    $ ls -l perl-install-small*.*fs
    -rw-r--r-- 1 mhx users 18282075 Dec 10 21:17 perl-install-small-l9.dwarfs
    -rw-r--r-- 1 mhx users 35328512 Dec  8 14:25 perl-install-small.cromfs
    -rw-r--r-- 1 mhx users 25175016 Dec 10 21:14 perl-install-small.dwarfs

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

    $ time mkfs.erofs perl-install.erofs install -zlz4hc,9 -d2
    mkfs.erofs 1.2
            c_version:           [     1.2]
            c_dbg_lvl:           [       2]
            c_dry_run:           [       0]
    ^C
    
    real    912m42.601s
    user    903m2.777s
    sys     1m52.812s

As you can tell, after more than 15 hours I just gave up. In those
15 hours, `mkfs.erofs` had produced a 13 GiB output file:

    $ ll -h perl-install.erofs 
    -rw-r--r-- 1 mhx users 13G Dec  9 14:42 perl-install.erofs

I don't think this would have been very useful to compare with DwarFS.

Just as for Cromfs, I re-ran with the smaller Perl dataset:

    $ time mkfs.erofs perl-install-small.erofs install-small -zlz4hc,9 -d2
    mkfs.erofs 1.2
            c_version:           [     1.2]
            c_dbg_lvl:           [       2]
            c_dry_run:           [       0]
    
    real    0m27.844s
    user    0m20.570s
    sys     0m1.848s

That was surprisingly quick, which makes me think that, again, there
might be some accidentally quadratic complexity hiding in `mkfs.erofs`.
The output file it produced is an order of magnitude larger than the
DwarFS image:

    $ ls -l perl-install-small.*fs
    -rw-r--r-- 1 mhx users  26928161 Dec  8 15:05 perl-install-small.dwarfs
    -rw-r--r-- 1 mhx users 296488960 Dec  9 14:45 perl-install-small.erofs

Admittedly, this isn't a fair comparison. EROFS has a fixed block size
of 4 KiB, and it uses LZ4 compression. If we tweak DwarFS to the same
parameters, we get:

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

It finishes in less than half the time and produces an output image
that's half the size of the EROFS image.

I'm going to stop the comparison here, as it's pretty obvious that the
domains in which EROFS and DwarFS are being used have extremely little
overlap. DwarFS will likely never be able to run on embedded devices
and EROFS will likely never be able to achieve the compression ratios
of DwarFS.

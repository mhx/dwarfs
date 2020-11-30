[![Build Status](https://travis-ci.com/mhx/dwarfs.svg?branch=main)](https://travis-ci.com/mhx/dwarfs)

# DwarFS

A fast high compression read-only file system

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
  [system creation tool](man/mkdwarfs.md) as well as the
  [FUSE driver](man/dwarfs.md) are able to make good use of the
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

    # apt install \
        g++ \
        clang \
        cmake \
        make \
        bison \
        flex \
        ronn \
        pkg-config \
        binutils-dev \
        libboost-all-dev \
        libevent-dev \
        libdouble-conversion-dev \
        libgoogle-glog-dev \
        libgflags-dev \
        libiberty-dev \
        liblz4-dev \
        liblzma-dev \
        libzstd-dev \
        libsnappy-dev \
        libjemalloc-dev \
        libssl-dev \
        libunwind-dev \
        libfmt-dev \
        libfuse3-dev \
        libsparsehash-dev \
        zlib1g-dev

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

    $ hyperfine -L prog $(echo ./mkdwarfs-* | tr ' ' ,) '{prog} --no-progress --log-level warn -i tree -o /dev/null -C null'
    Benchmark #1: ./mkdwarfs-clang-O2 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      4.358 s ±  0.033 s    [User: 6.364 s, System: 0.622 s]
      Range (min … max):    4.321 s …  4.408 s    10 runs
     
    Benchmark #2: ./mkdwarfs-clang-O3 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      4.282 s ±  0.035 s    [User: 6.249 s, System: 0.623 s]
      Range (min … max):    4.244 s …  4.349 s    10 runs
     
    Benchmark #3: ./mkdwarfs-gcc-O2 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      6.212 s ±  0.031 s    [User: 8.185 s, System: 0.638 s]
      Range (min … max):    6.159 s …  6.250 s    10 runs
     
    Benchmark #4: ./mkdwarfs-gcc-O3 --no-progress --log-level warn -i tree -o /dev/null -C null
      Time (mean ± σ):      5.740 s ±  0.037 s    [User: 7.742 s, System: 0.645 s]
      Range (min … max):    5.685 s …  5.796 s    10 runs
     
    Summary
      './mkdwarfs-clang-O3 --no-progress --log-level warn -i tree -o /dev/null -C null' ran
        1.02 ± 0.01 times faster than './mkdwarfs-clang-O2 --no-progress --log-level warn -i tree -o /dev/null -C null'
        1.34 ± 0.01 times faster than './mkdwarfs-gcc-O3 --no-progress --log-level warn -i tree -o /dev/null -C null'
        1.45 ± 0.01 times faster than './mkdwarfs-gcc-O2 --no-progress --log-level warn -i tree -o /dev/null -C null'

These measurements were made with gcc-9.3.0 and clang-10.0.1.

### Building

Firstly, either clone the repository...

    # git clone --recurse-submodules https://github.com/mhx/dwarfs
    # cd dwarfs

...or unpack the release archive:

    # tar xvf dwarfs-x.y.z.tar.bz2
    # cd dwarfs-x.y.z

Once all dependencies have been installed, you can build DwarFS
using:

    # mkdir build
    # cd build
    # cmake .. -DWITH_TESTS=1
    # make -j$(nproc)

If possible, try building with clang as your compiler, this will
make DwarFS significantly faster. If you have both gcc and clang
installed, use:

    # cmake .. -DWITH_TESTS=1 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

To build with experimental Lua support, you need to install both
`lua` and `luabind`. The latter isn't very well maintained and I
hope to get rid of the dependency in the future. Add `-DWITH_LUA=1`
to the `cmake` command line to enable Lua support.

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
    23:37:00.024298 scanning install
    23:37:12.510322 waiting for background scanners...
    23:38:09.725996 assigning directory and link inodes...
    23:38:10.059963 finding duplicate files...
    23:38:19.932928 saved 28.2 GiB / 47.65 GiB in 1782826/1927501 duplicate files
    23:38:19.933010 ordering 144675 inodes by similarity...
    23:38:20.503470 144675 inodes ordered [570.4ms]
    23:38:20.503531 assigning file inodes...
    23:38:20.505981 building metadata...
    23:38:20.506093 building blocks...
    23:38:20.506160 saving names and links...
    23:38:20.995777 updating name and link indices...
    23:51:26.991376 waiting for block compression to finish...
    23:51:26.991557 saving chunks...
    23:51:27.017126 saving directories...
    23:51:30.557777 waiting for compression to finish...
    23:52:11.527350 compressed 47.65 GiB to 555.7 MiB (ratio=0.0113884)
    23:52:12.026071 filesystem created without errors [912s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    waiting for block compression to finish
    scanned/found: 330733/330733 dirs, 0/0 links, 1927501/1927501 files
    original size: 47.65 GiB, dedupe: 28.2 GiB (1782826 files), segment: 12.42 GiB
    filesystem: 7.027 GiB in 450 blocks (754024 chunks, 144675/144675 inodes)
    compressed filesystem: 450 blocks/555.7 MiB written
    ███████████████████████████████████████████████████████████████████████▏100% -
    
    real    15m12.095s
    user    116m52.351s
    sys     2m36.983s

So in this comparison, `mkdwarfs` is more than 4 times faster than `mksquashfs`.
In total CPU time, it's actually 7 times less CPU resources.

    $ ls -l perl-install.*fs
    -rw-r--r-- 1 mhx users  582654491 Nov 29 03:04 perl-install.dwarfs
    -rw-r--r-- 1 mhx users 4748902400 Nov 25 00:37 perl-install.squashfs

In terms of compression ratio, the **DwarFS file system is more than 8 times
smaller than the SquashFS file system**. With DwarFS, the content has been
**compressed down to 1.1% (!) of its original size**.

When using identical block sizes for both file systems, the difference,
quite expectedly, becomes a lot less dramatic:

    $ time sudo mksquashfs install perl-install-1M.squashfs -comp zstd -Xcompression-level 22 -b 1M
    
    real    41m55.004s
    user    340m30.012s
    sys     1m47.945s

    $ time mkdwarfs -i install -o perl-install-1M.dwarfs -S 20
    
    real    26m26.987s
    user    245m11.438s
    sys     2m29.048s

    $ ll -h perl-install-1M.*
    -rw-r--r-- 1 mhx  users 2.8G Nov 30 10:34 perl-install-1M.dwarfs
    -rw-r--r-- 1 root root  4.0G Nov 30 10:05 perl-install-1M.squashfs

But the point is that this is really where SquashFS tops out, as it doesn't
support larger block sizes. And as you'll see below, the larger blocks don't
necessarily negatively impact performance.

DwarFS also features an option to recompress an existing file system with
a different compression algorithm. This can be useful as it allows relatively
fast experimentation with different algorithms and options without requiring
a full rebuild of the file system. For example, recompressing the above file
system with the best possible compression (`-l 9`):


    $ time mkdwarfs --recompress -i perl-install.dwarfs -o perl-lzma.dwarfs -l 9
    00:08:20.764694 filesystem rewritten [659.4s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    filesystem: 7.027 GiB in 450 blocks (0 chunks, 0 inodes)
    compressed filesystem: 450/450 blocks/457.5 MiB written
    █████████████████████████████████████████████████████████████████████▏100% /
    
    real    10m59.538s
    user    120m51.326s
    sys     1m43.097s

    $ ls -l perl-*.dwarfs
    -rw-r--r-- 1 mhx users 582654491 Nov 29 03:04 perl-install.dwarfs
    -rw-r--r-- 1 mhx users 479756881 Nov 29 03:18 perl-lzma.dwarfs

This reduces the file system size by another 18%, pushing the total
compression ratio below 1%.

In terms of how fast the file system is when using it, a quick test
I've done is to freshly mount the filesystem created above and run
each of the 1139 `perl` executables to print their version.

    $ hyperfine -c "umount mnt" -p "umount mnt; ./dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
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

    $ hyperfine -c "umount mnt" -p "umount mnt; ./dwarfs perl-lzma.dwarfs mnt -o cachesize=1g -o workers=4; sleep 1" -P procs 5 20 -D 5 "ls -1 mnt/*/*/bin/perl5* | xargs -d $'\n' -n1 -P{procs} sh -c '\$0 -v >/dev/null'"
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

    hyperfine -c 'sudo umount /tmp/perl/install' -p 'umount /tmp/perl/install; ./dwarfs perl-install.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4; sleep 1' -n dwarfs-zstd "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'" -p 'sudo umount /tmp/perl/install; sudo mount -t squashfs perl-install.squashfs /tmp/perl/install; sleep 1' -n squashfs-zstd "ls -1 /tmp/perl/install/*/*/bin/perl5* | xargs -d $'\n' -n1 -P20 sh -c '\$0 -v >/dev/null'"
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

    $ time ./dwarfs perl-install.dwarfs /tmp/perl/install -o cachesize=1g -o workers=4 -f
    
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
a recent Raspberry Pi OS release.

    $ time mkdwarfs -i raspbian -o raspbian.dwarfs
    23:25:14.256884 scanning raspbian
    23:25:14.598902 waiting for background scanners...
    23:25:16.285708 assigning directory and link inodes...
    23:25:16.300842 finding duplicate files...
    23:25:16.323520 saved 31.05 MiB / 1007 MiB in 1617/34582 duplicate files
    23:25:16.323602 ordering 32965 inodes by similarity...
    23:25:16.341961 32965 inodes ordered [18.29ms]
    23:25:16.342042 assigning file inodes...
    23:25:16.342326 building metadata...
    23:25:16.342426 building blocks...
    23:25:16.342470 saving names and links...
    23:25:16.374943 updating name and link indices...
    23:26:34.547856 waiting for block compression to finish...
    23:26:34.548018 saving chunks...
    23:26:34.552481 saving directories...
    23:26:34.677199 waiting for compression to finish...
    23:26:51.034506 compressed 1007 MiB to 297.3 MiB (ratio=0.295318)
    23:26:51.063847 filesystem created without errors [96.81s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    waiting for block compression to finish
    scanned/found: 4435/4435 dirs, 5908/5908 links, 34582/34582 files
    original size: 1007 MiB, dedupe: 31.05 MiB (1617 files), segment: 52.66 MiB
    filesystem: 923 MiB in 58 blocks (46074 chunks, 32965/32965 inodes)
    compressed filesystem: 58 blocks/297.3 MiB written
    ███████████████████████████████████████████████████████████████████████▏100% -
    
    real    1m36.865s
    user    14m52.770s
    sys     0m16.615s

Again, SquashFS uses the same compression options:

    $ time mksquashfs raspbian raspbian.squashfs -comp zstd -Xcompression-level 22
    Parallel mksquashfs: Using 12 processors
    Creating 4.0 filesystem on raspbian.squashfs, block size 131072.
    [===============================================================/] 38644/38644 100%
    
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
    
    real    1m54.673s
    user    18m32.152s
    sys     0m2.501s

The difference in speed is almost negligible. SquashFS is just a bit
slower here. In terms of compression, the difference also isn't huge:

    $ ll raspbian.* *.xz -h
    -rw-r--r-- 1 mhx users 298M Nov 29 23:26 raspbian.dwarfs
    -rw-r--r-- 1 mhx users 364M Nov 29 23:31 raspbian.squashfs
    -rw-r--r-- 1 mhx users 297M Aug 20 12:47 2020-08-20-raspios-buster-armhf-lite.img.xz

Interestingly, `xz` actually can't compress the whole original image
much better.

We can again try to increase the DwarFS compression level:

    $ time mkdwarfs -i raspbian.dwarfs -o raspbian-9.dwarfs -l 9 --recompress
    23:54:59.981488 filesystem rewritten [86.04s]
    ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
    filesystem: 923 MiB in 58 blocks (0 chunks, 0 inodes)
    compressed filesystem: 58/58 blocks/266.5 MiB written
    ██████████████████████████████████████████████████████████████████▏100% |
    
    real    1m26.084s
    user    15m46.619s
    sys     0m14.543s

Now that actually gets the DwarFS image size well below that of the
`xz` archive:

    $ ll -h raspbian-9.dwarfs *.xz
    -rw-r--r-- 1 root root 267M Nov 29 23:54 raspbian-9.dwarfs
    -rw-r--r-- 1 mhx users 297M Aug 20 12:47 2020-08-20-raspios-buster-armhf-lite.img.xz

However, if you actually build a tarball and compress that (instead of
compressing the EXT4 file system), `xz` is, unsurprisingly, able to take
the lead again:

    $ time sudo tar cf - raspbian | xz -9e -vT 0 >raspbian.tar.xz
      100 %     245.9 MiB / 1,012.3 MiB = 0.243   5.4 MiB/s       3:07
    
    real    3m8.088s
    user    14m16.519s
    sys     0m5.843s

    $ ll -h raspbian.tar.xz
    -rw-r--r-- 1 mhx users 246M Nov 30 00:16 raspbian.tar.xz

In summary, DwarFS can get pretty close to an `xz` compressed tarball
in terms of size. It's also about twice as fast to build the file
system than to build the tarball. At the same time, SquashFS really
isn't that much worse. It's really the cases where you *know* upfront
that your data is highly redundant where DwarFS can play out its full
strength.

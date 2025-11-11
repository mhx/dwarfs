[![Latest Release](https://img.shields.io/github/release/mhx/dwarfs?label=Latest%20Release)](https://github.com/mhx/dwarfs/releases/latest)
[![Total Downloads](https://img.shields.io/github/downloads/mhx/dwarfs/total.svg?&color=E95420&label=Total%20Downloads)](https://github.com/mhx/dwarfs/releases)
[![Homebrew Downloads](https://img.shields.io/homebrew/installs/dm/dwarfs?label=Homebrew)](https://formulae.brew.sh/formula/dwarfs)
[![DwarFS CI Build](https://github.com/mhx/dwarfs/actions/workflows/build.yml/badge.svg)](https://github.com/mhx/dwarfs/actions/workflows/build.yml)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/53489f77755248c999e380500267e889)](https://app.codacy.com/gh/mhx/dwarfs/dashboard)
[![codecov](https://codecov.io/github/mhx/dwarfs/graph/badge.svg?token=BKR4A3XKA9)](https://codecov.io/github/mhx/dwarfs)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8663/badge)](https://www.bestpractices.dev/projects/8663)

# DwarFS

The **D**eduplicating **W**arp-speed **A**dvanced **R**ead-only **F**ile **S**ystem.

A fast high-compression read-only file system for Linux, FreeBSD, macOS and Windows.

## Table of contents

<a href="https://repology.org/project/dwarfs/versions">
    <img src="https://repology.org/badge/vertical-allrepos/dwarfs.svg" alt="Packaging status" align="right">
</a>

- [What is DwarFS (in plain words)?](#what-is-dwarfs-in-plain-words)
- [Why not just use .zip or .tar.gz?](#why-not-just-use-zip-or-targz)
- [Performance comparison overview](#performance-comparison-overview)
- [Quick Start](#quick-start)
- [Overview](#overview)
- [History](#history)
- [Building and Installing](#building-and-installing)
  - [Note to Package Maintainers](#note-to-package-maintainers)
  - [Prebuilt Binaries](#prebuilt-binaries)
  - [Universal Binaries](#universal-binaries)
  - [Dependencies](#dependencies)
  - [Building](#building)
  - [Installing](#installing)
  - [Static Builds](#static-builds)
- [Usage](#usage)
- [Using the Libraries](#using-the-libraries)
- [Windows Support](#windows-support)
  - [Building on Windows](#building-on-windows)
- [macOS Support](#macos-support)
- [Use Cases](#use-cases)
  - [Astrophotography](#astrophotography)
- [Dealing with Bit Rot](#dealing-with-bit-rot)
- [Extended Attributes](#extended-attributes)
- [Comparison](#comparison)
  - [With SquashFS](#with-squashfs)
  - [With SquashFS &amp; xz](#with-squashfs--xz)
  - [With lrzip](#with-lrzip)
  - [With zpaq](#with-zpaq)
  - [With zpaqfranz](#with-zpaqfranz)
  - [With wimlib](#with-wimlib)
  - [With Cromfs](#with-cromfs)
  - [With EROFS](#with-erofs)
  - [With fuse-archive](#with-fuse-archive)
- [Performance Monitoring](#performance-monitoring)
- [Other Obscure Features](#other-obscure-features)
- [Related projects](#related-projects)
- [Notable users](#notable-users)
- [Stargazers over Time](#stargazers-over-time)

## What is DwarFS (in plain words)?

DwarFS is a **mountable archive**: you pack a directory into one file
(an "image"), then **open it instantly like a normal folder** — no full
extraction, no temporary files. It’s read-only when mounted, so you can
browse, open, and run files safely in place. This works particularly well
for big collections with lots of **similar or repeated content** — think
many versions of a project or dataset, folders of documents and text
files, backups/snapshots that mostly overlap, or libraries with many
near-duplicates. The image can be mounted in fractions of a second, you
can use the contents immediately with no delay, and you extract only if
you actually need to.

## Why not just use .zip or .tar.gz?

Traditional archives are fine for storage, but they’re usually slow to
open and awkward for random access (jumping around inside large files or
across many files). DwarFS is built for **fast random reads** *and*
**space savings**. It groups similar files and removes duplication, so
images are **often smaller** than a simple tar/zip, while reads stay
snappy — even when accessing lots of files simultaneously. In practice,
you keep huge directories compressed, mount them in milliseconds, and
work as if they were already unpacked.

## Performance comparison overview

This comparison uses DwarFS 0.14.1, 7-Zip 25.00 (x64), and `tar` +
[pigz](https://github.com/madler/pigz) for all `.tar.gz` tests since plain
`gzip` would have been incredibly slow. Both `.tar.gz` and `7zip` archives
were mounted using [fuse-archive](https://github.com/google/fuse-archive)
v1.16 with the `-olazycache` option since the default of caching the entire
archive in memory is infeasible for large archives.

### 1139 complete Perl installations

![Perl • Compression ratio](doc/perf/perl_compression_ratio.svg)
![Perl • Compression speed](doc/perf/perl_compression_speed.svg)
![Perl • Compression CPU time](doc/perf/perl_compression_cpu_time.svg)
![Perl • Decompression speed](doc/perf/perl_decompression_speed.svg)
![Perl • Decompression CPU time](doc/perf/perl_decompression_cpu_time.svg)
![Perl • Mount time](doc/perf/perl_mount_time.svg)
![Perl • Lookup speed](doc/perf/perl_lookup_speed.svg)
![Perl • Random access speed](doc/perf/perl_random_access_speed.svg)

| **Perl** (47.49 GiB, 1.9M files)      | .tar.gz [^pl1] | .tar.zst [^pl2] | 7zip (`-mx=7`) | SquashFS [^pl7] | DwarFS (lzma) | DwarFS (zstd) |
|---------------------------------------|---------------:|----------------:|---------------:|----------------:|--------------:|--------------:|
| Compression time                      |         4m 59s |          8m 06s |        23m 27s |          5m 37s |    **2m 13s** |         5m 3s |
| Compression CPU time                  |         1h 47m |          2h 18m |          5h 5m |          2h 29m |   **31m 17s** |       49m 51s |
| Compressed size                       |      12.17 GiB |       0.387 GiB |      1.219 GiB |       3.245 GiB | **0.310 GiB** |     0.352 GiB |
| Compression ratio                     |          3.902 |           122.7 |          38.96 |           14.63 |     **153.2** |         134.9 |
| Decompression time                    |         2m 19s |           57.2s |         1m 14s |       **39.3s** |         48.7s |         46.5s |
| Decompression CPU time                |         3m 44s |      **1m 21s** |         2m 28s |          1m 25s |        2m 23s |        1m 59s |
| Mount time                            |         2m 07s |      ❌  [^pl3] |         3.638s |          0.011s |        0.420s |    **0.009s** |
| Find all 1.9M files [^pl8]            |         5.670s |      ❌  [^pl3] |         5.695s |          5.311s |    **2.800s** |        2.821s |
| Checksum 1139 files (2.58 GiB) [^pl4] |     ❌  [^pl5] |      ❌  [^pl3] |     ~5h [^pl6] |          1.541s |        4.330s |    **1.134s** |

[^pl1]: using `pigz -9`
[^pl2]: using `zstd --long=31 --ultra -22 -T0`
[^pl3]: not supported by fuse-archive
[^pl4]: `$ ls -1 mnt/*/perl*/bin/perl5* | xargs -d $'\n' -n1 -P16 sha256sum`
[^pl5]: killed after making no progress for 15 minutes
[^pl6]: killed when only 78 files were finished after about 20 minutes
[^pl7]: using `-comp zstd -Xcompression-level 22 -b 1M -tailends`; using `squashfuse_ll` 0.6.0
[^pl8]: `$ fd -t f . mnt | wc -l`

### All artifacts from 205 DwarFS CI builds

![DwarFS CI • Compression ratio](doc/perf/dwarfsci_compression_ratio.svg)
![DwarFS CI • Compression speed](doc/perf/dwarfsci_compression_speed.svg)
![DwarFS CI • Compression CPU time](doc/perf/dwarfsci_compression_cpu_time.svg)
![DwarFS CI • Decompression speed](doc/perf/dwarfsci_decompression_speed.svg)
![DwarFS CI • Decompression CPU time](doc/perf/dwarfsci_decompression_cpu_time.svg)
![DwarFS CI • Mount time](doc/perf/dwarfsci_mount_time.svg)
![DwarFS CI • Random access speed](doc/perf/dwarfsci_random_access_speed.svg)

| **DwarFS CI** (465.2 GiB, 3.6M files) | .tar.gz (`pigz -9`) | 7zip (`-mx=7`) | DwarFS (zstd) |
|---------------------------------------|--------------------:|---------------:|--------------:|
| Compression time                      |         **40m 28s** |       141m 46s |        68m 6s |
| Compression CPU time                  |         **13h 55m** |        42h 47m |       31h 12m |
| Compressed size                       |           142.9 GiB |      60.04 GiB | **28.63 GiB** |
| Compression ratio                     |               3.255 |          7.748 |     **16.25** |
| Decompression time                    |             26m 17s |         8m 15s |    **7m 21s** |
| Decompression CPU time                |             35m 16s |        41m 29s |    **8m 59s** |
| Mount time                            |             22m 18s |          10.5s |    **0.024s** |
| Run 441 binaries (2.72 GiB) [^ci1]    |          ❌  [^ci2] |     ❌  [^ci3] |    **0.774s** |

[^ci1]: `$ ls -1 mnt/*/dwarfs-*-Linux-x86_64-*-minsize*/bin/mkdwarfs | xargs -d $'\n' -n1 -P16 sh -c '$0 -h'`
[^ci2]: killed after making no progress for 15 minutes
[^ci3]: killed after making no progress and consuming more than 32 GiB of RAM

### Game audio assets from sonniss.com

![Sonniss • Compression ratio](doc/perf/sonniss_compression_ratio.svg)
![Sonniss • Compression speed](doc/perf/sonniss_compression_speed.svg)
![Sonniss • Compression CPU time](doc/perf/sonniss_compression_cpu_time.svg)
![Sonniss • Decompression speed](doc/perf/sonniss_decompression_speed.svg)
![Sonniss • Decompression CPU time](doc/perf/sonniss_decompression_cpu_time.svg)
![Sonniss • Mount time](doc/perf/sonniss_mount_time.svg)
![Sonniss • Random access speed](doc/perf/sonniss_random_access_speed.svg)

| **Sonniss** (3.072 GiB, 171 files) [^wav1] | .tar.gz (`pigz -9`) | 7zip (`-mx=7`) | SquashFS [^wav3] | DwarFS (categorize) |
|--------------------------------------------|--------------------:|---------------:|-----------------:|--------------------:|
| Compression time                           |               5.34s |         6m 21s |           19.52s |           **3.98s** |
| Compression CPU time                       |               2m 1s |        19m 14s |           9m 47s |           **29.0s** |
| Compressed size                            |           2.725 GiB |      2.255 GiB |        2.711 GiB |       **1.664 GiB** |
| Compression ratio                          |               1.127 |          1.362 |            1.133 |           **1.846** |
| Decompression time                         |               13.7s |         1m 50s |       **0.774s** |               1.32s |
| Decompression CPU time                     |               17.8s |         1m 52s |        **4.60s** |               9.15s |
| Mount time                                 |               13.6s |         0.014s |           0.018s |          **0.008s** |
| Checksum all files [^wav2]                 |               3m 2s |        30m 42s |            2.81s |           **2.64s** |

[^wav1]: https://hippolytus.feralhosting.com/sonniss/Sonniss.com-GDC2024-GameAudioBundle1of9.zip
[^wav2]: `$ find mnt -type f | xargs -d $'\n' -n1 -P16 sha256sum`
[^wav3]: using `-comp zstd -Xcompression-level 22 -b 1M -tailends`

## Quick Start

To create a DwarFS image from a directory, use [`mkdwarfs`](doc/mkdwarfs.md):

```
$ mkdwarfs -i /path/to/input/dir -o /path/to/image.dwarfs
```

To mount the image, use the [`dwarfs`](doc/dwarfs.md) FUSE driver:

```
$ mkdir /path/to/mountpoint
$ dwarfs /path/to/image.dwarfs /path/to/mountpoint
$ ls /path/to/mountpoint
```

To extract the image, use [`dwarfsextract`](doc/dwarfsextract.md):

```
$ dwarfsextract -i /path/to/image.dwarfs -o /path/to/output/dir
```

There's also the [`dwarfsck`](doc/dwarfsck.md) tool that can be used
for a variety of tasks, for example listing the files in a DwarFS image:

```
$ dwarfsck /path/to/image.dwarfs -l
```

It can also be used for generating checksums for each file in the image
in a format that is recognized by the `*sum` commands:

```
$ dwarfsck /path/to/image.dwarfs --checksum=sha512 | sha512sum --check
```

This is useful if you want to verify the integrity of the actual files
stored in the DwarFS image.

## Overview

![Windows Screen Capture](doc/windows.gif?raw=true "DwarFS Windows")

![Linux Screen Capture](doc/screenshot.gif?raw=true "DwarFS Linux")

DwarFS is a read-only file system with a focus on achieving **very
high compression ratios**, particularly for highly redundant data.

This probably doesn't sound very exciting, because if it's redundant,
it *should* compress well. However, I found that other read-only,
compressed file systems don't do a very good job at making use of
this redundancy. See [here](#comparison) for a comparison with other
compressed file systems.

DwarFS also **doesn't compromise on speed**; in my use cases, it
performs on par with, or better than, SquashFS. For my primary use
case, **DwarFS compression is an order of magnitude better than
SquashFS compression**, it's **6 times faster to build the file
system**, it's typically faster to access files on DwarFS and it uses
less CPU resources.

To give you an idea of what DwarFS is capable of, here's a quick comparison
of DwarFS and SquashFS on a set of video files with a total size of 39 GiB.
The twist is that each unique video file has two sibling files with a
different set of audio streams (this is
[an actual use case](https://github.com/mhx/dwarfs/discussions/63)).
So there's redundancy in both the video and audio data, but as the streams
are interleaved and identical blocks are typically very far apart, it's
challenging to make use of that redundancy for compression. SquashFS
essentially fails to compress the source data at all, whereas DwarFS is
able to reduce the size to nearly one-third, which is close to the
theoretical maximum:

```
$ du -hs dwarfs-video-test
39G     dwarfs-video-test
$ ls -lh dwarfs-video-test.*fs
-rw-r--r-- 1 mhx users 14G Jul  2 13:01 dwarfs-video-test.dwarfs
-rw-r--r-- 1 mhx users 39G Jul 12 09:41 dwarfs-video-test.squashfs
```

Furthermore, when mounting the SquashFS image and performing a random-read
throughput test using [fio](https://github.com/axboe/fio/)-3.34, both
`squashfuse` and `squashfuse_ll` top out at around 230 MiB/s:

```
$ fio --readonly --rw=randread --name=randread --bs=64k --direct=1 \
      --opendir=mnt --numjobs=4 --ioengine=libaio --iodepth=32 \
      --group_reporting --runtime=60 --time_based
[...]
   READ: bw=230MiB/s (241MB/s), 230MiB/s-230MiB/s (241MB/s-241MB/s), io=13.5GiB (14.5GB), run=60004-60004msec
```

In comparison, DwarFS manages to sustain **random read rates of 20 GiB/s**:

```
  READ: bw=20.2GiB/s (21.7GB/s), 20.2GiB/s-20.2GiB/s (21.7GB/s-21.7GB/s), io=1212GiB (1301GB), run=60001-60001msec
```

Distinct features of DwarFS are:

- Clustering of files by similarity using a similarity hash function.
  This makes it easier to exploit the redundancy across file boundaries.

- Segmentation analysis across file system blocks in order to reduce
  the size of the uncompressed file system. This saves memory when
  using the compressed file system and thus potentially allows for
  higher cache hit rates as more data can be kept in the cache.

- [Categorization framework](doc/mkdwarfs.md#categorizers) to categorize
  files or even fragments of files and then process individual categories
  differently. For example, this allows you to not waste time trying to
  compress incompressible files or to compress PCM audio data using FLAC
  compression.

- Highly multi-threaded implementation. Both the
  [file system creation tool](doc/mkdwarfs.md) as well as the
  [FUSE driver](doc/dwarfs.md) are able to make good use of the
  many cores of your system.

## History

I started working on DwarFS in 2013 and my main use case and major
motivation was that I had several hundred different versions of Perl
that were taking up something around 30 gigabytes of disk space, and
I was unwilling to spend more than 10% of my hard drive keeping them
around for when I happened to need them.

Up until then, I had been using [Cromfs](https://bisqwit.iki.fi/source/cromfs.html)
for squeezing them into a manageable size. However, I was getting more
and more annoyed by the time it took to build the file system image
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
[folly](https://github.com/facebook/folly) library and it didn't
have any documentation.

Digging out the project again in 2020, things didn't look as grim
as they used to. Folly now built with CMake and so I just pulled
it in as a submodule. Most other dependencies could be satisfied
from packages that should be widely available.

## Building and Installing

### Note to Package Maintainers

DwarFS should usually build fine with minimal changes out of the box.
If it doesn't, please file an issue. I've set up
[CI jobs](.github/workflows/build.yml)
using Docker images for Ubuntu ([22.04](.docker/Dockerfile.ubuntu-2204)
and [24.04](.docker/Dockerfile.ubuntu)),
[Fedora Rawhide](.docker/Dockerfile.fedora),
[openSUSE Tumbleweed](.docker/Dockerfile.suse),
[Arch Linux](.docker/Dockerfile.arch), and
[Debian](.docker/Dockerfile.debian),
as well as a setup script for [FreeBSD](.github/scripts/freebsd_setup_base.sh),
that can help with determining an up-to-date set of dependencies.
Note that building from the release tarball requires less dependencies
than building from the git repository, notably the `ronn` tool as well
as Python and the `mistletoe` Python module are not required when
building from the release tarball. Also, the release tarball build
doesn't require to build the thrift compiler, which makes the build
a lot faster.

There are some things to be aware of:

- There's a tendency to try to unbundle the [folly](https://github.com/facebook/folly/)
  and [fbthrift](https://github.com/facebook/fbthrift) libraries that
  are included as submodules and are built along with DwarFS.
  While I agree with the sentiment, it's unfortunately a bad idea.
  Besides the fact that folly does not make any claims about ABI
  stability (i.e. you can't just dynamically link a binary built
  against one version of folly against another version), it's not
  even possible to safely link against a folly library built with
  different compile options. Even subtle differences, such as the
  C++ standard version, can cause run-time errors.
  See [this issue](https://github.com/facebook/folly/pull/1949)
  for details. Currently, it is not even possible to use external
  versions of folly/fbthrift as DwarFS is building minimal subsets of
  both libraries; these are bundled in the `dwarfs_common` library
  and they are strictly used internally, i.e. none of the folly or
  fbthrift headers are required to build against DwarFS' libraries.

- Similar issues can arise when using a system-installed version
  of GoogleTest. GoogleTest recommends downloading it as part of
  the build. However, you can use the system-installed version by
  passing `-DPREFER_SYSTEM_GTEST=ON` to the `cmake` call. Use at
  your own risk.

- For other bundled libraries (namely `fmt`, `parallel-hashmap`,
  `range-v3`), the system-installed version is used as long as it
  meets the minimum required version. Otherwise, the preferred
  version is fetched during the build.

### Prebuilt Binaries

[Each release](https://github.com/mhx/dwarfs/releases) has pre-built,
statically linked binaries for a number of architectures available
for download. These *should* run without any dependencies and can be
useful especially on older distributions where you can't easily build
the tools from source. For example, these binaries will work perfectly
fine on Linux distributions from 2007.

### Universal Binaries

In addition to the binary tarballs, there's a **universal binary**
available for each architecture. These universal binaries contain *all*
tools (`mkdwarfs`, `dwarfsck`, `dwarfsextract` and the `dwarfs` fuse3
driver) in a single executable. These executables are self-extracting
and use either a [custom self-extractor](sfx/stub.c) or
[upx](https://github.com/upx/upx). They are generally optimized to
provide the best performance in the smallest possible binary and are much
smaller than the individual tools combined. However, this also means the
binaries need to be decompressed each time they are run, which can add
significant overhead. If that is an issue, you can either stick to the
"classic" individual binaries or you can decompress the universal binary.
For upx-compressed binaries, you can use:

```
$ upx -d dwarfs-universal-0.7.0-Linux-aarch64
```

For the binaries that use the custom self-extractor, you can use:

```
$ ./dwarfs-universal-riscv64 --extract-wrapped-binary dwarfs-universal
```

Note that both self-extractors need at least Linux kernel 3.17 to work
properly. If you want to use the FUSE driver, you'll need to install
the fuse3 tools for your distribution. If you want to run the binaries
on an older kernel, you can unpack the universal binary (unpacking does
*not* require kernel 3.17). If you're stuck with fuse2, you must use the
individual `dwarfs2` driver instead of the universal binary.

You can run the universal binaries via symbolic links named after
the tool. For example:

```
$ ln -s dwarfs-universal-0.7.0-Linux-aarch64 mkdwarfs
$ ./mkdwarfs --help
```

This also works on Windows if the file system supports symbolic links:

```
> mklink mkdwarfs.exe dwarfs-universal-0.7.0-Windows-AMD64.exe
> .\mkdwarfs.exe --help
```

Alternatively, you can select the tool by passing `--tool=<name>` as
the first argument on the command line:

```
> .\dwarfs-universal-0.7.0-Windows-AMD64.exe --tool=mkdwarfs --help
```

Note that just like the `dwarfs.exe` Windows binary, the universal
Windows binary depends on the `winfsp-x64.dll` from the
[WinFsp](https://github.com/winfsp/winfsp) project. However, for the
universal binary, the DLL is loaded lazily, so you can still use all
other tools without the DLL.
See the [Windows Support](#windows-support) section for more details.

### Dependencies

DwarFS uses [CMake](https://cmake.org/) as a build tool.

If you build from a release tarball, the build will be faster and
there will be fewer dependencies, because the release tarball ships
with the auto-generated files that would otherwise have to be generated
during the in-repository build.

You can grab an up-to-date list of dependencies from the docker files
used for the CI builds:

- [Arch Linux](.docker/Dockerfile.arch)
- [Debian Testing](.docker/Dockerfile.debian)
- [Fedora Rawhide](.docker/Dockerfile.fedora)
- [openSUSE Tumbleweed](.docker/Dockerfile.suse)
- [Ubuntu 22.04](.docker/Dockerfile.ubuntu-2204)
- [Ubuntu 24.04](.docker/Dockerfile.ubuntu)

For FreeBSD, you can use `PKGS` from the
[script that sets up the jail for the CI builds](.github/scripts/freebsd_setup_base.sh).

### Building

First, unpack the release tarball:

```
$ tar xvf dwarfs-x.y.z.tar.xz
$ cd dwarfs-x.y.z
```

Alternatively, you can also clone the git repository, but be aware
that this has more dependencies and the build will likely take longer
because the release archive ships with most of the auto-generated
files that will have to be generated when building from the repository:

```
$ git clone --recurse-submodules https://github.com/mhx/dwarfs
$ cd dwarfs
```

Once all [dependencies](#dependencies) have been installed, you can
build DwarFS using:

```
$ mkdir build
$ cd build
$ cmake .. -GNinja -DWITH_TESTS=ON
$ ninja
```

You can then run tests with:

```
$ ctest -j
```

All binaries use [jemalloc](https://github.com/jemalloc/jemalloc)
as a memory allocator by default, as it typically uses much less
system memory compared to the `glibc` or `tcmalloc` allocators.
To disable the use of `jemalloc`, pass `-DUSE_JEMALLOC=0` on the
`cmake` command line.

It is also possible to build/install the DwarFS libraries, tools,
and FUSE driver independently. This is mostly interesting when
packaging DwarFS. Note that the tools and FUSE driver require the
libraries to be either built or already installed. To build just
the libraries, use:

```
$ cmake .. -GNinja -DWITH_TESTS=ON -DWITH_LIBDWARFS=ON -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=OFF
```

Once the libraries are tested and installed, you can build the
tools (i.e. `mkdwarfs`, `dwarfsck`, `dwarfsextract`) using:

```
$ cmake .. -GNinja -DWITH_TESTS=ON -DWITH_LIBDWARFS=OFF -DWITH_TOOLS=ON -DWITH_FUSE_DRIVER=OFF
```

To build the FUSE driver, use:

```
$ cmake .. -GNinja -DWITH_TESTS=ON -DWITH_LIBDWARFS=OFF -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=ON
```

### Installing

Installing is as easy as:

```
$ sudo ninja install
```

Though you don't have to install the tools to play with them.

### Static Builds

Attempting to build statically linked binaries is highly discouraged
and not officially supported. That being said, here's how to set up
an environment where you *might* be able to build static binaries.

This has been tested with `ubuntu-22.04-live-server-amd64.iso`. First,
install all the packages listed as dependencies above. Also install:

```
$ apt install ccache ninja libacl1-dev
```

`ccache` and `ninja` are optional, but help with a speedy compile.

Depending on your distribution, you'll need to build and install static
versions of some libraries, e.g. `libarchive` and `libmagic` for Ubuntu:

```
$ wget https://github.com/libarchive/libarchive/releases/download/v3.6.2/libarchive-3.6.2.tar.xz
$ tar xf libarchive-3.6.2.tar.xz && cd libarchive-3.6.2
$ ./configure --prefix=/opt/static-libs --without-iconv --without-xml2 --without-expat
$ make && sudo make install
```

```
$ wget ftp://ftp.astron.com/pub/file/file-5.44.tar.gz
$ tar xf file-5.44.tar.gz && cd file-5.44
$ ./configure --prefix=/opt/static-libs --enable-static=yes --enable-shared=no
$ make && make install
```

That's it! Now you can try building static binaries for DwarFS:

```
$ git clone --recurse-submodules https://github.com/mhx/dwarfs
$ cd dwarfs && mkdir build && cd build
$ cmake .. -GNinja -DWITH_TESTS=ON -DSTATIC_BUILD_DO_NOT_USE=ON \
           -DSTATIC_BUILD_EXTRA_PREFIX=/opt/static-libs
$ ninja
$ ninja test
```

## Usage

Please check out the manual pages for [mkdwarfs](doc/mkdwarfs.md),
[dwarfs](doc/dwarfs.md), [dwarfsck](doc/dwarfsck.md) and
[dwarfsextract](doc/dwarfsextract.md). You can also access the manual
pages using the `--man` option to each binary, e.g.:

```
$ mkdwarfs --man
```

The [dwarfs](doc/dwarfs.md) manual page also shows an example for setting up DwarFS
with [overlayfs](https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html)
in order to create a writable file system mount on top of a read-only DwarFS image.

A description of the DwarFS file system format can be found in
[dwarfs-format(5)](doc/dwarfs-format.md).

A high-level overview of the internal operation of `mkdwarfs` is shown
in [this sequence diagram](doc/mkdwarfs-sequence.svg).

## Using the Libraries

Using the DwarFS libraries should be pretty straightforward if you're
using [CMake](https://cmake.org/) to build your project. For a quick
start, have a look at the [example code](example/example.cpp) that uses
the libraries to print information about a DwarFS image (like `dwarfsck`)
or extract it (like `dwarfsextract`).

There are five individual libraries:

- `dwarfs_common` contains the common code required by all the other
  libraries. The interfaces are defined in [`dwarfs/`](include/dwarfs).

- `dwarfs_reader` contains all code required to read data from a
  DwarFS image. The interfaces are defined in [`dwarfs/reader/`](include/dwarfs/reader).

- `dwarfs_extractor` contains the code required to extract a DwarFS
  image using [`libarchive`](https://libarchive.org/). The interfaces
  are defined in [`dwarfs/utility/filesystem_extractor.h`](include/dwarfs/utility/filesystem_extractor.h).

- `dwarfs_writer` contains the code required to create DwarFS images.
  The interfaces are defined in [`dwarfs/writer/`](include/dwarfs/writer).

- `dwarfs_rewrite` contains the code to re-write DwarFS images. The
  interfaces are defined in [`dwarfs/utility/rewrite_filesystem.h`](include/dwarfs/utility/rewrite_filesystem.h).

The headers in `internal` subfolders are only accessible at build
time and won't be installed. The same goes for the `tool` subfolder.

The reader and extractor APIs should be fairly stable. The writer
APIs are likely going to change. Note, however, that there are no
guarantees on API stability before this project reaches version 1.0.0.

## Windows Support

Support for the Windows operating system is currently experimental.
Having worked pretty much exclusively in a Unix world for the past two
decades, my experience with Windows development is rather limited and
I'd expect there to definitely be bugs and rough edges in the Windows
code.

The Windows version of the DwarFS file system driver relies on the awesome
[WinFsp](https://github.com/winfsp/winfsp) project and its `winfsp-x64.dll`
must be discoverable by the `dwarfs.exe` driver.

The different tools should behave pretty much the same whether you're
using them on Linux or Windows. The file system images can be copied
between Linux and Windows and images created on one OS should work fine
on the other.

There are a few things worth pointing out, though:

- Windows traditionally treats file names as case-insensitive. However,
  the DwarFS driver is case-sensitive by default, even on Windows.
  There's an option `-o case_insensitive` that can be passed to the
  driver to make it behave like a traditional Windows file system.
  Note that this is *only* supported if the DwarFS image contains no
  directories or files with the same name differing only in case within
  the same directory. Otherwise, the driver will emit a warning when
  mounting the image.

- DwarFS supports both hardlinks and symlinks on Windows, just as it
  does on Linux. However, creating hardlinks and symlinks seems to
  require admin privileges on Windows, so if, for example, you want to
  extract a DwarFS image that contains links of some sort, you might
  run into errors if you don't have the right privileges.

- Due to a [problem](https://github.com/winfsp/winfsp/issues/454) in
  WinFsp, symlinks cannot currently point outside of the mounted file
  system.  Furthermore, due to another
  [problem](https://github.com/winfsp/winfsp/issues/530) in WinFsp,
  symlinks with a drive letter will appear with a mangled target path.

- The DwarFS driver on Windows correctly reports hardlink counts via
  its API, but currently these counts are not correctly propagated
  to the Windows file system layer. This is presumably due to a
  [problem](https://github.com/winfsp/winfsp/issues/511) in WinFsp.

- When mounting a DwarFS image on Windows, the mount point must not
  exist. This is different from Linux, where the mount point must
  actually exist. Also, it's possible to mount a DwarFS image as a
  drive letter, e.g.

    dwarfs.exe image.dwarfs Z:

- Filter rules for `mkdwarfs` always require Unix path separators,
  regardless of whether it's running on Windows or Linux.

### Building on Windows

Building on Windows is not too complicated thanks to [vcpkg](https://vcpkg.io/).
You'll need to install:

- [Visual Studio and the MSVC C/C++ compiler](https://visualstudio.microsoft.com/vs/features/cplusplus/)

- [Git](https://git-scm.com/download/win)

- [CMake](https://cmake.org/download/)

- [Ninja](https://github.com/ninja-build/ninja/releases)

- [WinFsp](https://github.com/winfsp/winfsp/releases)

`WinFsp` is expected to be installed in `C:\Program Files (x86)\WinFsp`;
if it's not, you'll need to set `WINFSP_PATH` when running CMake via
`cmake/win.bat`.

Clone `vcpkg` and `dwarfs`:

```
> cd %HOMEPATH%
> mkdir git
> cd git
> git clone https://github.com/Microsoft/vcpkg.git
> git clone https://github.com/mhx/dwarfs
```

Then, bootstrap `vcpkg`:

```
> .\vcpkg\bootstrap-vcpkg.bat
```

And build DwarFS:

```
> cd dwarfs
> mkdir build
> cd build
> ..\cmake\win.bat
> ninja
```

Once that's done, you should be able to run the tests.
Set `CTEST_PARALLEL_LEVEL` according to the number of CPU cores in
your machine.

```
> set CTEST_PARALLEL_LEVEL=10
> ninja test
```

## macOS Support

The DwarFS libraries and tools (`mkdwarfs`, `dwarfsck`, `dwarfsextract`)
are now available from [Homebrew](https://brew.sh/):

```
$ brew install dwarfs
$ brew test dwarfs
```

The macOS version of the DwarFS file system driver relies on the awesome
[macFUSE](https://macfuse.io) project and is available via gromgit's
[homebrew-fuse tap](https://github.com/gromgit/homebrew-fuse):

```
$ brew tap gromgit/homebrew-fuse
$ brew install dwarfs-fuse-mac
```

## Use Cases

### Astrophotography

Astrophotography can generate huge amounts of raw image data. During a
single night, it's not unlikely to end up with a few dozen gigabytes
of data. With most dedicated astrophotography cameras, this data ends up
in the form of FITS images. These are usually uncompressed, don't compress
very well with standard compression algorithms, and while there are certain
compressed FITS formats, these aren't widely supported.

One of the compression formats (simply called "Rice") compresses reasonably
well and is really fast. However, its implementation for compressed FITS
has a few drawbacks. The most severe drawbacks are that compression isn't
quite as good as it could be for color sensors and sensors with a less than
16 bits of resolution.

DwarFS supports the `ricepp` (Rice++) compression, which builds on the basic
idea of Rice compression, but makes a few enhancements: it compresses color
and low bit depth images significantly better and always searches for the
optimum solution during compression instead of relying on a heuristic.

Let's look at an example using 129 images (darks, flats and lights) taken
with an ASI1600MM camera. Each image is 32 MiB, so a total of 4 GiB of data.
Compressing these with the standard `fpack` tool takes about 16.6 seconds
and yields a total output size of 2.2 GiB:

```
$ time fpack */*.fit */*/*.fit

user	14.992
system	1.592
total	16.616

$ find . -name '*.fz' -print0 | xargs -0 cat | wc -c
2369943360
```

However, this leaves you with `*.fz` files that not every application can
actually read.

Using DwarFS, here's what we get:

```
$ mkdwarfs -i ASI1600 -o asi1600-20.dwarfs -S 20 --categorize
I 08:47:47.459077 scanning "ASI1600"
I 08:47:47.491492 assigning directory and link inodes...
I 08:47:47.491560 waiting for background scanners...
I 08:47:47.675241 scanning CPU time: 1.051s
I 08:47:47.675271 finalizing file inodes...
I 08:47:47.675330 saved 0 B / 3.941 GiB in 0/258 duplicate files
I 08:47:47.675360 assigning device inodes...
I 08:47:47.675371 assigning pipe/socket inodes...
I 08:47:47.675381 building metadata...
I 08:47:47.675393 building blocks...
I 08:47:47.675398 saving names and symlinks...
I 08:47:47.675514 updating name and link indices...
I 08:47:47.675796 waiting for segmenting/blockifying to finish...
I 08:47:50.274285 total ordering CPU time: 616.3us
I 08:47:50.274329 total segmenting CPU time: 1.132s
I 08:47:50.279476 saving chunks...
I 08:47:50.279622 saving directories...
I 08:47:50.279674 saving shared files table...
I 08:47:50.280745 saving names table... [1.047ms]
I 08:47:50.280768 saving symlinks table... [743ns]
I 08:47:50.282031 waiting for compression to finish...
I 08:47:50.823924 compressed 3.941 GiB to 1.201 GiB (ratio=0.304825)
I 08:47:50.824280 compression CPU time: 17.92s
I 08:47:50.824316 filesystem created without errors [3.366s]
⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
waiting for block compression to finish
5 dirs, 0/0 soft/hard links, 258/258 files, 0 other
original size: 3.941 GiB, hashed: 315.4 KiB (18 files, 0 B/s)
scanned: 3.941 GiB (258 files, 117.1 GiB/s), categorizing: 0 B/s
saved by deduplication: 0 B (0 files), saved by segmenting: 0 B
filesystem: 3.941 GiB in 4037 blocks (4550 chunks, 516/516 fragments, 258 inodes)
compressed filesystem: 4037 blocks/1.201 GiB written
```

In less than 3.4 seconds, it compresses the data down to 1.2 GiB, almost
half the size of the `fpack` output.

In addition to saving a lot of disk space, this can also be useful when your
data is stored on a NAS. Here's a comparison of the same set of data accessed
over a 1 Gb/s network connection, first using the uncompressed raw data:

```
find /mnt/ASI1600 -name '*.fit' -print0 | xargs -0 -P4 -n1 cat | dd of=/dev/null status=progress
4229012160 bytes (4.2 GB, 3.9 GiB) copied, 36.0455 s, 117 MB/s
```

And next using a DwarFS image on the same share:

```
$ dwarfs /mnt/asi1600-20.dwarfs mnt

$ find mnt -name '*.fit' -print0 | xargs -0 -P4 -n1 cat | dd of=/dev/null status=progress
4229012160 bytes (4.2 GB, 3.9 GiB) copied, 14.3681 s, 294 MB/s
```

That's roughly 2.5 times faster. You can very likely see similar results
with slow external hard drives.

## Dealing with Bit Rot

Currently, DwarFS has no built-in ability to add recovery information to a
file system image. However, for archival purposes, it's a good idea to have
such recovery information in order to be able to repair a damaged image.

This is fortunately relatively straightforward using something like
[par2cmdline](https://github.com/Parchive/par2cmdline):

```
$ par2create -n1 asi1600-20.dwarfs
```

This will create two additional files that you can place alongside the image
(or on a different storage), as you'll only need them if DwarFS has detected
an issue with the file system image. If there's an issue, you can run

```
$ par2repair asi1600-20.dwarfs
```

which will very likely be able to recover the image if less than 5% (that's
the default used by `par2create`) of the image are damaged.

## Extended Attributes

### Preserving Extended Attributes in DwarFS Images

Extended attributes are not currently supported. Any extended attributes
stored in the source file system will not currently be preserved when
building a DwarFS image using `mkdwarfs`.

### Extended Attributes exposed by the FUSE Driver

That being said, the root inode of a mounted DwarFS image currently exposes
one or two extended attributes on Linux:

```
$ attr -l mnt
Attribute "dwarfs.driver.pid" has a 4 byte value for mnt
Attribute "dwarfs.driver.perfmon" has a 4849 byte value for mnt
```

The `dwarfs.driver.pid` attribute simply contains the PID of the DwarFS
FUSE driver. The `dwarfs.driver.perfmon` attribute contains the current
results of the [performance monitor](#performance-monitoring).

Furthermore, each regular file exposes an attribute `dwarfs.inodeinfo`
with information about the underlying inode:

```
$ attr -l "05 Disappear.caf"
Attribute "dwarfs.inodeinfo" has a 448 byte value for 05 Disappear.caf
```

The attribute contains a JSON object with information about the
underlying inode:

```
$ attr -qg dwarfs.inodeinfo "05 Disappear.caf"
{
  "chunks": [
    {
      "block": 2,
      "category": "pcmaudio/metadata",
      "offset": 270976,
      "size": 4096
    },
    {
      "block": 414,
      "category": "pcmaudio/waveform",
      "offset": 37594368,
      "size": 29514492
    },
    {
      "block": 419,
      "category": "pcmaudio/waveform",
      "offset": 0,
      "size": 29385468
    }
  ],
  "gid": 100,
  "mode": 33188,
  "modestring": "----rw-r--r--",
  "uid": 1000
}
```

This is useful, for example, to check how a particular file is spread
across multiple blocks or which [categories](doc/mkdwarfs.md#categorizers)
have been assigned to the file.

## Comparison

The SquashFS, `xz`, `lrzip`, `zpaq` and `wimlib` tests were all done on
an 8 core Intel(R) Xeon(R) E-2286M CPU @ 2.40GHz with 64 GiB of RAM.

The Cromfs tests were done with an older version of DwarFS
on a 6 core Intel(R) Xeon(R) CPU D-1528 @ 1.90GHz with 64 GiB of RAM.

The EROFS tests were done using DwarFS v0.9.8 and EROFS v1.7.1 on an
Intel(R) Core(TM) i9-13900K with 64 GiB of RAM.

The systems were mostly idle during all of the tests.

### With SquashFS

The source directory contained **1139 different Perl installations**
from 284 distinct releases, a total of 47.65 GiB of data in 1,927,501
files and 330,733 directories. The source directory was freshly
unpacked from a tar archive to an XFS partition on a 970 EVO Plus 2TB
NVMe drive, so most of its contents were likely cached.

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

So, why not use `lzma` instead of `zstd` by default? The reason is that `lzma`
is about an order of magnitude slower to decompress than `zstd`. If you're
only accessing data on your compressed file system occasionally, this might
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
DwarFS to reference file chunks from up to two previous file system blocks.

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
I 20:28:03.246534 filesystem rewritten without errors [148.3s]
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

Note that while the recompressed file system is smaller than the original image,
it is still a lot bigger than the file system we previously build with `-l9`.
The reason is that the recompressed image still uses the same block size, and
the block size cannot be changed by recompressing.

In terms of how fast the file system is when using it, a quick test
I've done is to freshly mount the file system created above and run
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

Now here's a comparison with the SquashFS file system:

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

So, DwarFS is almost six times faster than SquashFS. But what's more,
SquashFS also uses significantly more CPU power. However, the numbers
shown above for DwarFS obviously don't include the time spent in the
`dwarfs` process, so I repeated the test outside of hyperfine:

```
$ time dwarfs perl-install.dwarfs mnt -o cachesize=1g -o workers=4 -f

real    0m4.569s
user    0m2.154s
sys     0m1.846s
```

So, in total, DwarFS was using 5.7 seconds of CPU time, whereas
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

This test uses slightly less pathological input data: the root file system of
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
that allows extraction of a file system image without the FUSE driver.
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

So, `dwarfsextract` is almost 4 times faster thanks to using multiple
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
for duplicate segments before passing the de-duplicated data on to
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

So, it's an order of magnitude slower than `mkdwarfs` and uses 14 times
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

A few years later, it was pointed out to me that using `-summary 1` would
suppress most of the output. Also, most people seem to use the default
compression level instead of `-m5`, so let's try that again:

```
$ time zpaq a perl-install.default.zpaq perl-install -summary 1
zpaq v7.15 journaling archiver, compiled Oct 25 2025
Creating perl-install.default.zpaq at offset 0 + 0
Adding 51161.953159 MB in 1927501 files -method 14 -threads 32 at 2025-10-25 07:07:09.
2258234 +added, 0 -removed.

0.000000 + (51161.953159 -> 8932.000297 -> 1143.401788) = 1143.401788 MB
381.062 seconds (all OK)

user	6:24.01
system	42.189
total	6:21.10
```

That is considerably faster, but the resulting archive is also considerably
bigger. Also, the machine I'm running this on is a lot faster, and `mkdwarfs`
takes about 2 minutes to build a 310 MiB image (albeit using significantly
more CPU time).

### With zpaqfranz

[zpaqfranz](https://github.com/fcorbelli/zpaqfranz) is a derivative of zpaq.
Much to my delight, it doesn't generate millions of lines of output.
It claims to be multi-threaded and de-duplicating, so definitely worth
taking a look. Like zpaq, it supports incremental backups.

We'll use a different input to compare zpaqfranz and DwarFS: The source code
of 670 different releases of the "wine" emulator. That's 73 gigabytes of data
in total, spread across slightly more than 3 million files. It's obviously
highly redundant and should thus be a good data set to compare the tools.
For reference, a `.tar.xz` of the directory is still 7 GiB in size and a
SquashFS image of the data gets down to around 1.6 GiB. An "optimized"
`.tar.xz`, where the input files were ordered by similarity, compresses down
to 399 MiB, almost 20 times better than without ordering.

Now it's time to try zpaqfranz. The input data is stored on a fast SSD and a
large fraction of it is already in the file system cache from previous runs,
so disk I/O is not a bottleneck.

```
$ time ./zpaqfranz a winesrc.zpaq winesrc
zpaqfranz v58.8k-JIT-L(2023-08-05)
Creating winesrc.zpaq at offset 0 + 0
Add 2024-01-11 07:25:22 3.117.413     69.632.090.852 (  64.85 GB) 16T (362.904 dirs)
3.480.317 +added, 0 -removed.

0 + (69.632.090.852 -> 8.347.553.798 -> 617.600.892) = 617.600.892 @ 58.38 MB/s

1137.441 seconds (000:18:57) (all OK)

real    18m58.632s
user    11m51.052s
sys     1m3.389s
```

That is considerably faster than the original zpaq, and uses about 60 times
less CPU resources. The output file is 589 MiB, so slightly larger than both
the "optimized" `.tar.gz` and the zpaq output.

How does `mkdwarfs` do?

```
$ time mkdwarfs -i winesrc -o winesrc.dwarfs -l9
[...]
I 07:55:20.546636 compressed 64.85 GiB to 93.2 MiB (ratio=0.00140344)
I 07:55:20.826699 compression CPU time: 6.726m
I 07:55:20.827338 filesystem created without errors [2.283m]
[...]

real    2m17.100s
user    9m53.633s
sys     2m29.236s
```

It uses pretty much the same amount of CPU resources, but finishes more than
8 times faster. The DwarFS output file is more than 6 times smaller.

You can actually squeeze a bit more redundancy out of the original data by
tweaking the similarity ordering and switching from lzma to brotli compression,
albeit at a somewhat slower compression speed:

```
mkdwarfs -i winesrc -o winesrc.dwarfs -l9 -C brotli:quality=11:lgwin=26 --order=nilsimsa:max-cluster-size=200k
[...]
I 08:21:01.138075 compressed 64.85 GiB to 73.52 MiB (ratio=0.00110716)
I 08:21:01.485737 compression CPU time: 36.58m
I 08:21:01.486313 filesystem created without errors [5.501m]
[...]
real    5m30.178s
user    40m59.193s
sys     2m36.234s
```

That's almost a 1000x reduction in size.

Let's also look at decompression speed:

```
$ time zpaqfranz x winesrc.zpaq
zpaqfranz v58.8k-JIT-L(2023-08-05)
/home/mhx/winesrc.zpaq:
1 versions, 3.480.317 files, 617.600.892 bytes (588.99 MB)
Extract 69.632.090.852 bytes (64.85 GB) in 3.117.413 files (362.904 folders) / 16 T
        99.18% 00:00:00  (  64.32 GB)=>(  64.85 GB)  548.83 MB/sec

125.636 seconds (000:02:05) (all OK)

real    2m6.968s
user    1m36.177s
sys     1m10.980s
```

```
$ time dwarfsextract -i winesrc.dwarfs

real    1m49.182s
user    0m34.667s
sys     1m28.733s
```

Decompression time is pretty much in the same ballpark, with just slightly
shorter times for the DwarFS image.

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

So, wimlib is definitely much better than squashfs, in terms of both
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

So, it processed 21 MiB out of 48 GiB in half an hour, using almost
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

So, `mkdwarfs` is about 50 times faster than `mkcromfs` and uses 75 times
less CPU resources. At the same time, the DwarFS file system is 30% smaller:

```
$ ls -l perl-install-small.*fs
-rw-r--r-- 1 mhx users 35328512 Dec  8 14:25 perl-install-small.cromfs
-rw-r--r-- 1 mhx users 25175016 Dec 10 21:14 perl-install-small.dwarfs
```

I noticed that the `blockifying` step that took ages for the full dataset
with `mkcromfs` ran substantially faster (in terms of MiB/second) on the
smaller dataset, which makes me wonder if there's some quadratic complexity
behavior that's slowing down `mkcromfs`.

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

[EROFS](https://github.com/erofs/erofs-utils) is another read-only
compressed file system included in the Linux kernel.
Its goals are different from those of DwarFS, though. It is designed to
be lightweight (which DwarFS is definitely not) and to run on constrained
hardware like embedded devices or smartphones. It is not designed to provide
maximum compression. It currently supports LZ4 and LZMA compression.

Running it on the full Perl dataset using options given in the README for
"well-compressed images":

```
$ time mkfs.erofs -C1048576 -Eztailpacking,fragments,all-fragments,dedupe -zlzma,9 perl-install-lzma9.erofs perl-install
mkfs.erofs 1.7.1-gd93a18c9
<W> erofs: It may take a longer time since MicroLZMA is still single-threaded for now.
Build completed.
------
Filesystem UUID: 538ce164-5f9d-4a6a-9808-5915f17ced30
Filesystem total blocks: 599854 (of 4096-byte blocks)
Filesystem total inodes: 2255795
Filesystem total metadata blocks: 74253
Filesystem total deduplicated bytes (of source files): 29625028195

user	2:35:08.03
system	1:12.65
total	2:39:25.35

$ ll -h perl-install-lzma9.erofs
-rw-r--r-- 1 mhx mhx 2.3G Apr 15 16:23 perl-install-lzma9.erofs
```

That's definitely slower than SquashFS, but also significantly smaller.

For a fair comparison, let's use the same 1 MiB block size with DwarFS,
but also tweak the options for best compression:

```
$ time mkdwarfs -i perl-install -o perl-install-1M.dwarfs -l9 -S20 -B64 --order=nilsimsa:max-cluster-size=150000
[...]
330733 dirs, 0/2440 soft/hard links, 1927501/1927501 files, 0 other
original size: 47.49 GiB, hashed: 43.47 GiB (1920025 files, 1.451 GiB/s)
scanned: 19.45 GiB (144675 files, 159.3 MiB/s), categorizing: 0 B/s
saved by deduplication: 28.03 GiB (1780386 files), saved by segmenting: 15.4 GiB
filesystem: 4.053 GiB in 4151 blocks (937069 chunks, 144674/144674 fragments, 144675 inodes)
compressed filesystem: 4151 blocks/806.2 MiB written
[...]
user	24:27.47
system	4:20.74
total	3:26.79
```

That's significantly smaller and, almost more importantly, 46 times
faster than `mkfs.erofs`.

Actually using the file system images, here's how DwarFS performs:

```
$ dwarfs perl-install-1M.dwarfs mnt -o workers=8
$ find mnt -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M status=progress
50392172594 bytes (50 GB, 47 GiB) copied, 19 s, 2.7 GB/s
0+1662649 records in
0+1662649 records out
51161953159 bytes (51 GB, 48 GiB) copied, 19.4813 s, 2.6 GB/s
```

Reading every single file from 16 parallel processes took less than
20 seconds. The FUSE driver consumed 143 seconds of CPU time.

Here's the same for EROFS:

```
$ erofsfuse perl-install-lzma9.erofs mnt
$ find mnt -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M status=progress
2594306810 bytes (2.6 GB, 2.4 GiB) copied, 300 s, 8.6 MB/s^C
0+133296 records in
0+133296 records out
2595212832 bytes (2.6 GB, 2.4 GiB) copied, 300.336 s, 8.6 MB/s
```

Note that I've stopped this after 5 minutes. The DwarFS FUSE driver
delivered about 300 times faster throughput compared to EROFS. The
EROFS FUSE driver consumed 50 minutes (!) of CPU time for only about
5% of the data, i.e. more than 400 times the CPU time consumed by
the DwarFS FUSE driver.

I've tried two more EROFS configurations on the same set of data.
The first one uses more or less just the defaults:

```
$ time mkfs.erofs -zlz4hc,12 perl-install-lz4hc.erofs perl-install
mkfs.erofs 1.7.1-gd93a18c9
Build completed.
------
Filesystem UUID: b75142ed-6cf3-46a4-84f3-12693f7759a0
Filesystem total blocks: 5847130 (of 4096-byte blocks)
Filesystem total inodes: 2255794
Filesystem total metadata blocks: 419699
Filesystem total deduplicated bytes (of source files): 0

user	3:38:23.36
system	1:10.84
total	3:41:37.33
```

The second one additionally enables the `-Ededupe` option:

```
$ time mkfs.erofs -zlz4hc,12 -Ededupe perl-install-lz4hc-dedupe.erofs perl-install
mkfs.erofs 1.7.1-gd93a18c9
Build completed.
------
Filesystem UUID: 0ccf581e-ad3b-4d08-8b10-5b7e15f8e3cd
Filesystem total blocks: 1510091 (of 4096-byte blocks)
Filesystem total inodes: 2255794
Filesystem total metadata blocks: 435599
Filesystem total deduplicated bytes (of source files): 19220717568

user	4:19:57.61
system	1:21.62
total	4:23:55.85
```

I don't know why these are even slower than the first, seemingly more
complex, set of options. As was to be expected, the resulting images
were significantly bigger:

```
$ ll -h perl-install*.erofs
-rw-r--r-- 1 mhx mhx 5.8G Apr 16 02:46 perl-install-lz4hc-dedupe.erofs
-rw-r--r-- 1 mhx mhx  23G Apr 15 22:34 perl-install-lz4hc.erofs
-rw-r--r-- 1 mhx mhx 2.3G Apr 15 16:23 perl-install-lzma9.erofs
```

The good news is that these perform *much* better and even outperform
DwarFS, albeit by a small margin:

```
$ erofsfuse perl-install-lz4hc.erofs mnt
$ find mnt -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M status=progress
49920168315 bytes (50 GB, 46 GiB) copied, 16 s, 3.1 GB/s
0+1493031 records in
0+1493031 records out
51161953159 bytes (51 GB, 48 GiB) copied, 16.4329 s, 3.1 GB/s
```

The deduplicated version is even a tiny bit faster:

```
$ erofsfuse perl-install-lz4hc-dedupe.erofs mnt
find mnt -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M status=progress
50808037121 bytes (51 GB, 47 GiB) copied, 16 s, 3.2 GB/s
0+1499949 records in
0+1499949 records out
51161953159 bytes (51 GB, 48 GiB) copied, 16.1184 s, 3.2 GB/s
```

The EROFS kernel driver wasn't any faster than the FUSE driver.

The FUSE driver used about 27 seconds of CPU time in both cases,
substantially less than before and 5 times less than DwarFS.

DwarFS can get close to the throughput of EROFS by using `zstd` instead
of `lzma` compression:

```
$ dwarfs perl-install-1M-zstd.dwarfs mnt -o workers=8
find mnt -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M status=progress
49224202357 bytes (49 GB, 46 GiB) copied, 16 s, 3.1 GB/s
0+1529018 records in
0+1529018 records out
51161953159 bytes (51 GB, 48 GiB) copied, 16.6716 s, 3.1 GB/s
```

### With fuse-archive

I came across [fuse-archive](https://github.com/google/fuse-archive)
while looking for FUSE drivers to mount archives and it seems to be
the most versatile of the alternatives (and the one that actually
compiles out of the box).

An interesting test case straight from fuse-archive's README is in
the [Performance](https://github.com/google/fuse-archive#performance)
section: an archive with a single huge file full of zeroes. Let's
make the example a bit more extreme and use a 1 GiB file instead of
just 256 MiB:

```
$ mkdir zerotest
$ truncate --size=1G zerotest/zeroes
```

Now, we build several different archives and a DwarFS image:

```
$ time mkdwarfs -i zerotest -o zerotest.dwarfs -W16 --log-level=warn --progress=none

real    0m7.604s
user    0m7.521s
sys     0m0.083s

$ time zip -9 zerotest.zip zerotest/zeroes
  adding: zerotest/zeroes (deflated 100%)

real    0m4.923s
user    0m4.840s
sys     0m0.080s

$ time 7z a -bb0 -bd zerotest.7z zerotest/zeroes

7-Zip [64] 16.02 : Copyright (c) 1999-2016 Igor Pavlov : 2016-05-21
p7zip Version 16.02 (locale=en_US.UTF-8,Utf16=on,HugeFiles=on,64 bits,16 CPUs Intel(R) Xeon(R) E-2286M  CPU @ 2.40GHz (906ED),ASM,AES-NI)

Scanning the drive:
1 file, 1073741824 bytes (1024 MiB)

Creating archive: zerotest.7z

Items to compress: 1

Files read from disk: 1
Archive size: 157819 bytes (155 KiB)
Everything is Ok

real    0m5.535s
user    0m48.281s
sys     0m1.116s

$ time tar --zstd -cf zerotest.tar.zstd zerotest/zeroes

real    0m0.449s
user    0m0.510s
sys     0m0.610s
```

Turns out that `tar --zstd` is easily winning the compression speed
test. Looking at the file sizes did genuinely surprise me:

```
$ ll zerotest.* --sort=size
-rw-r--r-- 1 mhx users 1042231 Jul  1 15:24 zerotest.zip
-rw-r--r-- 1 mhx users  157819 Jul  1 15:26 zerotest.7z
-rw-r--r-- 1 mhx users   33762 Jul  1 15:28 zerotest.tar.zstd
-rw-r--r-- 1 mhx users     848 Jul  1 15:23 zerotest.dwarfs
```

I definitely didn't expect the DwarFS image to be *that* small.
Dropping the section index would actually save another 100 bytes.
So, if you want to archive lots of zeroes, DwarFS is your friend.

Anyway, let's look at how fast and efficiently the zeroes can
be read from the different archives. First, the `zip` archive:

```
$ time dd if=mnt/zerotest/zeroes of=/dev/null status=progress
1020117504 bytes (1.0 GB, 973 MiB) copied, 2 s, 510 MB/s
2097152+0 records in
2097152+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 2.10309 s, 511 MB/s

real    0m2.104s
user    0m0.264s
sys     0m0.486s
```

CPU time used by the FUSE driver was 1.8 seconds and mount time
was in the milliseconds.

Now, the `7z` archive:

```
 $ time dd if=mnt/zerotest/zeroes of=/dev/null status=progress
594759168 bytes (595 MB, 567 MiB) copied, 1 s, 595 MB/s
2097152+0 records in
2097152+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 1.76904 s, 607 MB/s

real    0m1.772s
user    0m0.229s
sys     0m0.572s
```

CPU time used by the FUSE driver was 2.9 seconds and mount time
was just over 1.0 seconds.

Now, the `.tar.zstd` archive:

```
$ time dd if=mnt/zerotest/zeroes of=/dev/null status=progress
2097152+0 records in
2097152+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.799409 s, 1.3 GB/s

real    0m0.801s
user    0m0.262s
sys     0m0.537s
```

CPU time used by the FUSE driver was 0.53 seconds and mount time
was 0.13 seconds.

Last but not least, let's look at DwarFS:

```
$ time dd if=mnt/zeroes of=/dev/null status=progress
2097152+0 records in
2097152+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.753 s, 1.4 GB/s

real    0m0.757s
user    0m0.220s
sys     0m0.534s
```

CPU time used by the FUSE driver was 0.17 seconds and mount time
was less than a millisecond.

If we increase the block size for the `dd` command, we can get
even higher throughput. For fuse-archive with the `.tar.zstd`:

```
$ time dd if=mnt/zerotest/zeroes of=/dev/null status=progress bs=16384
65536+0 records in
65536+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.318682 s, 3.4 GB/s

real    0m0.323s
user    0m0.005s
sys     0m0.154s
```

And for DwarFS:

```
$ time dd if=mnt/zeroes of=/dev/null status=progress bs=16384
65536+0 records in
65536+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.172226 s, 6.2 GB/s

real    0m0.176s
user    0m0.020s
sys     0m0.141s
```

This is all nice, but what about a more real-life use case?
Let's take the 1.82.0 boost release archives:

```
$ ll --sort=size boost_1_82_0.*
-rw-r--r-- 1 mhx users 208188085 Apr 10 14:25 boost_1_82_0.zip
-rw-r--r-- 1 mhx users 142580547 Apr 10 14:23 boost_1_82_0.tar.gz
-rw-r--r-- 1 mhx users 121325129 Apr 10 14:23 boost_1_82_0.tar.bz2
-rw-r--r-- 1 mhx users 105901369 Jun 28 12:47 boost_1_82_0.dwarfs
-rw-r--r-- 1 mhx users 103710551 Apr 10 14:25 boost_1_82_0.7z
```

Here are the timings for mounting each archive and then using
`tar` to build another archive from the mountpoint and just counting
the number of bytes in that archive, e.g.:

```
$ time tar cf - mnt | wc -c
803614720

real    0m4.602s
user    0m0.156s
sys     0m1.123s
```

Here are the results in terms of wallclock time and FUSE driver
CPU time:

| Archive    | Mount Time | `tar` Wallclock Time | FUSE Driver CPU Time |
| ---------- | ---------: | -------------------: | -------------------: |
| `.zip`     |     0.458s |               5.073s |               4.418s |
| `.tar.gz`  |     1.391s |               3.483s |               3.943s |
| `.tar.bz2` |    15.663s |              17.942s |              32.040s |
| `.7z`      |     0.321s |              32.554s |              31.625s |
| `.dwarfs`  |     0.013s |               2.974s |               1.984s |

DwarFS easily wins all categories while still compressing the data
almost as well as `7z`.

What about accessing files more randomly?

```
$ find mnt -type f -print0 | xargs -0 -P32 -n32 cat | dd of=/dev/null status=progress
```

It turns out that fuse-archive grinds to a halt in this case, so I had
to run the test on a subset (the `boost` subdirectory) of the data.
The `.tar.bz2` and `.7z` archives were so slow to read that I stopped
them after a few minutes.

| Archive    | Throughput | Wallclock Time | FUSE Driver CPU Time |
| ---------- | ---------: | -------------: | -------------------: |
| `.zip`     |   1.8 MB/s |        83.245s |              83.669s |
| `.tar.gz`  |   1.2 MB/s |       121.377s |             122.711s |
| `.tar.bz2` |   0.2 MB/s |              - |                    - |
| `.7z`      |   0.3 MB/s |              - |                    - |
| `.dwarfs`  | 598.0 MB/s |         0.249s |               1.099s |


## Performance Monitoring

Both the FUSE driver and `dwarfsextract` by default have support for
simple performance monitoring. You can build binaries without this
feature (`-DENABLE_PERFMON=OFF`), but impact should be negligible even
if performance monitoring is enabled at run-time.

To enable the performance monitor, you pass a list of components for which
you want to collect latency metrics, e.g.:

```
$ dwarfs test.dwarfs mnt -f -o perfmon=fuse
```

When the driver exits, you will see output like this:

```
[fuse.op_read]
      samples: 45145
      overall: 3.214s
  avg latency: 71.2us
  p50 latency: 131.1us
  p90 latency: 131.1us
  p99 latency: 262.1us

[fuse.op_readdir]
      samples: 2
      overall: 51.31ms
  avg latency: 25.65ms
  p50 latency: 32.77us
  p90 latency: 67.11ms
  p99 latency: 67.11ms

[fuse.op_lookup]
      samples: 16
      overall: 19.98ms
  avg latency: 1.249ms
  p50 latency: 2.097ms
  p90 latency: 4.194ms
  p99 latency: 4.194ms

[fuse.op_init]
      samples: 1
      overall: 199.4us
  avg latency: 199.4us
  p50 latency: 262.1us
  p90 latency: 262.1us
  p99 latency: 262.1us

[fuse.op_open]
      samples: 16
      overall: 122.2us
  avg latency: 7.641us
  p50 latency: 4.096us
  p90 latency: 32.77us
  p99 latency: 32.77us

[fuse.op_getattr]
      samples: 1
      overall: 5.786us
  avg latency: 5.786us
  p50 latency: 8.192us
  p90 latency: 8.192us
  p99 latency: 8.192us
```

The metrics should be self-explanatory. However, note that the
percentile metrics are logarithmically quantized in order to use
as little resources as possible. As a result, you will only see
values that look an awful lot like powers of two.

Currently, the supported components are `fuse` for the FUSE
operations, `filesystem_v2` for the DwarFS file system component
and `inode_reader_v2` for the component that handles all `read()`
system calls.

The FUSE driver also exposes the performance monitor metrics via
an [extended attribute](#extended-attributes).


## Other Obscure Features

### Setting Worker Thread CPU Affinity

This only works on Linux and usually only makes sense if you have CPUs
with different types of cores (e.g. "performance" vs "efficiency" cores)
and are *really* trying to squeeze the last ounce of speed out of DwarFS.

By setting the environment variable `DWARFS_WORKER_GROUP_AFFINITY`, you
can set the CPU affinity of different worker thread groups, e.g.:

```
export DWARFS_WORKER_GROUP_AFFINITY=blockify=3:compress=6,7
```

This will set the affinity of the `blockify` worker group to CPU 3 and
the affinity of the `compress` worker group to CPUs 6 and 7.

You can use this feature for all tools that use one or more worker thread
groups. For example, the FUSE driver `dwarfs` and `dwarfsextract` use a
worker group `blkcache` that the block cache (i.e. block decompression and
lookup) runs on. `mkdwarfs` uses a whole array of different worker groups,
namely `compress` for compression, `scanner` for scanning, `ordering` for
input ordering, and `blockify` for segmenting. `blockify` is what you would
typically want to run on your "performance" cores.

### Specifying file system offset and size

You can specify the byte offset at which the file system is located in the
file using the `-o offset=N` option. This can be useful when mounting images
where there is some preceding data before the file system or when mounting
merged/concatenated images. When combined with the `-o imagesize=N` option
you can mount merged file systems, i.e. multiple file systems stored in a
single file.

Here is an example, you have two file systems concatenated into a single
file and you want to mount both of them, you can achieve this by running:
```sh
dwarfs merged.dwarfs /mnt/fs1 -o imagesize=9231
dwarfs merged.dwarfs /mnt/fs2 -o offset=9231,imagesize=7999
```

## Related Projects

[dwarfs-rs](https://github.com/oxalica/dwarfs-rs): A library for reading and writing DwarFS archives (aka. DwarFS images).

## Notable Users

[Conty: easy to use unprivileged Linux container packed into a single portable executable](https://github.com/Kron4ek/Conty)

[Tebako: an executable packager for Ruby programs](https://github.com/tamatebako/tebako)

[Runimage: portable single-file linux container](https://github.com/VHSgunzo/runimage)

[PELF - The AppBundle format and the AppBundle Creation Tool](https://github.com/xplshn/pelf)

[uruntime: Universal RunImage and AppImage runtime with SquashFS and DwarFS supports](https://github.com/VHSgunzo/uruntime)

## Stargazers over Time

[![Stargazers over Time](https://starchart.cc/mhx/dwarfs.svg?variant=adaptive)](https://starchart.cc/mhx/dwarfs)

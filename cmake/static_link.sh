#!/bin/bash
set -eu

target=$1
shift

fuse=""
if [[ "$target" == "dwarfs" ]]; then
	fuse="/usr/lib/x86_64-linux-gnu/libfuse3.a"
elif [[ "$target" == "dwarfs2" ]]; then
	fuse="/usr/lib/x86_64-linux-gnu/libfuse.a"
fi

extra=""
if [[ "$target" == *_test ]]; then
	extra="lib/libgtest.a lib/libgtest_main.a"
fi
if [[ "$target" == "dwarfs_test" || "$target" == "dwarfs_tools_test" ]]; then
	extra="$extra libtest_helpers.a"
fi

g++ -static "$@" -o "$target" \
	libdwarfs.a \
	libfsst.a \
	libmetadata_thrift.a \
	libthrift_light.a \
	libxxhash.a \
	folly/libfolly.a \
	zstd/build/cmake/lib/libzstd.a \
	$fuse \
	$extra \
	/usr/lib/x86_64-linux-gnu/libfmt.a \
	/usr/lib/x86_64-linux-gnu/libboost_context.a \
	/usr/lib/x86_64-linux-gnu/libboost_regex.a \
	/usr/lib/x86_64-linux-gnu/libboost_thread.a \
	/usr/lib/x86_64-linux-gnu/libboost_atomic.a \
	/usr/lib/x86_64-linux-gnu/libboost_date_time.a \
	/usr/lib/x86_64-linux-gnu/libboost_filesystem.a \
	/usr/lib/x86_64-linux-gnu/libboost_program_options.a \
	/usr/lib/x86_64-linux-gnu/libboost_system.a \
	/usr/lib/x86_64-linux-gnu/libdouble-conversion.a \
	/usr/lib/x86_64-linux-gnu/libglog.a \
	/usr/lib/x86_64-linux-gnu/libgflags.a \
	/usr/lib/x86_64-linux-gnu/libevent.a \
	/usr/local/lib/libarchive.a \
	/usr/lib/libacl.a \
	/usr/local/lib/libxml2.a \
	/usr/lib/x86_64-linux-gnu/libssl.a \
	/usr/lib/x86_64-linux-gnu/libcrypto.a \
	/usr/lib/x86_64-linux-gnu/libiberty.a \
	/usr/lib/x86_64-linux-gnu/liblz4.a \
	/usr/lib/x86_64-linux-gnu/libz.a \
	/usr/lib/gcc/x86_64-linux-gnu/10/libatomic.a \
	/usr/lib/x86_64-linux-gnu/libjemalloc.a \
	/usr/lib/x86_64-linux-gnu/libpthread.a \
	/usr/lib/x86_64-linux-gnu/libdl.a \
	/usr/lib/x86_64-linux-gnu/libc.a \
	/usr/lib/x86_64-linux-gnu/libm.a \
	/usr/lib/x86_64-linux-gnu/librt.a \
	/usr/lib/gcc/x86_64-linux-gnu/10/libgcc_eh.a \
	/usr/lib/x86_64-linux-gnu/libunwind.a \
	/usr/lib/x86_64-linux-gnu/liblzma.a

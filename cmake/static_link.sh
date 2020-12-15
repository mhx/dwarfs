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

g++ -static -static-libgcc -static-libstdc++ "$@" -o "$target" \
	-Wl,-allow-multiple-definition -Wl,-Bstatic \
        libdwarfs.a \
	libmetadata_thrift.a \
	libthrift_light.a \
	folly/libfolly.a \
	zstd/build/cmake/lib/libzstd.a \
	$fuse \
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
	/usr/lib/x86_64-linux-gnu/libgflags.a \
	/usr/lib/x86_64-linux-gnu/libglog.a \
	/usr/lib/x86_64-linux-gnu/libevent.a \
	/usr/lib/x86_64-linux-gnu/libz.a \
	/usr/lib/x86_64-linux-gnu/libssl.a \
	/usr/lib/x86_64-linux-gnu/libcrypto.a \
	/usr/lib/x86_64-linux-gnu/libiberty.a \
	/usr/lib/x86_64-linux-gnu/libunwind.a \
	/usr/lib/x86_64-linux-gnu/liblz4.a \
	/usr/lib/x86_64-linux-gnu/liblzma.a \
	/usr/lib/x86_64-linux-gnu/libz.a \
	/usr/lib/gcc/x86_64-linux-gnu/10/libatomic.a \
	/usr/lib/x86_64-linux-gnu/libjemalloc.a \
	/usr/lib/x86_64-linux-gnu/libpthread.a \
	/usr/lib/x86_64-linux-gnu/libdl.a \
	/usr/lib/x86_64-linux-gnu/libc.a \
	/usr/lib/x86_64-linux-gnu/libm.a \
	/usr/lib/x86_64-linux-gnu/librt.a

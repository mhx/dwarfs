#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

source "$(dirname "$0")/static-libs-versions.sh"

fetch_lib() {
    local lib="$1"
    local url="$2"
    local tarball="${3:-${url##*/}}"
    fetch.sh "$url" "$tarball"
}

fetch_lib bzip2 https://sourceware.org/pub/bzip2/${BZIP2_TARBALL}
fetch_lib boost https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz ${BOOST_TARBALL}
fetch_lib libarchive https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/${LIBARCHIVE_TARBALL}
fetch_lib flac https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/${FLAC_TARBALL}
fetch_lib libucontext https://github.com/kaniini/libucontext/archive/refs/tags/${LIBUCONTEXT_TARBALL}
fetch_lib libunwind https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/${LIBUNWIND_TARBALL}
fetch_lib benchmark https://github.com/google/benchmark/archive/refs/tags/v${BENCHMARK_VERSION}.tar.gz ${BENCHMARK_TARBALL}
fetch_lib blake3 https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/${BLAKE3_VERSION}.tar.gz ${BLAKE3_TARBALL}
fetch_lib openssl https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}
fetch_lib libressl https://github.com/libressl/portable/releases/download/v${LIBRESSL_VERSION}/${LIBRESSL_TARBALL}
fetch_lib cpptrace https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v${CPPTRACE_VERSION}.tar.gz ${CPPTRACE_TARBALL}
fetch_lib double-conversion https://github.com/google/double-conversion/archive/refs/tags/v${DOUBLE_CONVERSION_VERSION}.tar.gz ${DOUBLE_CONVERSION_TARBALL}
fetch_lib fmt https://github.com/fmtlib/fmt/archive/refs/tags/${FMT_VERSION}.tar.gz ${FMT_TARBALL}
fetch_lib glog https://github.com/google/glog/archive/refs/tags/v${GLOG_VERSION}.tar.gz ${GLOG_TARBALL}
fetch_lib xxhash https://github.com/Cyan4973/xxHash/archive/refs/tags/v${XXHASH_VERSION}.tar.gz ${XXHASH_TARBALL}
fetch_lib lz4 https://github.com/lz4/lz4/releases/download/v${LZ4_VERSION}/${LZ4_TARBALL}
fetch_lib brotli https://github.com/google/brotli/archive/refs/tags/v${BROTLI_VERSION}.tar.gz ${BROTLI_TARBALL}
fetch_lib zstd https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${ZSTD_TARBALL}
fetch_lib fuse https://github.com/libfuse/libfuse/releases/download/fuse-${LIBFUSE_VERSION}/${LIBFUSE_TARBALL}
fetch_lib fuse3 https://github.com/libfuse/libfuse/releases/download/fuse-${LIBFUSE3_VERSION}/${LIBFUSE3_TARBALL}
fetch_lib mimalloc https://github.com/microsoft/mimalloc/archive/refs/tags/v${MIMALLOC_VERSION}.tar.gz ${MIMALLOC_TARBALL}
fetch_lib jemalloc https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/${JEMALLOC_TARBALL}
fetch_lib xz https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/${XZ_TARBALL}
fetch_lib libdwarf https://github.com/davea42/libdwarf-code/releases/download/v${LIBDWARF_VERSION}/${LIBDWARF_TARBALL}
fetch_lib libevent https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}-stable/${LIBEVENT_TARBALL}
fetch_lib nlohmann https://github.com/nlohmann/json/releases/download/v${NLOHMANN_VERSION}/json.hpp
fetch_lib utfcpp https://github.com/nemtrif/utfcpp/archive/refs/tags/v${UTFCPP_VERSION}.tar.gz ${UTFCPP_TARBALL}
fetch_lib range-v3 https://github.com/ericniebler/range-v3/archive/refs/tags/${RANGE_V3_VERSION}.tar.gz ${RANGE_V3_TARBALL}
fetch_lib parallel-hashmap https://github.com/greg7mdp/parallel-hashmap/archive/refs/tags/v${PARALLEL_HASHMAP_VERSION}.tar.gz ${PARALLEL_HASHMAP_TARBALL}

# file is special, as you often receive crap from the ftp server
RETRY=0
while true; do
    if fetch.sh "ftp://ftp.astron.com/pub/file/$FILE_TARBALL" "$FILE_TARBALL" "$FILE_SHA512"; then
        break
    fi
    RETRY=$((RETRY+1))
    if [ $RETRY -gt 10 ]; then
        echo "Failed to download $FILE_TARBALL"
        exit 1
    fi
done

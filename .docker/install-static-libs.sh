#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

GCC="${1:-gcc}"
CLANG="${2:-clang}"
PKGS="${3:-:none}"

FILE_VERSION=5.46
FILE_SHA512=a6cb7325c49fd4af159b7555bdd38149e48a5097207acbe5e36deb5b7493ad6ea94d703da6e0edece5bb32959581741f4213707e5cb0528cd46d75a97a5242dc
BZIP2_VERSION=1.0.8
LIBARCHIVE_VERSION=3.7.7
FLAC_VERSION=1.5.0
# TODO: https://github.com/libunwind/libunwind/issues/702
LIBUNWIND_VERSION=1.7.2
BENCHMARK_VERSION=1.9.1
OPENSSL_VERSION=3.3.3
CPPTRACE_VERSION=0.8.2
DOUBLE_CONVERSION_VERSION=3.3.1
FMT_VERSION=11.1.4
GLOG_VERSION=0.7.1
XXHASH_VERSION=0.8.3
LZ4_VERSION=1.10.0
BROTLI_VERSION=1.1.0
ZSTD_VERSION=1.5.7

echo "Using $GCC and $CLANG"

if [[ "$PKGS" == ":ubuntu" ]]; then
    PKGS="file,bzip2,libarchive,flac,libunwind,benchmark,openssl,cpptrace"
    COMPILERS="clang gcc"
elif [[ "$PKGS" == ":alpine" ]]; then
    PKGS="benchmark,brotli,bzip2,cpptrace,double-conversion,flac,fmt,glog,libarchive,lz4,openssl,xxhash,zstd"
    export COMMON_CFLAGS="-Os -ffunction-sections -fdata-sections -fmerge-all-constants"
    export COMMON_CXXFLAGS="$COMMON_CFLAGS"
    COMPILERS="clang gcc clang-lto gcc-lto"
elif [[ "$PKGS" == ":none" ]]; then
    echo "No libraries to build"
    exit 0
fi

FILE_TARBALL="file-${FILE_VERSION}.tar.gz"
BZIP2_TARBALL="bzip2-${BZIP2_VERSION}.tar.gz"
LIBARCHIVE_TARBALL="libarchive-${LIBARCHIVE_VERSION}.tar.xz"
FLAC_TARBALL="flac-${FLAC_VERSION}.tar.xz"
LIBUNWIND_TARBALL="libunwind-${LIBUNWIND_VERSION}.tar.gz"
BENCHMARK_TARBALL="benchmark-${BENCHMARK_VERSION}.tar.gz"
OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
CPPTRACE_TARBALL="cpptrace-${CPPTRACE_VERSION}.tar.gz"
DOUBLE_CONVERSION_TARBALL="double-conversion-${DOUBLE_CONVERSION_VERSION}.tar.gz"
FMT_TARBALL="fmt-${FMT_VERSION}.tar.gz"
GLOG_TARBALL="glog-${GLOG_VERSION}.tar.gz"
XXHASH_TARBALL="xxHash-${XXHASH_VERSION}.tar.gz"
LZ4_TARBALL="lz4-${LZ4_VERSION}.tar.gz"
BROTLI_TARBALL="brotli-${BROTLI_VERSION}.tar.gz"
ZSTD_TARBALL="zstd-${ZSTD_VERSION}.tar.gz"

use_lib() {
    local lib="$1"
    if [[ ",$PKGS," == *",$lib,"* ]]; then
        return 0
    else
        return 1
    fi
}

if use_lib file; then
    RETRY=0
    while true; do
        rm -f "$FILE_TARBALL"
        curl -o "$FILE_TARBALL" "ftp://ftp.astron.com/pub/file/$FILE_TARBALL"
        if echo "${FILE_SHA512}  $FILE_TARBALL" | sha512sum -c; then
            break
        fi
        RETRY=$((RETRY+1))
        if [ $RETRY -gt 10 ]; then
            echo "Failed to download $FILE_TARBALL"
            exit 1
        fi
    done
fi

fetch_lib() {
    local lib="$1"
    local url="$2"
    local tarball="${3:-${url##*/}}"
    if use_lib "$lib"; then
        wget -O "$tarball" "$url"
    fi
}

fetch_lib bzip2 https://sourceware.org/pub/bzip2/${BZIP2_TARBALL}
fetch_lib libarchive https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/${LIBARCHIVE_TARBALL}
fetch_lib flac https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/${FLAC_TARBALL}
fetch_lib libunwind https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/${LIBUNWIND_TARBALL}
fetch_lib benchmark https://github.com/google/benchmark/archive/refs/tags/v${BENCHMARK_VERSION}.tar.gz ${BENCHMARK_TARBALL}
fetch_lib openssl https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}
fetch_lib cpptrace https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v${CPPTRACE_VERSION}.tar.gz ${CPPTRACE_TARBALL}
fetch_lib double-conversion https://github.com/google/double-conversion/archive/refs/tags/v${DOUBLE_CONVERSION_VERSION}.tar.gz ${DOUBLE_CONVERSION_TARBALL}
fetch_lib fmt https://github.com/fmtlib/fmt/archive/refs/tags/${FMT_VERSION}.tar.gz ${FMT_TARBALL}
fetch_lib glog https://github.com/google/glog/archive/refs/tags/v${GLOG_VERSION}.tar.gz ${GLOG_TARBALL}
fetch_lib xxhash https://github.com/Cyan4973/xxHash/archive/refs/tags/v${XXHASH_VERSION}.tar.gz ${XXHASH_TARBALL}
fetch_lib lz4 https://github.com/lz4/lz4/releases/download/v${LZ4_VERSION}/${LZ4_TARBALL}
fetch_lib brotli https://github.com/google/brotli/archive/refs/tags/v${BROTLI_VERSION}.tar.gz ${BROTLI_TARBALL}
fetch_lib zstd https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${ZSTD_TARBALL}

for COMPILER in $COMPILERS; do
    case "$COMPILER" in
        clang*)
            export CC="$CLANG"
            export CXX="${CLANG/clang/clang++}"
            ;;
        gcc*)
            export CC="$GCC"
            export CXX="${GCC/gcc/g++}"
            ;;
        *)
            echo "Unknown compiler: $COMPILER"
            exit 1
            ;;
    esac

    case "$COMPILER" in
        *-lto)
            export CFLAGS="$COMMON_CFLAGS -flto"
            export CXXFLAGS="$COMMON_CXXFLAGS -flto"
            export LDFLAGS="$COMMON_LDFLAGS -flto"
            ;;
        *-lto-thin)
            export CFLAGS="$COMMON_CFLAGS -flto=thin"
            export CXXFLAGS="$COMMON_CXXFLAGS -flto=thin"
            export LDFLAGS="$COMMON_LDFLAGS -flto=thin"
            ;;
        *)
            unset CFLAGS
            unset CXXFLAGS
            unset LDFLAGS
            ;;
    esac

    cd "$HOME/pkgs"
    mkdir $COMPILER

    INSTALL_DIR=/opt/static-libs/$COMPILER

    if use_lib zstd; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${ZSTD_TARBALL}
        cd zstd-${ZSTD_VERSION}
        make -j$(nproc)
        make install PREFIX="$INSTALL_DIR"
    fi

    if use_lib libunwind; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBUNWIND_TARBALL}
        cd libunwind-${LIBUNWIND_VERSION}
        ./configure --prefix="$INSTALL_DIR"
        make -j$(nproc)
        make install
    fi

    if use_lib bzip2; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BZIP2_TARBALL}
        cd bzip2-${BZIP2_VERSION}
        make -j$(nproc)
        make PREFIX="$INSTALL_DIR" install
    fi

    if use_lib openssl; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${OPENSSL_TARBALL}
        cd openssl-${OPENSSL_VERSION}
        ./Configure --prefix="$INSTALL_DIR" --libdir=lib threads no-fips no-shared no-pic no-dso no-aria no-async no-atexit \
                no-autoload-config no-blake2 no-bf no-camellia no-cast no-chacha no-cmac no-cms no-cmp no-comp no-ct no-des \
                no-dgram no-dh no-dsa no-ec no-engine no-filenames no-idea no-ktls no-md4 no-multiblock \
                no-nextprotoneg no-ocsp no-ocb no-poly1305 no-psk no-rc2 no-rc4 no-seed no-siphash no-siv no-sm3 no-sm4 \
                no-srp no-srtp no-ssl3-method no-ssl-trace no-tfo no-ts no-ui-console no-whirlpool no-fips-securitychecks \
                no-tests no-docs

        make -j$(nproc)
        make install_sw
    fi

    if use_lib libarchive; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBARCHIVE_TARBALL}
        cd libarchive-${LIBARCHIVE_VERSION}
        ./configure --prefix="$INSTALL_DIR" --without-iconv --without-xml2 --without-expat
        make -j$(nproc)
        make install
    fi

    if use_lib file; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FILE_TARBALL}
        cd file-${FILE_VERSION}
        ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
        make -j$(nproc)
        make install
    fi

    if use_lib flac; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FLAC_TARBALL}
        cd flac-${FLAC_VERSION}
        ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
        make -j$(nproc)
        make install
    fi

    if use_lib benchmark; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BENCHMARK_TARBALL}
        cd benchmark-${BENCHMARK_VERSION}
        mkdir build
        cd build
        cmake .. -DBENCHMARK_ENABLE_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
        make -j$(nproc)
        make install
    fi

    if use_lib cpptrace; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${CPPTRACE_TARBALL}
        cd cpptrace-${CPPTRACE_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
        make -j$(nproc)
        make install
    fi

    if use_lib double-conversion; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${DOUBLE_CONVERSION_TARBALL}
        cd double-conversion-${DOUBLE_CONVERSION_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF
        make -j$(nproc)
        make install
    fi

    if use_lib fmt; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FMT_TARBALL}
        cd fmt-${FMT_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF
        make -j$(nproc)
        make install
    fi

    if use_lib glog; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${GLOG_TARBALL}
        cd glog-${GLOG_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF
        make -j$(nproc)
        make install
    fi

    if use_lib xxhash; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${XXHASH_TARBALL}
        cd xxHash-${XXHASH_VERSION}
        make -j$(nproc)
        make install PREFIX="$INSTALL_DIR"
    fi

    if use_lib brotli; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BROTLI_TARBALL}
        cd brotli-${BROTLI_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF
        make -j$(nproc)
        make install
    fi

    if use_lib lz4; then
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LZ4_TARBALL}
        cd lz4-${LZ4_VERSION}/build/cmake
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
        make -j$(nproc)
        make install
    fi
done

cd "$HOME"
rm -rf pkgs

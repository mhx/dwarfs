#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

GCC="${1:-gcc}"
CLANG="${2:-clang}"

echo "Using $GCC and $CLANG"

FILE_VERSION=5.46
FILE_SHA512=a6cb7325c49fd4af159b7555bdd38149e48a5097207acbe5e36deb5b7493ad6ea94d703da6e0edece5bb32959581741f4213707e5cb0528cd46d75a97a5242dc
BZIP2_VERSION=1.0.8
LIBARCHIVE_VERSION=3.7.7
FLAC_VERSION=1.5.0
# TODO: https://github.com/libunwind/libunwind/issues/702
LIBUNWIND_VERSION=1.7.2
BENCHMARK_VERSION=1.9.1
OPENSSL_VERSION=3.0.16
CPPTRACE_VERSION=0.8.2

RETRY=0
while true; do
    file_=file-${FILE_VERSION}.tar.gz
    rm -f "$file_"
    curl -o "$file_" "ftp://ftp.astron.com/pub/file/$file_"
    if echo "${FILE_SHA512}  $file_" | sha512sum -c; then
        break
    fi
    RETRY=$((RETRY+1))
    if [ $RETRY -gt 10 ]; then
        echo "Failed to download $file_"
        exit 1
    fi
done

wget https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VERSION}.tar.gz
wget https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.xz
wget https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/flac-${FLAC_VERSION}.tar.xz
wget https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/libunwind-${LIBUNWIND_VERSION}.tar.gz
wget https://github.com/google/benchmark/archive/refs/tags/v${BENCHMARK_VERSION}.tar.gz
wget https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz
wget https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v${CPPTRACE_VERSION}.tar.gz

for COMPILER in clang gcc; do
    if [[ "$COMPILER" == "clang" ]]; then
        export CC="$CLANG"
        export CXX="${CLANG/clang/clang++}"
    elif [[ "$COMPILER" == "gcc" ]]; then
        export CC="$GCC"
        export CXX="${GCC/gcc/g++}"
    else
        echo "Unknown compiler: $COMPILER"
        exit 1
    fi

    cd "$HOME/pkgs"
    mkdir $COMPILER

    INSTALL_DIR=/opt/static-libs/$COMPILER

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../libunwind-${LIBUNWIND_VERSION}.tar.gz
    cd libunwind-${LIBUNWIND_VERSION}
    ./configure --prefix="$INSTALL_DIR"
    make -j$(nproc)
    make install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../bzip2-${BZIP2_VERSION}.tar.gz
    cd bzip2-${BZIP2_VERSION}
    make -j$(nproc)
    make PREFIX="$INSTALL_DIR" install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../openssl-${OPENSSL_VERSION}.tar.gz
    cd openssl-${OPENSSL_VERSION}
    ./Configure --prefix="$INSTALL_DIR" --libdir=lib threads no-fips no-shared no-pic no-dso
    make -j$(nproc)
    make install_sw

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../libarchive-${LIBARCHIVE_VERSION}.tar.xz
    cd libarchive-${LIBARCHIVE_VERSION}
    ./configure --prefix="$INSTALL_DIR" --without-iconv --without-xml2 --without-expat
    make -j$(nproc)
    make install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../file-${FILE_VERSION}.tar.gz
    cd file-${FILE_VERSION}
    ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
    make -j$(nproc)
    make install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../flac-${FLAC_VERSION}.tar.xz
    cd flac-${FLAC_VERSION}
    ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
    make -j$(nproc)
    make install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../v${BENCHMARK_VERSION}.tar.gz
    cd benchmark-${BENCHMARK_VERSION}
    mkdir build
    cd build
    cmake .. -DBENCHMARK_ENABLE_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    make -j$(nproc)
    make install

    cd "$HOME/pkgs/$COMPILER"
    tar xf ../v${CPPTRACE_VERSION}.tar.gz
    cd cpptrace-${CPPTRACE_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    make -j$(nproc)
    make install
done

cd "$HOME"
rm -rf pkgs

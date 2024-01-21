#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

FILE_VERSION=5.45
FILE_SHA512=12611a59ff766c22a55db4b4a9f80f95a0a2e916a1d8593612c6ead32c247102a8fdc23693c6bf81bda9b604d951a62c0051e91580b1b79e190a3504c0efc20a
LIBARCHIVE_VERSION=3.7.2
FLAC_VERSION=1.4.3
LIBUNWIND_VERSION=1.7.2
# BENCHMARK_VERSION=1.8.2

RETRY=0
while true; do
    rm -f file-${FILE_VERSION}.tar.gz
    wget ftp://ftp.astron.com/pub/file/file-${FILE_VERSION}.tar.gz
    if echo "${FILE_SHA512}  file-${FILE_VERSION}.tar.gz" | sha512sum -c; then
        break
    fi
    RETRY=$((RETRY+1))
    if [ $RETRY -gt 10 ]; then
        echo "Failed to download file-${FILE_VERSION}.tar.gz"
        exit 1
    fi
done

wget https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.xz
wget https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/flac-${FLAC_VERSION}.tar.xz
wget https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/libunwind-${LIBUNWIND_VERSION}.tar.gz
# wget https://github.com/google/benchmark/archive/refs/tags/v${BENCHMARK_VERSION}.tar.gz

for COMPILER in clang gcc; do
    if [[ "$COMPILER" == "clang" ]]; then
        export CC=clang-17
        export CXX=clang++-17
    elif [[ "$COMPILER" == "gcc" ]]; then
        export CC=gcc-13
        export CXX=g++-13
    else
        echo "Unknown compiler: $COMPILER"
        exit 1
    fi

    cd "$HOME/pkgs"
    mkdir $COMPILER
    cd $COMPILER

    INSTALL_DIR=/opt/static-libs/$COMPILER

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
    tar xf ../libunwind-${LIBUNWIND_VERSION}.tar.gz
    cd libunwind-${LIBUNWIND_VERSION}
    ./configure --prefix="$INSTALL_DIR"
    make -j$(nproc)
    make install

    # cd "$HOME/pkgs"
    # tar xf v${BENCHMARK_VERSION}.tar.gz
    # cd benchmark-${BENCHMARK_VERSION}
    # mkdir build
    # cd build
    # cmake .. -DBENCHMARK_DOWNLOAD_DEPENDENCIES=1 -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    # make -j$(nproc)
    # make install
done

cd "$HOME"
rm -rf pkgs

#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

LIBARCHIVE_VERSION=3.7.1
FILE_VERSION=5.45
FLAC_VERSION=1.4.3

wget https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.xz
wget ftp://ftp.astron.com/pub/file/file-${FILE_VERSION}.tar.gz
wget https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/flac-${FLAC_VERSION}.tar.xz

tar xf libarchive-${LIBARCHIVE_VERSION}.tar.xz
cd libarchive-${LIBARCHIVE_VERSION}
./configure --prefix=/opt/static-libs --without-iconv --without-xml2 --without-expat
make -j$(nproc)
make install

cd "$HOME/pkgs"
tar xf file-${FILE_VERSION}.tar.gz
cd file-${FILE_VERSION}
./configure --prefix=/opt/static-libs --enable-static=yes --enable-shared=no
make -j$(nproc)
make install

cd "$HOME/pkgs"
tar xf flac-${FLAC_VERSION}.tar.xz
cd flac-${FLAC_VERSION}
CC=clang-15 CXX=clang++-15 ./configure --prefix=/opt/static-libs --enable-static=yes --enable-shared=no --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
make -j$(nproc)
make install

cd "$HOME"
rm -rf pkgs

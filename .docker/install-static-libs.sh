#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

wget https://github.com/libarchive/libarchive/releases/download/v3.6.2/libarchive-3.6.2.tar.xz
wget ftp://ftp.astron.com/pub/file/file-5.44.tar.gz

tar xf libarchive-3.6.2.tar.xz
cd libarchive-3.6.2
./configure --prefix=/opt/static-libs --without-iconv --without-xml2 --without-expat
make -j$(nproc)
make install

cd "$HOME/pkgs"
tar xf file-5.44.tar.gz
cd file-5.44
./configure --prefix=/opt/static-libs --enable-static=yes --enable-shared=no
make -j$(nproc)
make install

cd "$HOME"
rm -rf pkgs

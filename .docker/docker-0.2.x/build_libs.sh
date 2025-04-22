#!/bin/bash

set -e

cd /opt
mkdir build
cd build
tar xf /opt/double-conversion-3.1.5.tar.gz
cd double-conversion-3.1.5
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=OFF
make -j$(nproc)
make install

cd /opt/build
tar xf /opt/fmt-7.1.3.tar.gz
cd fmt-7.1.3
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF
make -j$(nproc)
make install

cd /opt/build
tar xf /opt/fuse-3.10.4.tar.xz
cd fuse-3.10.4
mkdir build
cd build
meson setup .. --default-library=static --prefix=/usr/local
meson configure -D utils=false -D tests=false -D examples=false
meson setup --reconfigure ..
ninja
ninja install

cd /opt/build
tar xf /opt/glog-0.5.0.tar.gz
cd glog-0.5.0
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF
make -j$(nproc)
make install

cd /opt
rm -rf build

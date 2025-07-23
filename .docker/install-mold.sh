#!/bin/bash

set -ex

# if [ -n "$TARGETPLATFORM" ]; then
#     export CC="xx-cc"
#     export CXX="xx-c++"
# fi

cd "$HOME"
mkdir pkgs
cd pkgs

MOLD_VERSION=2.40.2

wget -O mold-${MOLD_VERSION}.tar.gz https://github.com/rui314/mold/archive/refs/tags/v${MOLD_VERSION}.tar.gz
tar xf mold-${MOLD_VERSION}.tar.gz
cd mold-${MOLD_VERSION}
mkdir build
cd build

cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja
ninja install

cd "$HOME"
rm -rf pkgs

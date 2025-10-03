#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

MOLD_VERSION=2.40.4

fetch.sh https://github.com/rui314/mold/archive/refs/tags/v${MOLD_VERSION}.tar.gz mold-${MOLD_VERSION}.tar.gz
tar xf mold-${MOLD_VERSION}.tar.gz
cd mold-${MOLD_VERSION}
# fetch.sh https://github.com/rui314/mold/commit/d9b20a158097a376154cb692f607edbe7a823338.diff - | patch -p1
mkdir build
cd build

export PATH="/usr/lib/ccache/bin:$PATH"

cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja
ninja install

cd "$HOME"
rm -rf pkgs

#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

git clone --recurse-submodules https://github.com/google/bloaty
cd bloaty
mkdir build
cd build

cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja
ninja install

cd "$HOME"
rm -rf pkgs

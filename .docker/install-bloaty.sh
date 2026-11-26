#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

git clone --depth=1 --revision ccb49b0be3d5bc0dd156bcf16f1515726c395ec5 \
          --recurse-submodules --shallow-submodules https://github.com/google/bloaty
cd bloaty
mkdir build
cd build

export PATH="/usr/lib/ccache/bin:$PATH"

cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja
ccache -s
ninja install

cd "$HOME"
rm -rf pkgs

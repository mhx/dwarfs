#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

# v2.41.0 has an issue with mixing LTO and non-LTO objects: #1613
MOLD_VERSION=2.40.4

fetch.sh https://github.com/rui314/mold/archive/refs/tags/v${MOLD_VERSION}.tar.gz mold-${MOLD_VERSION}.tar.gz
tar xf mold-${MOLD_VERSION}.tar.gz
cd mold-${MOLD_VERSION}
mkdir build
cd build

export PATH="/usr/lib/ccache/bin:$PATH"

cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
ninja
ninja install

cd "$HOME"
rm -rf pkgs

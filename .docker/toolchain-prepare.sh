#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

source "$(dirname "$0")/toolchain-versions.sh"

BINUTILS_TARBALL=binutils-${BINUTILS_VERSION}.tar.xz
GCC_TARBALL=gcc-${GCC_VERSION}.tar.xz
MUSL_TARBALL=musl-${MUSL_VERSION}.tar.gz
LINUX_TARBALL=linux-${LINUX_VERSION}.tar.xz

cd "$HOME"
mkdir pkgs
cd pkgs

fetch.sh https://mirror.netcologne.de/gnu/binutils/${BINUTILS_TARBALL}
fetch.sh https://ftp.gwdg.de/pub/misc/gcc/releases/gcc-${GCC_VERSION}/${GCC_TARBALL}
fetch.sh https://www.musl-libc.org/releases/${MUSL_TARBALL}
fetch.sh https://cdn.kernel.org/pub/linux/kernel/v6.x/${LINUX_TARBALL}

tar xf ${BINUTILS_TARBALL}
tar xf ${GCC_TARBALL}
tar xf ${LINUX_TARBALL}

cd gcc-${GCC_VERSION}

for p in \
         0025-always-build-libgcc_eh.a.patch \
           ; do
    fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/master/main/gcc/$p - | patch -p1
done

#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

source "$(dirname "$0")/static-libs-versions.sh"

CARCH="$1"
LLVM_MAJOR=${LLVM_VERSION%%.*}
SYSROOT=/opt/cross/O2
TARGET="$CARCH-alpine-linux-musl"
if [[ "$CARCH" == arm ]]; then
    TARGET=arm-linux-musleabihf
fi

export PATH="$SYSROOT/usr/bin:/usr/lib/ninja-build/bin:$PATH"
RESOURCE_DIR="$SYSROOT/clang/$TARGET"
CC="$TARGET-clang"
CXX="$TARGET-clang++"

# Keep Clang's builtin headers, but use our own target-specific runtime files.
mkdir -p "$RESOURCE_DIR"
HOST_RESOURCE_DIR="$(clang-$LLVM_MAJOR -print-resource-dir)"
test -d "$HOST_RESOURCE_DIR/include"
ln -s "$HOST_RESOURCE_DIR/include" "$RESOURCE_DIR/include"

cd "$HOME/pkgs"
tar xf "$LLVM_TARBALL"
cd "llvm-project-${LLVM_VERSION}.src"

# Normalize Alpine's PowerPC architecture names for compiler-rt.
sed -i '/list(GET .* 0 COMPILER_RT_DEFAULT_TARGET_ARCH)/a\
  string(REGEX REPLACE "^ppc" "powerpc" COMPILER_RT_DEFAULT_TARGET_ARCH "${COMPILER_RT_DEFAULT_TARGET_ARCH}")' \
    compiler-rt/cmake/Modules/CompilerRTUtils.cmake

# A standalone builtins build also produces the compiler-rt CRT objects.
# No target C++ runtime or libgcc is required for the compile-only probes.
# Set the sysroot in CMAKE_*_FLAGS as well as CMAKE_SYSROOT: compiler-rt
# has a few custom compile-only probes that use CMAKE_C_FLAGS directly.
FLAGS="--sysroot=$SYSROOT -resource-dir=$RESOURCE_DIR -ffunction-sections -fdata-sections"
cmake -S compiler-rt/lib/builtins -B build-builtins -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR="$CARCH" \
    -DCMAKE_SYSROOT="$SYSROOT" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_ASM_COMPILER="$CC" \
    -DCMAKE_C_COMPILER_TARGET="$TARGET" \
    -DCMAKE_CXX_COMPILER_TARGET="$TARGET" \
    -DCMAKE_ASM_COMPILER_TARGET="$TARGET" \
    -DCMAKE_C_FLAGS="$FLAGS" \
    -DCMAKE_CXX_FLAGS="$FLAGS -nostdinc++" \
    -DCMAKE_ASM_FLAGS="$FLAGS" \
    -DCMAKE_AR=/usr/bin/llvm${LLVM_MAJOR}-ar \
    -DCMAKE_RANLIB=/usr/bin/llvm${LLVM_MAJOR}-ranlib \
    -DCMAKE_OBJDUMP="$SYSROOT/usr/bin/$TARGET-objdump" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_INSTALL_PREFIX="$RESOURCE_DIR" \
    -DCOMPILER_RT_INSTALL_PATH="$RESOURCE_DIR" \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
    -DCOMPILER_RT_BUILD_CRT=ON \
    -DCOMPILER_RT_EXCLUDE_ATOMIC_BUILTIN=OFF \
    -DCOMPILER_RT_ARM_OPTIMIZED_FP=OFF \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF
ninja -C build-builtins -j$(nproc) -l$(nproc)
ninja -C build-builtins install

# Clang silently falls back to GCC's CRT if the LLVM objects aren't found.
# Check the actual driver-selected builtins and adjacent CRT objects.
BUILTINS="$($CC --sysroot="$SYSROOT" -resource-dir="$RESOURCE_DIR" -rtlib=compiler-rt -print-libgcc-file-name)"
[[ "$BUILTINS" == "$RESOURCE_DIR/"* ]]
test -f "$BUILTINS"
test -f "$(dirname "$BUILTINS")/clang_rt.crtbegin.o"
test -f "$(dirname "$BUILTINS")/clang_rt.crtend.o"

cd "$HOME"
rm -rf pkgs

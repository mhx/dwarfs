#!/bin/bash

set -e

if [[ -z "${VCPKG_ROOT}" ]]; then
  VCPKG_ROOT=$HOME/git/vcpkg
fi

if [[ -z "${VCPKG_INSTALL_ROOT}" ]]; then
  VCPKG_INSTALL_ROOT=$HOME/git/@vcpkg-install
fi

if [[ -z "${LIPO_DIR_MERGE}" ]]; then
  LIPO_DIR_MERGE=$HOME/git/lipo-dir-merge/lipo-dir-merge.py
fi

build_mode=$1

if [[ -z $build_mode ]]; then
    build_mode="Release"
fi

if [[ $build_mode == "rebuild-vcpkg" ]]; then
  for triplet in x64-osx arm64-osx; do
    rm -rf $VCPKG_INSTALL_ROOT/tmp/$triplet
    $VCPKG_ROOT/vcpkg install --triplet=$triplet --x-install-root=$VCPKG_INSTALL_ROOT/tmp/$triplet
  done

  rm -rf $VCPKG_INSTALL_ROOT/uni-osx

  echo "merging x64-osx and arm64-osx to uni-osx..."

  python3 $LIPO_DIR_MERGE \
          $VCPKG_INSTALL_ROOT/tmp/x64-osx/x64-osx \
          $VCPKG_INSTALL_ROOT/tmp/arm64-osx/arm64-osx \
          $VCPKG_INSTALL_ROOT/uni-osx

  echo "DONE"
else
  cmake .. -GNinja \
      -DWITH_TESTS=ON -DPREFER_SYSTEM_ZSTD=ON -DUSE_JEMALLOC=OFF \
      -DCMAKE_BUILD_TYPE=$build_mode \
      -DCMAKE_PREFIX_PATH=$VCPKG_INSTALL_ROOT/uni-osx \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
fi

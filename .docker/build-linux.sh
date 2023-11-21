#!/bin/bash

set -ex

export CCACHE_DIR=/ccache

cd "$HOME"

rm -f dwarfs
ln -s /workspace dwarfs

rm -rf build
mkdir build
cd build

case "-$BUILD_TYPE-" in
  *-ninja-*)
    BUILD_TOOL=ninja
    CMAKE_ARGS="-GNinja"
    ;;
  *-make-*)
    BUILD_TOOL="make -j$(nproc)"
    CMAKE_ARGS=
    ;;
  *)
    echo "missing build tool in: $BUILD_TYPE"
    exit 1
esac

case "-$BUILD_TYPE-" in
  *-gcc-*)
    export CC=gcc-13 CXX=g++-13
    ;;
  *-oldgcc-*)
    export CC=gcc-12 CXX=g++-12
    ;;
  *-clang-*)
    export CC=clang-17 CXX=clang++-17
    ;;
  *-oldclang-*)
    export CC=clang-16 CXX=clang++-16
    ;;
  *)
    echo "missing compiler in: $BUILD_TYPE"
    exit 1
esac

case "-$BUILD_TYPE-" in
  *-debug-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    ;;
  *-release-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Release"
    ;;
  *-asan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=1"
    ;;
  *-tsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TSAN=1"
    ;;
  *-ubsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=1"
    ;;
  *)
    echo "missing build type: $BUILD_TYPE"
    exit 1
esac

if [[ "-$BUILD_TYPE-" == *-nojemalloc-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DUSE_JEMALLOC=0"
fi

if [[ "-$BUILD_TYPE-" == *-noperfmon-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_PERFMON=0"
fi

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_DO_NOT_USE=1"
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_EXTRA_PREFIX=/opt/static-libs"
else
  CMAKE_ARGS="${CMAKE_ARGS} -DWITH_BENCHMARKS=1"
fi

CMAKE_ARGS="${CMAKE_ARGS} -DWITH_TESTS=1 -DWITH_LEGACY_FUSE=1"
CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_ARTIFACTS_DIR=/artifacts"

# shellcheck disable=SC2086
cmake ../dwarfs/ $CMAKE_ARGS

$BUILD_TOOL

ctest --output-on-failure -j$(nproc)

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  $BUILD_TOOL package_source
  if [[ "$BUILD_ARCH" == "amd64" ]]; then
    $BUILD_TOOL copy_source_artifacts
  fi
fi

$BUILD_TOOL realclean

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  cd "$HOME"

  VERSION=$(git -C /workspace describe --tags --match "v*" --dirty)
  VERSION=${VERSION:1}

  rm -rf dwarfs-*
  rm -f dwarfs

  mv "build/dwarfs-${VERSION}.tar.zst" .
  rm -rf build

  tar xvf "dwarfs-${VERSION}.tar.zst"
  mv "dwarfs-${VERSION}" dwarfs

  mkdir build
  cd build

  # shellcheck disable=SC2086
  cmake ../dwarfs/ $CMAKE_ARGS

  $BUILD_TOOL

  ctest --output-on-failure -j$(nproc)

  $BUILD_TOOL strip
  $BUILD_TOOL package
  $BUILD_TOOL universal_upx

  $BUILD_TOOL copy_artifacts

  $BUILD_TOOL realclean
fi

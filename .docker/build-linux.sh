#!/bin/bash

set -ex

export CCACHE_DIR=/ccache

cd "$HOME"

git config --global --add safe.directory /workspace

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
    export COMPILER=gcc
    ;;
  *-oldgcc-*)
    export CC=gcc-12 CXX=g++-12
    ;;
  *-clang-*)
    export CC=clang-17 CXX=clang++-17
    export COMPILER=clang
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
  *-reldbg-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo"
    ;;
  *-asan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=1"
    ;;
  *-tsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TSAN=1"
    export TSAN_OPTIONS="suppressions=/workspace/tsan.supp"
    ;;
  *-ubsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=1"
    ;;
  *-coverage-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_COVERAGE=1"
    ;;
  *)
    echo "missing build type: $BUILD_TYPE"
    exit 1
esac

if [[ "-$BUILD_TYPE-" == *-O2-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_OPTIMIZE=2"
fi

if [[ "-$BUILD_TYPE-" == *-nojemalloc-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DUSE_JEMALLOC=0"
fi

if [[ "-$BUILD_TYPE-" == *-noperfmon-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_PERFMON=0 -DWITH_MAN_OPTION=0"
fi

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_DO_NOT_USE=1"
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_EXTRA_PREFIX=/opt/static-libs/$COMPILER"
else
  CMAKE_ARGS="${CMAKE_ARGS} -DWITH_BENCHMARKS=1"
fi

CMAKE_ARGS="${CMAKE_ARGS} -DWITH_TESTS=1 -DWITH_LEGACY_FUSE=1"
CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_ARTIFACTS_DIR=/artifacts"

# shellcheck disable=SC2086
cmake ../dwarfs/ $CMAKE_ARGS

$BUILD_TOOL

if [[ "-$BUILD_TYPE-" == *-coverage-* ]]; then
  export LLVM_PROFILE_FILE="$PWD/profile/%32m.profraw"
fi

ctest --output-on-failure -j$(nproc)

if [[ "-$BUILD_TYPE-" == *-coverage-* ]]; then
  unset LLVM_PROFILE_FILE
  rm -rf /tmp-runner/coverage
  mkdir -p /tmp-runner/coverage
  llvm-profdata-17 merge -sparse profile/* -o dwarfs.profdata
  for binary in mkdwarfs dwarfs dwarfsck dwarfsextract *_test; do
    llvm-cov-17 show -instr-profile=dwarfs.profdata $binary >/tmp-runner/coverage/$binary.txt
  done
fi

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  if [[ "-$BUILD_TYPE-" == *-release-* ]]; then
    # in the clang-release-static case, we also try to build from the source tarball
    if [[ "-$BUILD_TYPE-" == *-clang-* ]] && [[ "-$BUILD_TYPE-" != *-O2-* ]]; then
      $BUILD_TOOL package_source

      if [[ "$BUILD_ARCH" == "amd64" ]]; then
        $BUILD_TOOL copy_source_artifacts
      fi

      $BUILD_TOOL realclean

      cd "$HOME"

      VERSION=$(git -C /workspace describe --tags --match "v*" --dirty --abbrev=10)
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
    fi

    $BUILD_TOOL strip
  fi

  $BUILD_TOOL package
  $BUILD_TOOL universal_upx

  $BUILD_TOOL copy_artifacts

  rm -rf /tmp-runner/artifacts
  mkdir -p /tmp-runner/artifacts
  cp artifacts.env /tmp-runner
  cp dwarfs-universal-* /tmp-runner/artifacts
  cp dwarfs-*-Linux*.tar.zst /tmp-runner/artifacts
fi

$BUILD_TOOL realclean

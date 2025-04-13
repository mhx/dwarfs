#!/bin/bash

set -ex

export CCACHE_DIR=/ccache

LOCAL_REPO_PATH=/local/repos
mkdir -p "$LOCAL_REPO_PATH"
LAST_UPDATE_FILE="$LOCAL_REPO_PATH/last-update"

if [ -f "$LAST_UPDATE_FILE" ] && [ $(find "$LAST_UPDATE_FILE" -mmin -180) ]; then
    echo "Skipping git repo update because it already ran in the last three hours."
else
    echo "Running git repo update."

    for repo in "fmtlib/fmt" \
                "google/googletest" \
                "ericniebler/range-v3" \
                "greg7mdp/parallel-hashmap"; do
      reponame=$(basename "$repo")
      cd "$LOCAL_REPO_PATH"
      if [ -d "$reponame" ]; then
        cd "$reponame"
        time git fetch
      else
        time git clone "https://github.com/$repo.git"
      fi
    done

    touch "$LAST_UPDATE_FILE"
fi

if [[ "$BUILD_TYPE" != "clang-release-ninja-static" ]]; then
  export DWARFS_LOCAL_REPO_PATH="$LOCAL_REPO_PATH"
fi

if [[ "-$BUILD_TYPE-" == *-debug-* ]] && [[ "-$BUILD_TYPE-" != *-coverage-* ]] &&
   [[ "-$BUILD_TYPE-" != *-[at]san-* ]] && [[ "-$BUILD_TYPE-" != *-ubsan-* ]]; then
  export DWARFS_SKIP_SLOW_TESTS=1
fi

ARCH="$(uname -m)"

cd "$HOME"

rm -rf dwarfs dwarfs-*

if [[ "$BUILD_FROM_TARBALL" == "1" ]]; then
  tar xf "/artifacts/cache/dwarfs-source-${GITHUB_RUN_NUMBER}.tar.zst"
  ln -s dwarfs-* dwarfs
else
  git config --global --add safe.directory /workspace
  ln -s /workspace dwarfs
fi

rm -rf build
mkdir build
cd build

# Stick to clang-18, clang-19 has a regression for nilsimsa performance
if [[ "$BUILD_DIST" == "alpine" ]]; then
  GCC_VERSION=
  CLANG_VERSION=-19
elif [[ "$BUILD_DIST" == "ubuntu-2204" ]]; then
  GCC_VERSION=-12
  CLANG_VERSION=-15
else
  GCC_VERSION=-14
  CLANG_VERSION=-18
fi

case "-$BUILD_TYPE-" in
  *-ninja-*)
    BUILD_TOOL=ninja
    CMAKE_TOOL_ARGS="-GNinja"
    ;;
  *-make-*)
    BUILD_TOOL="make -j$(nproc)"
    CMAKE_TOOL_ARGS=
    ;;
  *)
    echo "missing build tool in: $BUILD_TYPE"
    exit 1
esac

CMAKE_ARGS="${CMAKE_TOOL_ARGS}"

case "-$BUILD_TYPE-" in
  *-gcc-*)
    case "-$BUILD_DIST-" in
      *-ubuntu-*)
        export CC=gcc$GCC_VERSION CXX=g++$GCC_VERSION
        ;;
    esac
    export COMPILER=gcc
    ;;
  *-oldgcc-*)
    export CC=gcc-11 CXX=g++-11
    ;;
  *-clang-*)
    case "-$BUILD_DIST-" in
      *-ubuntu-*|*-alpine-*)
        export CC=clang$CLANG_VERSION CXX=clang++$CLANG_VERSION
        ;;
      *)
        export CC=clang CXX=clang++
        ;;
    esac
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
    if [[ "-$BUILD_TYPE-" != *-minimal-* ]]; then
      CMAKE_ARGS="${CMAKE_ARGS} -DWITH_BENCHMARKS=1"
    fi
    if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
      export CFLAGS="-ffunction-sections -fdata-sections -fvisibility=hidden -fmerge-all-constants"
      export CXXFLAGS="${CFLAGS}"
      export LDFLAGS="-Wl,--gc-sections"
    fi
    ;;
  *-relsize-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=MinSizeRel"
    if [[ "-$BUILD_TYPE-" != *-minimal-* ]]; then
      CMAKE_ARGS="${CMAKE_ARGS} -DWITH_BENCHMARKS=1"
    fi
    if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
      export CFLAGS="-ffunction-sections -fdata-sections -fvisibility=hidden -fmerge-all-constants"
      export CXXFLAGS="${CFLAGS}"
      export LDFLAGS="-Wl,--gc-sections"
    fi
    export COMPILER="${COMPILER}-minsize"
    ;;
  *-reldbg-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RelWithDebInfo"
    ;;
  *)
    echo "missing build type: $BUILD_TYPE"
    exit 1
esac

case "-$BUILD_TYPE-" in
  *-static-*)
    export LDFLAGS="${LDFLAGS} -fuse-ld=mold"
    ;;
esac

case "-$BUILD_TYPE-" in
  *-lto-*)
    export CFLAGS="${CFLAGS} -flto=auto"
    export CXXFLAGS="${CXXFLAGS} -flto=auto"
    # The -L option is needed so that boost_iostreams finds the right libzstd...
    export LDFLAGS="${LDFLAGS} -flto=auto -Wl,--icf=all -L/opt/static-libs/$COMPILER/lib"
    export COMPILER="${COMPILER}-lto"
    CMAKE_ARGS="${CMAKE_ARGS} -DMONOLITHIC_TEST_BINARY=1"
    ;;
esac

case "-$BUILD_TYPE-" in
  *-minimal-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_PERFMON=0 -DWITH_MAN_OPTION=0 -DENABLE_RICEPP=0"
    CMAKE_ARGS="${CMAKE_ARGS} -DTRY_ENABLE_BROTLI=0 -DTRY_ENABLE_LZ4=0 -DTRY_ENABLE_FLAC=0"
    ;;
esac

case "-$BUILD_TYPE-" in
  *-asan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_ASAN=1"
    ;;
  *-tsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_TSAN=1"
    export TSAN_OPTIONS="suppressions=/workspace/tsan.supp"
    ;;
  *-ubsan-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_UBSAN=1"
    ;;
  *-coverage-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_COVERAGE=1 -DWITH_UNIVERSAL_BINARY=1"
    ;;
esac

if [[ "-$BUILD_TYPE-" == *-O2-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_OPTIMIZE=2"
fi

if [[ "-$BUILD_TYPE-" == *-nojemalloc-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DUSE_JEMALLOC=0"
fi

if [[ "-$BUILD_TYPE-" == *-mimalloc-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DUSE_JEMALLOC=0 -DUSE_MIMALLOC=1"
fi

if [[ "-$BUILD_TYPE-" == *-noperfmon-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_PERFMON=0 -DWITH_MAN_OPTION=0"
fi

if [[ "-$BUILD_TYPE-" == *-stacktrace-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_STACKTRACE=ON"
fi

if [[ "-$BUILD_TYPE-" == *-source-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_OPTIMIZE=0"
else
  CMAKE_ARGS="${CMAKE_ARGS} -DWITH_LEGACY_FUSE=1"
fi

if [[ "-$BUILD_TYPE-" == *-notest-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DWITH_TESTS=0"
  RUN_TESTS="echo 'skipping tests'"
else
  CMAKE_ARGS="${CMAKE_ARGS} -DWITH_TESTS=1"
  RUN_TESTS="ctest --output-on-failure -j$(nproc)"
fi

CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_ARTIFACTS_DIR=/artifacts"

SUFFIX=""

if [[ "$BUILD_DIST" == "alpine" ]]; then
  SUFFIX="-musl"

  if [[ "-$BUILD_TYPE-" == *-minimal-* ]]; then
    SUFFIX="${SUFFIX}-minimal"
  fi

  if [[ "-$BUILD_TYPE-" == *-mimalloc-* ]]; then
    SUFFIX="${SUFFIX}-mimalloc"
  fi

  if [[ "-$BUILD_TYPE-" == *-lto-* ]]; then
    SUFFIX="${SUFFIX}-lto"
  fi
fi

if [[ -n "$SUFFIX" ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DDWARFS_ARTIFACT_SUFFIX=$SUFFIX"
fi

if [[ "-$BUILD_TYPE-" == *-shared-* ]]; then
  CMAKE_ARGS="${CMAKE_ARGS} -DBUILD_SHARED_LIBS=1 -DCMAKE_POSITION_INDEPENDENT_CODE=1"
fi

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  CMAKE_ARGS_NONSTATIC="${CMAKE_ARGS}"
  export LDFLAGS="${LDFLAGS} -L/opt/static-libs/$COMPILER/lib"
  if [[ "$ARCH" == "aarch64" ]]; then
    # For some reason, this dependency of libunwind is not resolved on aarch64
    export LDFLAGS="${LDFLAGS} -lz -lgcc_eh"
  fi
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_DO_NOT_USE=1 -DWITH_UNIVERSAL_BINARY=1 -DWITH_FUSE_EXTRACT_BINARY=1"
  if [[ "$BUILD_TYPE" != *-minimal-* ]]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DWITH_PXATTR=1"
  fi
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_EXTRA_PREFIX=/opt/static-libs/$COMPILER"
fi

if [[ "$BUILD_FROM_TARBALL" == "1" ]]; then
  INSTALLDIR="$HOME/install"
  PREFIXPATH="$INSTALLDIR/usr/local"
  rm -rf "$INSTALLDIR"

  if [[ "-$BUILD_TYPE-" == *-shared-* ]]; then
    LDLIBPATH="$(readlink -m "$PREFIXPATH/lib/$(gcc -print-multi-os-directory)")"
    if [[ ":$LD_LIBRARY_PATH:" != *":$LDLIBPATH:"* ]]; then
      export "LD_LIBRARY_PATH=${LD_LIBRARY_PATH:+${LD_LIBRARY_PATH}:}$LDLIBPATH"
    fi
  fi

  case "-$BUILD_TYPE-" in
    *-full-*)
      cmake ../dwarfs/ $CMAKE_ARGS
      time $BUILD_TOOL
      $RUN_TESTS
      DESTDIR="$INSTALLDIR" $BUILD_TOOL install
      $BUILD_TOOL distclean
      ;;

    *-split-*)
      # ==== libdwarfs ====
      cmake ../dwarfs/ $CMAKE_ARGS -DWITH_LIBDWARFS=ON -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=OFF
      time $BUILD_TOOL
      $RUN_TESTS
      DESTDIR="$INSTALLDIR" $BUILD_TOOL install
      $BUILD_TOOL distclean
      rm -rf *

      # ==== example binary ====
      cmake ../dwarfs/example $CMAKE_TOOL_ARGS -DCMAKE_PREFIX_PATH="$PREFIXPATH"
      time $BUILD_TOOL
      rm -rf *

      # ==== dwarfs tools ====
      cmake ../dwarfs/ $CMAKE_ARGS -DWITH_LIBDWARFS=OFF -DWITH_TOOLS=ON -DWITH_FUSE_DRIVER=OFF -DCMAKE_PREFIX_PATH="$PREFIXPATH"
      time $BUILD_TOOL
      $RUN_TESTS
      DESTDIR="$INSTALLDIR" $BUILD_TOOL install
      $BUILD_TOOL distclean
      rm -rf *

      # ==== dwarfs fuse driver ====
      cmake ../dwarfs/ $CMAKE_ARGS -DWITH_LIBDWARFS=OFF -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=ON -DCMAKE_PREFIX_PATH="$PREFIXPATH"
      time $BUILD_TOOL
      $RUN_TESTS
      DESTDIR="$INSTALLDIR" $BUILD_TOOL install
      $BUILD_TOOL distclean
      ;;

    *)
      echo "builds from source tarball must be 'full' or 'split'"
      exit 1
      ;;
  esac
else
  # shellcheck disable=SC2086
  cmake ../dwarfs/ $CMAKE_ARGS -DWITH_EXAMPLE=1

  time $BUILD_TOOL

  $RUN_TESTS

  if [[ "-$BUILD_TYPE-" == *-coverage-* ]]; then
    rm -f /tmp-runner/dwarfs-coverage.txt
    llvm-profdata$CLANG_VERSION merge -sparse profile/* -o dwarfs.profdata
    llvm-cov$CLANG_VERSION show -instr-profile=dwarfs.profdata \
      $(for i in mkdwarfs dwarfs dwarfsck dwarfsextract *_test ricepp/ricepp_test; do echo $i; done | sed -e's/^/-object=/') \
      >/tmp-runner/dwarfs-coverage.txt
  fi

  if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
    # for release and relsize builds, strip the binaries
    if [[ "-$BUILD_TYPE-" =~ -(release|relsize)- ]]; then
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
  elif [[ "-$BUILD_TYPE-" == *-source-* ]]; then
    $BUILD_TOOL package_source
    $BUILD_TOOL copy_source_artifacts
  fi

  if [[ "-$BUILD_TYPE-" != *-[at]san-* ]] && \
     [[ "-$BUILD_TYPE-" != *-ubsan-* ]] && \
     [[ "-$BUILD_TYPE-" != *-source-* ]] && \
     [[ "-$BUILD_TYPE-" != *-static-* ]]; then
    INSTALLDIR="$HOME/install"
    rm -rf "$INSTALLDIR"
    DESTDIR="$INSTALLDIR" $BUILD_TOOL install

    $BUILD_TOOL distclean

    cmake ../dwarfs/example $CMAKE_TOOL_ARGS -DCMAKE_PREFIX_PATH="$INSTALLDIR/usr/local"
    $BUILD_TOOL
    $BUILD_TOOL clean
  else
    $BUILD_TOOL distclean
  fi
fi

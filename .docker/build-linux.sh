#!/bin/bash

set -ex

export CCACHE_DIR=/ccache

LOCAL_REPO_PATH=/local/repos
mkdir -p "$LOCAL_REPO_PATH"
LAST_UPDATE_FILE="$LOCAL_REPO_PATH/last-update"

WORKFLOW_LOG_DIR="/artifacts/workflow-logs/${GITHUB_RUN_ID}"
NINJA_LOG_FILE="${WORKFLOW_LOG_DIR}/ninja-${BUILD_ARCH},${BUILD_DIST},${BUILD_TYPE}.log"
BUILD_LOG_FILE="${WORKFLOW_LOG_DIR}/build-${BUILD_ARCH},${BUILD_DIST},${BUILD_TYPE}.log"
mkdir -p "$WORKFLOW_LOG_DIR"

log() {
    local event="$1"
    # log timestamp (with microsecond resolution) + tab + event
    echo "$(python3 - <<<'from datetime import datetime; print(datetime.now().isoformat())')	$event" >> "$BUILD_LOG_FILE"
}

if [ -f "$LAST_UPDATE_FILE" ] && [ $(find "$LAST_UPDATE_FILE" -mmin -180) ]; then
    echo "Skipping git repo update because it already ran in the last three hours."
else
    echo "Running git repo update."

    log "begin:repo-update"

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

    log "end:repo-update"

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
  log "begin:tarball-extract"
  tar xf "/artifacts/cache/dwarfs-source-${GITHUB_RUN_NUMBER}.tar.zst"
  ln -s dwarfs-* dwarfs
  log "end:tarball-extract"
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
    if [[ "$BUILD_DIST" == "alpine" ]]; then
      BUILD_TOOL=/usr/lib/ninja-build/bin/ninja
    else
      BUILD_TOOL=ninja
    fi
    CMAKE_TOOL_ARGS="-GNinja"
    SAVE_BUILD_LOG="cp -a .ninja_log $NINJA_LOG_FILE"
    ;;
  *-make-*)
    BUILD_TOOL="make -j$(nproc)"
    CMAKE_TOOL_ARGS=
    SAVE_BUILD_LOG=
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
      if [[ "-$BUILD_TYPE-" != *-lto-* ]]; then
        CMAKE_ARGS="${CMAKE_ARGS} -DWITH_ALL_BENCHMARKS=1"
      fi
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
      if [[ "-$BUILD_TYPE-" != *-lto-* ]]; then
        CMAKE_ARGS="${CMAKE_ARGS} -DWITH_ALL_BENCHMARKS=1"
      fi
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
    export LDFLAGS="${LDFLAGS} -flto=auto -Wl,--icf=all"
    export COMPILER="${COMPILER}-lto"
    ;;
esac

case "-$BUILD_TYPE-" in
  *-minimal-*)
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_PERFMON=0 -DWITH_MAN_OPTION=0 -DENABLE_RICEPP=0"
    CMAKE_ARGS="${CMAKE_ARGS} -DTRY_ENABLE_BROTLI=0 -DTRY_ENABLE_LZ4=0 -DTRY_ENABLE_FLAC=0"
    CMAKE_ARGS="${CMAKE_ARGS} -DDISABLE_FILESYSTEM_EXTRACTOR_FORMAT=1"
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

  if [[ "-$BUILD_TYPE-" == *-libressl-* ]]; then
    SUFFIX="${SUFFIX}-libressl"
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
  if [[ "-$BUILD_TYPE-" == *-relsize-* ]]; then
    _LIBSTDCXXDIR="/opt/static-libs/libstdc++-Os/lib"
    if [[ "$ARCH" == "aarch64" ]]; then
      # Similar to the issue with *not* linking against `gcc_eh` in the CMakeLists.txt,
      # if we link against the `gcc_eh` from the `-Os` build, we run into exactly the
      # same issue. So we temporarily copy the size-optimized `libgcc.a` to a directory
      # we then use for linking.
      _GCCLIBDIR="/tmp/gcclib"
      mkdir -p "$_GCCLIBDIR"
      cp -a "$_LIBSTDCXXDIR"/gcc/*/*/libgcc.a "$_GCCLIBDIR"
    else
      _GCCLIBDIR=$(ls -d1 $_LIBSTDCXXDIR/gcc/*/*)
    fi
    LDFLAGS="${LDFLAGS} -L$_GCCLIBDIR -L$_LIBSTDCXXDIR"
  fi
  export LDFLAGS="${LDFLAGS} -L/opt/static-libs/$COMPILER/lib"
  if [[ "$ARCH" == "aarch64" ]]; then
    # For some reason, this dependency of libunwind is not resolved on aarch64
    export LDFLAGS="${LDFLAGS} -lz"
  fi
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_DO_NOT_USE=1 -DWITH_UNIVERSAL_BINARY=1 -DWITH_FUSE_EXTRACT_BINARY=1"
  if [[ "$BUILD_TYPE" == *-minimal-* ]]; then
    _jemallocprefix="/opt/static-libs/$COMPILER-jemalloc-minimal"
  else
    CMAKE_ARGS="${CMAKE_ARGS} -DWITH_PXATTR=1"
    _jemallocprefix="/opt/static-libs/$COMPILER-jemalloc-full"
  fi
  if [[ "$BUILD_TYPE" == *-libressl-* ]]; then
    _sslprefix="/opt/static-libs/$COMPILER-libressl"
  else
    _sslprefix="/opt/static-libs/$COMPILER-openssl"
  fi
  CMAKE_ARGS="${CMAKE_ARGS} -DSTATIC_BUILD_EXTRA_PREFIX=/opt/static-libs/$COMPILER;$_sslprefix;$_jemallocprefix"
fi

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
    log "begin:cmake"
    cmake ../dwarfs/ $CMAKE_ARGS -DWITH_EXAMPLE=1
    log "end:cmake"
    log "begin:build"
    time $BUILD_TOOL
    log "end:build"
    $SAVE_BUILD_LOG
    log "begin:test"
    $RUN_TESTS
    log "end:test"
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

    # That's it for split builds, we are done
    exit 0
    ;;

  *)
    log "begin:cmake"
    if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
      cmake ../dwarfs/ $CMAKE_ARGS
    else
      cmake ../dwarfs/ $CMAKE_ARGS -DWITH_EXAMPLE=1
    fi
    log "end:cmake"
    log "begin:build"
    time $BUILD_TOOL
    log "end:build"
    $SAVE_BUILD_LOG
    log "begin:test"
    $RUN_TESTS
    log "end:test"
    ;;
esac

if [[ "-$BUILD_TYPE-" == *-coverage-* ]]; then
  rm -f /tmp-runner/dwarfs-coverage.txt
  llvm-profdata$CLANG_VERSION merge -sparse profile/* -o dwarfs.profdata
  llvm-cov$CLANG_VERSION show -instr-profile=dwarfs.profdata \
    $(for i in mkdwarfs dwarfs dwarfsck dwarfsextract *_test *_tests ricepp/ricepp_test; do echo $i; done | sed -e's/^/-object=/') \
    >/tmp-runner/dwarfs-coverage.txt
fi

if [[ "-$BUILD_TYPE-" == *-static-* ]]; then
  # for release and relsize builds, strip the binaries
  if [[ "-$BUILD_TYPE-" =~ -(release|relsize)- ]]; then
    $BUILD_TOOL strip
  fi

  log "begin:package"
  $BUILD_TOOL package
  log "end:package"
  log "begin:upx"
  $BUILD_TOOL universal_upx
  log "end:upx"

  log "begin:copy-artifacts"
  $BUILD_TOOL copy_artifacts
  log "end:copy-artifacts"

  rm -rf /tmp-runner/artifacts
  mkdir -p /tmp-runner/artifacts
  cp artifacts.env /tmp-runner
  cp dwarfs-universal-* /tmp-runner/artifacts
  cp dwarfs-fuse-extract-* /tmp-runner/artifacts
  cp dwarfs-*-Linux*.tar.zst /tmp-runner/artifacts
elif [[ "-$BUILD_TYPE-" == *-source-* ]]; then
  log "begin:package-source"
  $BUILD_TOOL package_source
  log "end:package-source"
  log "begin:copy-artifacts"
  $BUILD_TOOL copy_source_artifacts
  log "end:copy-artifacts"
fi

if [[ "-$BUILD_TYPE-" != *-[at]san-* ]] && \
   [[ "-$BUILD_TYPE-" != *-ubsan-* ]] && \
   [[ "-$BUILD_TYPE-" != *-source-* ]] && \
   [[ "-$BUILD_TYPE-" != *-static-* ]]; then
  DESTDIR="$INSTALLDIR" $BUILD_TOOL install
  $BUILD_TOOL distclean

  cmake ../dwarfs/example $CMAKE_TOOL_ARGS -DCMAKE_PREFIX_PATH="$INSTALLDIR/usr/local"
  $BUILD_TOOL
  $BUILD_TOOL clean
fi

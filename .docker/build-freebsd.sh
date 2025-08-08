#!/bin/sh
set -eux

export HOME=/root
export PATH=/usr/local/bin:/usr/local/sbin:/bin:/usr/bin:/sbin:/usr/sbin
export CCACHE_DIR=/ccache
export RUNNER_TEMP=/runner_temp
export RUNNER_WORKSPACE=/work
export GITHUB_RUN_NUMBER="${GITHUB_RUN_NUMBER:-0}"
export BUILD_MODE="${BUILD_MODE:-Release}"
export CMAKE_ARGS="${CMAKE_ARGS:-}"

# Source tarball path (NFS is nullfs-mounted from host)
SRC_TARBALL="/mnt/opensource/artifacts/dwarfs/cache/dwarfs-source-${GITHUB_RUN_NUMBER}.tar.zst"
test -r "${SRC_TARBALL}"

cd "$HOME"
rm -rf dwarfs dwarfs-*
tar xf "${SRC_TARBALL}"
ln -sfn dwarfs-* dwarfs

rm -rf "${RUNNER_TEMP}/build"
cmake --fresh \
  -B"${RUNNER_TEMP}/build" \
  -S"${HOME}/dwarfs" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_MODE}" \
  -DWITH_TESTS=ON \
  -DWITH_PXATTR=ON \
  ${CMAKE_ARGS}

cmake --build "${RUNNER_TEMP}/build"
ctest --test-dir "${RUNNER_TEMP}/build" --output-on-failure -j"$(sysctl -n hw.ncpu)"
cmake --build "${RUNNER_TEMP}/build" --target realclean

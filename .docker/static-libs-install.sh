#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

source "$(dirname "$0")/static-libs-versions.sh"

ARCH="$(uname -m)"

LLVM_MAJOR=${LLVM_VERSION%%.*}

PKGS="$1"
TARGET_ARCH="$2"
COMPILER="$3"

if [[ "$COMPILER" != clang* ]]; then
    echo "Unknown static library compiler: $COMPILER"
    exit 1
fi

if [[ "$PKGS" == ":none" ]]; then
    echo "No libraries to build"
    exit 0
elif [[ "$PKGS" == ":all" ]]; then
    PKGS="benchmark,boost,brotli,cpptrace,file,flac,fmt,fuse,fuse3,hhdate,jemalloc,libarchive,libdwarf,libucontext,libressl,llvm,lz4,mimalloc,nlohmann,openssl,parallel-hashmap,range-v3,utfcpp,xxhash,xz,zstd,zxc"
fi

TARGET_EMULATOR=""
if [[ "$TARGET_ARCH" != "$ARCH" ]]; then
    TARGET_EMULATOR="qemu-$TARGET_ARCH"
fi

export COMMON_CFLAGS="-ffunction-sections -fdata-sections -fmerge-all-constants"
export COMMON_CXXFLAGS="$COMMON_CFLAGS"

use_lib() {
    local lib="$1"
    case "$CARCH" in
        ppc64*|arm)
            case "$lib" in
                libdwarf|cpptrace)
                    return 1
                    ;;
            esac
            ;;
        s390x)
            case "$lib" in
                libdwarf|cpptrace|libressl)
                    return 1
                    ;;
            esac
            ;;
        loongarch64)
            case "$lib" in
                fuse|libdwarf|cpptrace)
                    return 1
                    ;;
            esac
            ;;
    esac
    if [[ ",$PKGS," == *",$lib,"* ]]; then
        return 0
    else
        return 1
    fi
}

set_build_flags() {
    if [[ $CFLAGS =~ ^[[:space:]]*$ ]]; then
        echo "unsetting CFLAGS"
        unset CFLAGS
    else
        echo "setting CFLAGS: $CFLAGS"
    fi

    if [[ $CXXFLAGS =~ ^[[:space:]]*$ ]]; then
        echo "unsetting CXXFLAGS"
        unset CXXFLAGS
    else
        echo "setting CXXFLAGS: $CXXFLAGS"
    fi

    if [[ $COMP_LDFLAGS =~ ^[[:space:]]*$ ]]; then
        echo "unsetting LDFLAGS"
        unset LDFLAGS
    else
        export LDFLAGS="$COMP_LDFLAGS"
        echo "setting LDFLAGS: $LDFLAGS"
    fi
}

opt_size() {
    export CFLAGS="$SIZE_CFLAGS"
    export CXXFLAGS="$SIZE_CXXFLAGS"
    export CPPFLAGS=""
    export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=MinSizeRel"
    export CMAKE_ARGS="-GNinja -DCMAKE_SYSROOT=$SYSROOT"
    if [[ "$CARCH" != "$ARCH" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$CARCH"
    fi
    set_build_flags
}

opt_perf() {
    export CFLAGS="$PERF_CFLAGS"
    export CXXFLAGS="$PERF_CXXFLAGS"
    export CPPFLAGS=""
    export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
    export CMAKE_ARGS="-GNinja -DCMAKE_SYSROOT=$SYSROOT"
    if [[ "$CARCH" != "$ARCH" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$CARCH"
    fi
    set_build_flags
}

echo "==========================================================="
echo "Building for target architecture: $TARGET_ARCH"
echo "==========================================================="

export CARCH="$TARGET_ARCH"

rm -f /tmp/meson-$CARCH.txt

case "$CARCH" in
    aarch64*)    OPENSSL_TARGET_ARGS="linux-aarch64" ;;
    arm*)        OPENSSL_TARGET_ARGS="linux-armv4" ;;
    mips64*)     OPENSSL_TARGET_ARGS="linux64-mips64" ;;
    ppc)         OPENSSL_TARGET_ARGS="linux-ppc" ;;
    ppc64)       OPENSSL_TARGET_ARGS="linux-ppc64" ;;
    ppc64le)     OPENSSL_TARGET_ARGS="linux-ppc64le" ;;
    i386)        OPENSSL_TARGET_ARGS="linux-elf" ;;
    s390x)       OPENSSL_TARGET_ARGS="linux64-s390x" ;;
    riscv64)     OPENSSL_TARGET_ARGS="linux64-riscv64" ;;
    loongarch64) OPENSSL_TARGET_ARGS="linux64-loongarch64" ;;
    x86_64)      OPENSSL_TARGET_ARGS="linux-x86_64" ;;
    *)           echo "Unable to determine architecture from (CARCH=$CARCH)"; exit 1 ;;
esac

BOOST_CONTEXT_ARCH="$CARCH"
case "$CARCH" in
    aarch64*)    BOOST_CONTEXT_ARCH="arm64" ;;
    ppc64*)      BOOST_CONTEXT_ARCH="ppc64" ;;
esac

case "$CARCH" in
    i386|x86_64) MAX_LG_PAGE_SIZE=12 ;;
    *)           MAX_LG_PAGE_SIZE=16 ;;
esac

export TARGET="${CARCH}-alpine-linux-musl"

case "$CARCH" in
    arm) export TARGET="arm-linux-musleabihf" ;;
esac

export TRIPLETS="--host=$TARGET --target=$TARGET --build=$ARCH-alpine-linux-musl"
export BOOST_CMAKE_ARGS="-DBOOST_CONTEXT_ARCHITECTURE=$BOOST_CONTEXT_ARCH"
export LIBUCONTEXT_MAKE_ARGS="ARCH=$CARCH"

endian="little"
case "$CARCH" in
    s390x|ppc|ppc64)
        endian="big"
        ;;
esac

TOOLCHAIN="/opt/cross/O2"
export SYSROOT="$TOOLCHAIN/sysroots/$TARGET"
export PATH="$TOOLCHAIN/usr/lib/ccache/bin:$TOOLCHAIN/usr/bin:/usr/lib/ninja-build/bin:$PATH"
export WORKROOT="$HOME/pkgs"

INSTALL_ROOT="/opt/static-libs"
INSTALL_DIR="$INSTALL_ROOT/$COMPILER/$TARGET"
INSTALL_DIR_OPENSSL="$INSTALL_ROOT/$COMPILER-openssl/$TARGET"
INSTALL_DIR_LIBRESSL="$INSTALL_ROOT/$COMPILER-libressl/$TARGET"
WORKSUBDIR="$COMPILER/$TARGET"
WORKDIR="$WORKROOT/$WORKSUBDIR"

RESOURCE_DIR="$TOOLCHAIN/clang/$TARGET"
TARGET_FLAGS="--no-default-config --config=$RESOURCE_DIR/target.cfg"
export SIZE_CFLAGS="$COMMON_CFLAGS -isystem $INSTALL_DIR/include"
export SIZE_CXXFLAGS="$COMMON_CXXFLAGS -isystem $INSTALL_DIR/include"
export PERF_CFLAGS="$COMMON_CFLAGS -isystem $INSTALL_DIR/include"
export PERF_CXXFLAGS="$COMMON_CXXFLAGS -isystem $INSTALL_DIR/include"
export COMP_LDFLAGS=""

case "$CARCH" in
    ppc64)
        # https://github.com/rui314/mold/issues/1699
        export COMP_LDFLAGS="-fuse-ld=lld"
        ;;
    *)
        export COMP_LDFLAGS="-fuse-ld=mold"
        ;;
esac

export CC="$TARGET-clang"
export CXX="$TARGET-clang++"

case "-$COMPILER-" in
    *-minsize-*)
        export PERF_CFLAGS="$PERF_CFLAGS -O2"
        export PERF_CXXFLAGS="$PERF_CXXFLAGS -O2"
        export SIZE_CFLAGS="$SIZE_CFLAGS -Os"
        export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -Os"
        ;;
    *)
        export PERF_CFLAGS="$PERF_CFLAGS -O3"
        export PERF_CXXFLAGS="$PERF_CXXFLAGS -O3"
        export SIZE_CFLAGS="$SIZE_CFLAGS -O2"
        export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -O2"
        ;;
esac

case "$COMPILER" in
    *-lto)
        COMMON_DEBUG_FLAGS="-g1 -gno-column-info -gz=zstd"
        export SIZE_CFLAGS="$SIZE_CFLAGS -flto $COMMON_DEBUG_FLAGS"
        export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -flto $COMMON_DEBUG_FLAGS"
        export PERF_CFLAGS="$PERF_CFLAGS -flto $COMMON_DEBUG_FLAGS"
        export PERF_CXXFLAGS="$PERF_CXXFLAGS -flto $COMMON_DEBUG_FLAGS"
        export COMP_LDFLAGS="$COMP_LDFLAGS -flto"
        ;;
esac

export MESON_CROSS_FILE="--cross-file=/tmp/meson-$CARCH-$COMPILER.txt"


CORES="$(nproc)"
MAKE_PARALLEL="make -j$CORES -l$CORES --output-sync=target"
NINJA_PARALLEL="ninja -j$CORES -l$CORES"

cd "$WORKROOT"
mkdir -p "$WORKSUBDIR"

if use_lib llvm; then
    opt_size
    # The C++ runtimes do not exist yet. In particular, do not let CMake's
    # initial compiler checks pull in the GCC C++ library or unwinder.
    export CFLAGS="$TARGET_FLAGS $CFLAGS"
    export CXXFLAGS="$TARGET_FLAGS $CXXFLAGS -stdlib=libc++ -nostdinc++"
    export LDFLAGS="$TARGET_FLAGS $LDFLAGS -static -rtlib=compiler-rt -unwindlib=none -nostdlib++"
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${LLVM_TARBALL}
    cd llvm-project-${LLVM_VERSION}.src
    python /usr/local/bin/trim-libcxx-locale.py
    cd runtimes
    mkdir build
    cd build
    LIBUNWIND_EXTRA_ARGS=()
    if [[ "$CARCH" == arm ]]; then
        # ARM EHABI's _Unwind_* implementations are compiled with
        # -fno-exceptions; do not let LTO infer nounwind on __cxa_throw.
        # https://github.com/llvm/llvm-project/issues/120657
        LIBUNWIND_EXTRA_ARGS+=(-DLIBUNWIND_ADDITIONAL_COMPILE_FLAGS=-fno-lto)
    fi
    cmake .. -DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" \
             -DLIBUNWIND_ENABLE_SHARED=OFF \
             -DLIBUNWIND_USE_COMPILER_RT=ON \
             -DLIBUNWIND_ENABLE_ASSERTIONS=OFF \
             "${LIBUNWIND_EXTRA_ARGS[@]}" \
             -DLIBUNWIND_INCLUDE_TESTS=OFF \
             -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
             -DLIBCXXABI_USE_COMPILER_RT=ON \
             -DLIBCXXABI_INCLUDE_TESTS=OFF \
             -DLIBCXX_USE_COMPILER_RT=ON \
             -DLIBCXX_ENABLE_SHARED=OFF \
             -DLIBCXXABI_ENABLE_SHARED=OFF \
             -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
             -DLIBCXX_HARDENING_MODE=none \
             -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
             -DLIBCXX_INCLUDE_TESTS=OFF \
             -DLIBCXX_HAS_MUSL_LIBC=ON \
             -DCMAKE_ASM_COMPILER="$CC" \
             -DCMAKE_ASM_FLAGS="$TARGET_FLAGS" \
             -DCMAKE_AR=/usr/bin/llvm${LLVM_MAJOR}-ar \
             -DCMAKE_RANLIB=/usr/bin/llvm${LLVM_MAJOR}-ranlib \
             -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install

    # Use the same driver configuration for all subsequent builds, including
    # build systems that do not consistently propagate CFLAGS/LDFLAGS.
    cat > "$INSTALL_DIR/clang.cfg" <<EOF
@$RESOURCE_DIR/target.cfg
--start-no-unused-arguments
-unwindlib=libunwind
-L$INSTALL_DIR/lib
--end-no-unused-arguments
EOF
    cat > "$INSTALL_DIR/clang++.cfg" <<EOF
@$INSTALL_DIR/clang.cfg
-stdlib=libc++
-nostdinc++
-isystem $INSTALL_DIR/include/c++/v1
EOF
fi

# A selective build must use an already installed set of LLVM runtimes.
test -f "$INSTALL_DIR/clang.cfg"
test -f "$INSTALL_DIR/clang++.cfg"
export CC="$TARGET-clang --no-default-config --config=$INSTALL_DIR/clang.cfg"
export CXX="$TARGET-clang++ --no-default-config --config=$INSTALL_DIR/clang++.cfg"
# Meson needs the compiler command and its arguments as separate array entries.
cat <<EOF > /tmp/meson-$CARCH-$COMPILER.txt
[binaries]
c = ['$TARGET-clang', '--no-default-config', '--config=$INSTALL_DIR/clang.cfg']
cpp = ['$TARGET-clang++', '--no-default-config', '--config=$INSTALL_DIR/clang++.cfg']
ld = '$TARGET-clang'
ar = '$TARGET-ar'
strip = '$TARGET-strip'

[host_machine]
system = 'linux'
cpu_family = '$CARCH'
cpu = '$CARCH'
endian = '$endian'
EOF

# Fail early, before building the expensive dependencies. Exercise C++ ABI,
# unwinding, threads, and both native and non-lock-free atomics.
opt_size
cat > "$WORKDIR/llvm-runtime-test.cpp" <<'EOF'
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>

struct value { std::uint64_t data[3]; };
std::atomic<value> wide;
std::atomic<std::uint64_t> counter{0};

int main() {
    std::thread worker([] {
        try {
            throw std::runtime_error("test");
        } catch (std::runtime_error const&) {
            wide.store(value{{1, 2, 3}});
            ++counter;
        }
    });
    worker.join();
    return counter.load() != 1 || wide.load().data[2] != 3;
}
EOF
$CXX $CXXFLAGS $LDFLAGS -static -pthread \
    -Wl,-Map,"$WORKDIR/llvm-runtime-test.map" \
    "$WORKDIR/llvm-runtime-test.cpp" -o "$WORKDIR/llvm-runtime-test"
test -s "$WORKDIR/llvm-runtime-test.map"
if grep -E 'lib(gcc(_eh)?|stdc\+\+|supc\+\+|atomic)\.a|(^|[/[:space:]])crt(begin|end)[ST]?\.o' \
        "$WORKDIR/llvm-runtime-test.map"; then
    echo "Unexpected GCC runtime in LLVM runtime test" >&2
    exit 1
fi
$TARGET_EMULATOR "$WORKDIR/llvm-runtime-test"

if use_lib libucontext; then
    opt_size
    cd "$WORKDIR"
    mkdir libucontext-${LIBUCONTEXT_VERSION}
    tar xf ${WORKROOT}/${LIBUCONTEXT_TARBALL} --strip-components=1 -C libucontext-${LIBUCONTEXT_VERSION}
    cd libucontext-${LIBUCONTEXT_VERSION}
    $MAKE_PARALLEL ${LIBUCONTEXT_MAKE_ARGS}
    make install ${LIBUCONTEXT_MAKE_ARGS} DESTDIR="$INSTALL_DIR" prefix=""
fi

if use_lib nlohmann; then
    mkdir -p "$INSTALL_DIR/include/nlohmann"
    cp "$HOME/pkgs/json.hpp" "$INSTALL_DIR/include/nlohmann/json.hpp"
fi

if use_lib utfcpp; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${UTFCPP_TARBALL}
    cd utfcpp-${UTFCPP_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib hhdate; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${DATE_TARBALL}
    cd date-${DATE_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
             -DBUILD_TZ_LIB=OFF -DENABLE_DATE_TESTING=OFF \
             ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib boost; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${BOOST_TARBALL}
    cd boost-${BOOST_VERSION}
    mkdir build
    cd build
    cmake .. -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF -DBUILD_SHARED_LIBS=OFF \
             -DBOOST_CHARCONV_QUADMATH_FOUND_EXITCODE=0 \
             -DBOOST_EXCLUDE_LIBRARIES='cobalt;contract;coroutine;fiber;graph;graph_parallel;iostreams;json;locale;log;log_setup;nowide;prg_exec_monitor;stacktrace;timer;type_erasure;unit_test_framework;url;wave;wserialization' \
             ${BOOST_CMAKE_ARGS} \
             -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib jemalloc; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${JEMALLOC_TARBALL}
    cd jemalloc-${JEMALLOC_VERSION}
    # fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/musl-exception-specification-errors.patch - | patch -p1
    # fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/pkgconf.patch - | patch -p1
    # fetch.sh https://github.com/jemalloc/jemalloc/commit/1a15fe33a48c52bfe26ea83e49f0d317a47da3ea.diff - | patch -p1
    # Keep the _Unwind_Backtrace profiling backend, but use LLVM libunwind
    # instead of libgcc. The other libunwind backend uses unw_backtrace,
    # which LLVM libunwind does not provide.
    grep -Fq 'AC_CHECK_LIB([gcc], [_Unwind_Backtrace], [JE_APPEND_VS(LIBS, -lgcc)]' configure.ac
    sed -i 's/AC_CHECK_LIB(\[gcc\], \[_Unwind_Backtrace\], \[JE_APPEND_VS(LIBS, -lgcc)\]/AC_CHECK_LIB([unwind], [_Unwind_Backtrace], [JE_APPEND_VS(LIBS, -lunwind)]/' configure.ac
    grep -Fq 'AC_CHECK_LIB([unwind], [_Unwind_Backtrace], [JE_APPEND_VS(LIBS, -lunwind)]' configure.ac
    ./autogen.sh ${TRIPLETS} --with-cxx-stdlib=libcxx
    mkdir build-minimal
    cd build-minimal
    ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-minimal/$TARGET" --localstatedir=/var \
                 --with-cxx-stdlib=libcxx \
                 --sysconfdir=/etc --with-lg-page=$MAX_LG_PAGE_SIZE --with-lg-hugepage=21 \
                 --disable-stats --disable-prof --enable-static --disable-shared --disable-log --disable-debug
    $MAKE_PARALLEL
    make install
    cd ..
    mkdir build-full
    cd build-full
    ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-full/$TARGET" --localstatedir=/var \
                 --with-cxx-stdlib=libcxx \
                 --sysconfdir=/etc --with-lg-page=$MAX_LG_PAGE_SIZE --with-lg-hugepage=21 \
                 --enable-stats --enable-prof --disable-prof-libunwind --enable-static --disable-shared --disable-log --disable-debug
    $MAKE_PARALLEL
    make install
    # Ensure profiling did not silently fall back to another backend.
    grep -E '^Libs(\.private)?:.*[[:space:]]-lunwind([[:space:]]|$)' \
        "$INSTALL_ROOT/$COMPILER-jemalloc-full/$TARGET/lib/pkgconfig/jemalloc.pc"
fi

if use_lib mimalloc; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${MIMALLOC_TARBALL}
    cd mimalloc-${MIMALLOC_VERSION}
    mkdir build
    cd build
    cmake .. -DMI_LIBC_MUSL=ON -DMI_BUILD_SHARED=OFF -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF -DMI_OPT_ARCH=OFF -DMI_INSTALL_TOPLEVEL=ON \
             -Dmi_has_libatomic:BOOL=OFF -DMI_LIBPATH_atomic:STRING= -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib fmt; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${FMT_TARBALL}
    cd fmt-${FMT_VERSION}
    fetch.sh https://github.com/fmtlib/fmt/commit/588b3a0f8f6a8bcf2a959cae882d5b2703e86737.diff - | patch -p1
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib fuse; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${LIBFUSE_TARBALL}
    cd fuse-${LIBFUSE_VERSION}
    ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" \
        --disable-shared --enable-static \
        --disable-example --enable-lib --disable-util
    $MAKE_PARALLEL
    make install
fi

if use_lib fuse3; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${LIBFUSE3_TARBALL}
    cd fuse-${LIBFUSE3_VERSION}
    mkdir build
    cd build
    meson setup .. --default-library=static --prefix="$INSTALL_DIR" $MESON_CROSS_FILE
    # meson configure ${TRIPLETS} -D utils=false -D tests=false -D examples=false
    meson configure -D utils=false -D tests=false -D examples=false
    meson setup --reconfigure ..
    $NINJA_PARALLEL
    ninja install
fi

if use_lib benchmark; then
    opt_perf
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${BENCHMARK_TARBALL}
    cd benchmark-${BENCHMARK_VERSION}
    mkdir build
    cd build
    cmake .. -DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_WERROR=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib xxhash; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${XXHASH_TARBALL}
    cd xxHash-${XXHASH_VERSION}
    $MAKE_PARALLEL
    make install PREFIX="$INSTALL_DIR"
fi

if use_lib brotli; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${BROTLI_TARBALL}
    cd brotli-${BROTLI_VERSION}
    # https://github.com/google/brotli/pull/1369
    fetch.sh https://github.com/google/brotli/commit/7f91406a2fa6e3a649cc93e9b147ee1c6e3a3543.diff - | patch -p1
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBROTLI_BUILD_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib lz4; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${LZ4_TARBALL}
    cd lz4-${LZ4_VERSION}/build/cmake
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib zxc; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${ZXC_TARBALL}
    cd zxc-${ZXC_VERSION}

    mkdir meson-build
    cd meson-build
    meson setup .. --default-library=static --prefix="$INSTALL_DIR" $MESON_CROSS_FILE
    meson configure -D build_cli=false
    meson setup --reconfigure ..
    $NINJA_PARALLEL
    ninja install
fi

if use_lib xz; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${XZ_TARBALL}
    cd xz-${XZ_VERSION}
    ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --localstatedir=/var --sysconfdir=/etc \
                --disable-rpath --disable-werror --disable-doc --disable-shared --disable-nls \
                --disable-xz --disable-xzdec --disable-lzmainfo --disable-lzmadec \
                --disable-lzma-links --disable-scripts
    $MAKE_PARALLEL
    make install
fi

if use_lib zstd; then
    # Do NOT use `opt_perf` here without benchmarking. While this has shown to
    # improve *compression* performance, it did also degrade *decompression*
    # performance.
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${ZSTD_TARBALL}
    cd zstd-${ZSTD_VERSION}

    mkdir meson-build
    cd meson-build
    meson setup ../build/meson --default-library=static --prefix="$INSTALL_DIR" $MESON_CROSS_FILE
    meson configure -D bin_programs=false \
                    -D bin_contrib=false \
                    -D zlib=disabled \
                    -D lzma=disabled \
                    -D lz4=disabled \
                    -D multi_thread=disabled
    meson setup --reconfigure ../build/meson
    $NINJA_PARALLEL
    ninja install
fi

if use_lib openssl; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${OPENSSL_TARBALL}
    cd openssl-${OPENSSL_VERSION}
    # Atomic builtins are provided by compiler-rt.
    sed -i 's/threads("-latomic")/threads("")/g' Configurations/10-main.conf
    ./Configure ${OPENSSL_TARGET_ARGS} --prefix="$INSTALL_DIR_OPENSSL" --libdir=lib \
            threads no-fips no-shared no-pic no-dso no-aria no-async no-atexit \
            no-autoload-config no-blake2 no-bf no-camellia no-cast no-chacha no-cmac no-cms no-cmp no-comp no-ct no-des \
            no-dgram no-dh no-dsa no-ec no-engine no-filenames no-idea no-ktls no-md4 no-multiblock \
            no-nextprotoneg no-ocsp no-ocb no-poly1305 no-psk no-rc2 no-rc4 no-seed no-siphash no-siv no-sm3 no-sm4 \
            no-srp no-srtp no-ssl3-method no-ssl-trace no-tfo no-ts no-ui-console no-whirlpool no-fips-securitychecks \
            no-tests no-docs

    $MAKE_PARALLEL build_libs
    make install_dev
fi

if use_lib libressl; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${LIBRESSL_TARBALL}
    cd libressl-${LIBRESSL_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR_LIBRESSL" \
             -DBUILD_SHARED_LIBS=OFF -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF \
             ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

SSL_PREFIXES=""
if [ -d "$INSTALL_DIR_OPENSSL" ]; then
    SSL_PREFIXES="$SSL_PREFIXES $INSTALL_DIR_OPENSSL"
fi
if [ -d "$INSTALL_DIR_LIBRESSL" ]; then
    SSL_PREFIXES="$SSL_PREFIXES $INSTALL_DIR_LIBRESSL"
fi
if [[ -z "$SSL_PREFIXES" ]]; then
    SSL_PREFIXES="$INSTALL_DIR"
fi

if use_lib libarchive; then
    for prefix in $SSL_PREFIXES; do
        sslsuffix=""
        if [[ "$prefix" == *libressl* ]]; then
            sslsuffix="-libressl"
        elif [[ "$prefix" == *openssl* ]]; then
            sslsuffix="-openssl"
        fi
        opt_size
        # This is safe because `opt_size` will re-initialize CFLAGS, CPPFLAGS, and LDFLAGS
        export CFLAGS="-I$prefix/include $CFLAGS"
        export CPPFLAGS="-I$prefix/include $CPPFLAGS"
        export LDFLAGS="-L$prefix/lib $LDFLAGS"
        cd "$WORKDIR"
        rm -rf libarchive-${LIBARCHIVE_VERSION}
        tar xf ${WORKROOT}/${LIBARCHIVE_TARBALL}
        cd libarchive-${LIBARCHIVE_VERSION}

        # Disable per-entry umask query. We never change this during extraction,
        # and this causes a *massive* 50% performance regression when extracting
        # to disk.
        patch -p1 <<'PATCH'
diff --git a/libarchive/archive_write_disk_posix.c b/libarchive/archive_write_disk_posix.c
index e62da665..e4ff57be 100644
--- a/libarchive/archive_write_disk_posix.c
+++ b/libarchive/archive_write_disk_posix.c
@@ -648,7 +648,7 @@ _archive_write_disk_header(struct archive *_a, struct archive_entry *entry)
 	 * user edits their umask during the extraction for some
 	 * reason.
 	 */
-	a->user_umask = __archive_get_umask();
+	// a->user_umask = __archive_get_umask();
 
 	/* Figure out what we need to do for this entry. */
 	a->todo = TODO_MODE_BASE;
PATCH

        # TODO: once DwarFS supports ACLs / xattrs, we need to update this
        ./configure ${TRIPLETS} --prefix="$prefix" \
                    --without-iconv --without-xml2 --without-expat \
                    --without-bz2lib --without-zlib \
                    --disable-shared --disable-acl --disable-xattr \
                    --disable-bsdtar --disable-bsdcat --disable-bsdcpio \
                    --disable-bsdunzip
        $MAKE_PARALLEL
        make install
    done
fi

if use_lib file; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${FILE_TARBALL}
    cd file-${FILE_VERSION}
    # libtool's -all-static is needed for the executable run via QEMU.
    FILE_LDFLAGS="${LDFLAGS/-fuse-ld=mold/-fuse-ld=lld}"
    LDFLAGS="$FILE_LDFLAGS -static" \
        ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
    $MAKE_PARALLEL FILE_COMPILE="${TARGET_EMULATOR} $WORKDIR/file-${FILE_VERSION}/src/file" \
                   LDFLAGS="$FILE_LDFLAGS -all-static"
    make install
fi

if use_lib flac; then
    opt_perf
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${FLAC_TARBALL}
    cd flac-${FLAC_VERSION}
    ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no \
                --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
    $MAKE_PARALLEL
    make install
fi

if use_lib libdwarf; then
    for prefix in $SSL_PREFIXES; do
        opt_size
        # This is safe because `opt_size` will re-initialize CFLAGS, CPPFLAGS, and LDFLAGS
        export CFLAGS="-isystem $prefix/include $CFLAGS"
        export CPPFLAGS="-isystem $prefix/include $CPPFLAGS"
        export LDFLAGS="-L$prefix/lib $LDFLAGS"
        cd "$WORKDIR"
        rm -rf libdwarf-${LIBDWARF_VERSION}
        tar xf ${WORKROOT}/${LIBDWARF_TARBALL}
        cd libdwarf-${LIBDWARF_VERSION}
        mkdir meson-build
        cd meson-build
        meson setup .. --default-library=static --prefix="$prefix" $MESON_CROSS_FILE
        # meson configure
        # meson setup --reconfigure ..
        $NINJA_PARALLEL
        ninja install
    done
fi

if use_lib cpptrace; then
    for prefix in $SSL_PREFIXES; do
        opt_size
        # This is safe because `opt_size` will re-initialize CFLAGS, CPPFLAGS, and LDFLAGS
        export CFLAGS="-isystem $prefix/include $CFLAGS"
        export CPPFLAGS="-isystem $prefix/include $CPPFLAGS"
        export LDFLAGS="-L$prefix/lib $LDFLAGS"
        cd "$WORKDIR"
        rm -rf cpptrace-${CPPTRACE_VERSION}
        tar xf ${WORKROOT}/${CPPTRACE_TARBALL}
        cd cpptrace-${CPPTRACE_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_PREFIX_PATH="$prefix;$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$prefix" \
                 -DCPPTRACE_USE_EXTERNAL_LIBDWARF=ON -DCPPTRACE_FIND_LIBDWARF_WITH_PKGCONFIG=ON \
                 -DCPPTRACE_UNWIND_WITH_UNWIND=ON -DCPPTRACE_GET_SYMBOLS_WITH_LIBDWARF=ON \
                 ${CMAKE_ARGS}
        $NINJA_PARALLEL
        ninja install
    done
fi

if use_lib range-v3; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${RANGE_V3_TARBALL}
    cd range-v3-${RANGE_V3_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
             -DBUILD_SHARED_LIBS=OFF -DRANGE_V3_EXAMPLES=OFF -DRANGE_V3_TESTS=OFF \
             -DRANGE_V3_DOCS=OFF -DRANGES_BUILD_CALENDAR_EXAMPLE=OFF \
             -DRANGES_ENABLE_WERROR=OFF -DRANGES_NATIVE=OFF -DRANGES_DEBUG_INFO=OFF \
             ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

if use_lib parallel-hashmap; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${PARALLEL_HASHMAP_TARBALL}
    cd parallel-hashmap-${PARALLEL_HASHMAP_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
             -DBUILD_SHARED_LIBS=OFF -DPHMAP_BUILD_EXAMPLES=OFF -DPHMAP_BUILD_TESTS=OFF \
             ${CMAKE_ARGS}
    $NINJA_PARALLEL
    ninja install
fi

# Catch GCC runtime dependencies exported by third-party build systems.
gcc_deps_found=0

for prefix in "$INSTALL_ROOT/$COMPILER/$TARGET" "$INSTALL_ROOT/$COMPILER-"*/"$TARGET"; do
    [[ -d "$prefix/lib" ]] || continue

    if rg -n --no-ignore --hidden -g '*.pc' -g '*.la' -g '*.cmake' -- \
            '(^|[[:space:];"=])-l(gcc(_s|_eh)?|stdc\+\+|supc\+\+|atomic)([[:space:];"]|$)|lib(gcc(_s|_eh)?|stdc\+\+|supc\+\+|atomic)\.(a|so)([.[:space:];"]|$)' \
            "$prefix/lib"; then
        gcc_deps_found=1
    else
        status=$?
        [[ "$status" == 1 ]] || exit "$status"
    fi
done

if (( gcc_deps_found )); then
    echo "Unexpected GCC runtime dependencies found" >&2
    exit 1
fi

cd "$HOME"
rm -rf pkgs
rm -f /tmp/meson-*.txt

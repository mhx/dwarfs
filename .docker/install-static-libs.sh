#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

ARCH="$(uname -m)"

GCC="gcc"
CLANG="clang-20"
PKGS="$1"
TARGET_ARCH_STR="${2:-${ARCH}}"

FILE_VERSION=5.46
FILE_SHA512=a6cb7325c49fd4af159b7555bdd38149e48a5097207acbe5e36deb5b7493ad6ea94d703da6e0edece5bb32959581741f4213707e5cb0528cd46d75a97a5242dc
BZIP2_VERSION=1.0.8

LIBARCHIVE_VERSION=3.8.1                 # 2025-06-01
FLAC_VERSION=1.5.0                       # 2025-02-11
LIBUCONTEXT_VERSION=1.3.2                # 2024-10-07
LIBUNWIND_VERSION=1.8.2                  # 2025-05-22
BENCHMARK_VERSION=1.9.4                  # 2025-05-19
BOOST_VERSION=1.88.0                     # 2025-04-11
OPENSSL_VERSION=3.5.1                    # 2025-07-01
LIBRESSL_VERSION=4.1.0                   # 2025-04-30
CPPTRACE_VERSION=1.0.4                   # 2025-07-25
DOUBLE_CONVERSION_VERSION=3.3.1          # 2025-02-14
FMT_VERSION=11.2.0                       # 2025-05-03
GLOG_VERSION=0.7.1                       # 2024-06-08
XXHASH_VERSION=0.8.3                     # 2024-12-30
LZ4_VERSION=1.10.0                       # 2024-07-22
BROTLI_VERSION=1.1.0                     # 2023-08-31
ZSTD_VERSION=1.5.7                       # 2025-02-19
LIBFUSE_VERSION=2.9.9                    # 2019-01-04
LIBFUSE3_VERSION=3.17.3                  # 2025-07-19
MIMALLOC_VERSION=2.1.7                   # 2024-05-21
JEMALLOC_VERSION=5.3.0                   # 2022-05-02
XZ_VERSION=5.8.1                         # 2025-04-03
LIBDWARF_VERSION=2.1.0                   # 2025-07-19
LIBEVENT_VERSION=2.1.12                  # 2020-07-05
NLOHMANN_VERSION=3.12.0                  # 2025-04-07
DATE_VERSION=3.0.4                       # 2025-05-28
UTFCPP_VERSION=4.0.6                     # 2024-11-03
RANGE_V3_VERSION=0.12.0                  # 2022-06-21
PARALLEL_HASHMAP_VERSION=2.0.0           # 2025-01-21

echo "Using $GCC and $CLANG"

if [[ "$PKGS" == ":none" ]]; then
    echo "No libraries to build"
    exit 0
elif [[ "$PKGS" == ":all" ]]; then
    PKGS="benchmark,boost,brotli,cpptrace,date,double-conversion,flac,fmt,fuse,fuse3,glog,jemalloc,libarchive,libdwarf,libevent,libucontext,libunwind,libressl,lz4,mimalloc,nlohmann,openssl,parallel-hashmap,range-v3,utfcpp,xxhash,xz,zstd"
fi

export COMMON_CFLAGS="-ffunction-sections -fdata-sections -fmerge-all-constants"
export COMMON_CXXFLAGS="$COMMON_CFLAGS"
export COMMON_LDFLAGS="-static-libgcc"

FILE_TARBALL="file-${FILE_VERSION}.tar.gz"
BZIP2_TARBALL="bzip2-${BZIP2_VERSION}.tar.gz"
BOOST_TARBALL="boost-${BOOST_VERSION}.tar.xz"
LIBARCHIVE_TARBALL="libarchive-${LIBARCHIVE_VERSION}.tar.xz"
FLAC_TARBALL="flac-${FLAC_VERSION}.tar.xz"
LIBUCONTEXT_TARBALL="libucontext-${LIBUCONTEXT_VERSION}.tar.gz"
LIBUNWIND_TARBALL="libunwind-${LIBUNWIND_VERSION}.tar.gz"
BENCHMARK_TARBALL="benchmark-${BENCHMARK_VERSION}.tar.gz"
OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
LIBRESSL_TARBALL="libressl-${LIBRESSL_VERSION}.tar.gz"
CPPTRACE_TARBALL="cpptrace-${CPPTRACE_VERSION}.tar.gz"
DOUBLE_CONVERSION_TARBALL="double-conversion-${DOUBLE_CONVERSION_VERSION}.tar.gz"
FMT_TARBALL="fmt-${FMT_VERSION}.tar.gz"
GLOG_TARBALL="glog-${GLOG_VERSION}.tar.gz"
XXHASH_TARBALL="xxHash-${XXHASH_VERSION}.tar.gz"
LZ4_TARBALL="lz4-${LZ4_VERSION}.tar.gz"
BROTLI_TARBALL="brotli-${BROTLI_VERSION}.tar.gz"
ZSTD_TARBALL="zstd-${ZSTD_VERSION}.tar.gz"
LIBFUSE_TARBALL="fuse-${LIBFUSE_VERSION}.tar.gz"
LIBFUSE3_TARBALL="fuse-${LIBFUSE3_VERSION}.tar.gz"
MIMALLOC_TARBALL="mimalloc-${MIMALLOC_VERSION}.tar.gz"
JEMALLOC_TARBALL="jemalloc-${JEMALLOC_VERSION}.tar.bz2"
XZ_TARBALL="xz-${XZ_VERSION}.tar.xz"
LIBDWARF_TARBALL="libdwarf-${LIBDWARF_VERSION}.tar.xz"
LIBEVENT_TARBALL="libevent-${LIBEVENT_VERSION}-stable.tar.gz"
DATE_TARBALL="date-${DATE_VERSION}.tar.gz"
UTFCPP_TARBALL="utfcpp-${UTFCPP_VERSION}.tar.gz"
RANGE_V3_TARBALL="range-v3-${RANGE_V3_VERSION}.tar.gz"
PARALLEL_HASHMAP_TARBALL="parallel-hashmap-${PARALLEL_HASHMAP_VERSION}.tar.gz"

use_lib() {
    local lib="$1"
    case "$CARCH" in
        ppc64le|arm)
            case "$lib" in
                libunwind|libdwarf|cpptrace)
                    return 1
                    ;;
            esac
            ;;
        s390x)
            case "$lib" in
                libunwind|libdwarf|cpptrace|libressl)
                    return 1
                    ;;
            esac
            ;;
        loongarch64)
            case "$lib" in
                fuse|libunwind|libdwarf|cpptrace|libressl)
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

if use_lib file; then
    RETRY=0
    while true; do
        if fetch.sh "ftp://ftp.astron.com/pub/file/$FILE_TARBALL" "$FILE_TARBALL" "$FILE_SHA512"; then
            break
        fi
        RETRY=$((RETRY+1))
        if [ $RETRY -gt 10 ]; then
            echo "Failed to download $FILE_TARBALL"
            exit 1
        fi
    done
fi

fetch_lib() {
    local lib="$1"
    local url="$2"
    local tarball="${3:-${url##*/}}"
    if use_lib "$lib"; then
        fetch.sh "$url" "$tarball"
    fi
}

fetch_lib bzip2 https://sourceware.org/pub/bzip2/${BZIP2_TARBALL}
fetch_lib boost https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz ${BOOST_TARBALL}
fetch_lib libarchive https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/${LIBARCHIVE_TARBALL}
fetch_lib flac https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/${FLAC_TARBALL}
fetch_lib libucontext https://github.com/kaniini/libucontext/archive/refs/tags/${LIBUCONTEXT_TARBALL}
fetch_lib libunwind https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/${LIBUNWIND_TARBALL}
fetch_lib benchmark https://github.com/google/benchmark/archive/refs/tags/v${BENCHMARK_VERSION}.tar.gz ${BENCHMARK_TARBALL}
fetch_lib openssl https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}
fetch_lib libressl https://github.com/libressl/portable/releases/download/v${LIBRESSL_VERSION}/${LIBRESSL_TARBALL}
fetch_lib cpptrace https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v${CPPTRACE_VERSION}.tar.gz ${CPPTRACE_TARBALL}
fetch_lib double-conversion https://github.com/google/double-conversion/archive/refs/tags/v${DOUBLE_CONVERSION_VERSION}.tar.gz ${DOUBLE_CONVERSION_TARBALL}
fetch_lib fmt https://github.com/fmtlib/fmt/archive/refs/tags/${FMT_VERSION}.tar.gz ${FMT_TARBALL}
fetch_lib glog https://github.com/google/glog/archive/refs/tags/v${GLOG_VERSION}.tar.gz ${GLOG_TARBALL}
fetch_lib xxhash https://github.com/Cyan4973/xxHash/archive/refs/tags/v${XXHASH_VERSION}.tar.gz ${XXHASH_TARBALL}
fetch_lib lz4 https://github.com/lz4/lz4/releases/download/v${LZ4_VERSION}/${LZ4_TARBALL}
fetch_lib brotli https://github.com/google/brotli/archive/refs/tags/v${BROTLI_VERSION}.tar.gz ${BROTLI_TARBALL}
fetch_lib zstd https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${ZSTD_TARBALL}
fetch_lib fuse https://github.com/libfuse/libfuse/releases/download/fuse-${LIBFUSE_VERSION}/${LIBFUSE_TARBALL}
fetch_lib fuse3 https://github.com/libfuse/libfuse/releases/download/fuse-${LIBFUSE3_VERSION}/${LIBFUSE3_TARBALL}
fetch_lib mimalloc https://github.com/microsoft/mimalloc/archive/refs/tags/v${MIMALLOC_VERSION}.tar.gz ${MIMALLOC_TARBALL}
fetch_lib jemalloc https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/${JEMALLOC_TARBALL}
fetch_lib xz https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/${XZ_TARBALL}
fetch_lib libdwarf https://github.com/davea42/libdwarf-code/releases/download/v${LIBDWARF_VERSION}/${LIBDWARF_TARBALL}
fetch_lib libevent https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}-stable/${LIBEVENT_TARBALL}
fetch_lib nlohmann https://github.com/nlohmann/json/releases/download/v${NLOHMANN_VERSION}/json.hpp
fetch_lib date https://github.com/HowardHinnant/date/archive/refs/tags/v${DATE_VERSION}.tar.gz ${DATE_TARBALL}
fetch_lib utfcpp https://github.com/nemtrif/utfcpp/archive/refs/tags/v${UTFCPP_VERSION}.tar.gz ${UTFCPP_TARBALL}
fetch_lib range-v3 https://github.com/ericniebler/range-v3/archive/refs/tags/${RANGE_V3_VERSION}.tar.gz ${RANGE_V3_TARBALL}
fetch_lib parallel-hashmap https://github.com/greg7mdp/parallel-hashmap/archive/refs/tags/v${PARALLEL_HASHMAP_VERSION}.tar.gz ${PARALLEL_HASHMAP_TARBALL}

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
    export CPPFLAGS="$TARGET_FLAGS"
    # export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=MinSizeRel"
    export CMAKE_ARGS="-GNinja"
    if [[ "$CARCH" != "$ARCH" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$CARCH"
    fi
    set_build_flags
}

opt_perf() {
    export CFLAGS="$PERF_CFLAGS"
    export CXXFLAGS="$PERF_CXXFLAGS"
    export CPPFLAGS="$TARGET_FLAGS"
    # export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
    export CMAKE_ARGS="-GNinja"
    if [[ "$CARCH" != "$ARCH" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$CARCH"
    fi
    set_build_flags
}

for target_arch in ${TARGET_ARCH_STR//,/ }; do
    echo "==========================================================="
    echo "Building for target architecture: $target_arch"
    echo "==========================================================="

    export CARCH="$target_arch"

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

    export TARGET="${CARCH}-alpine-linux-musl"

    case "$CARCH" in
        arm) export TARGET="arm-linux-musleabihf" ;;
    esac

    export TRIPLETS="--host=$TARGET --target=$TARGET --build=$ARCH-alpine-linux-musl"
    export BOOST_CMAKE_ARGS="-DBOOST_CONTEXT_ARCHITECTURE=$BOOST_CONTEXT_ARCH"
    export LIBUCONTEXT_MAKE_ARGS="ARCH=$CARCH"

    endian="little"
    case "$CARCH" in
        s390x|powerpc|powerpc64)
            endian="big"
            ;;
    esac

    export SYSROOT="/opt/cross/O2"
    export PATH="$SYSROOT/usr/lib/ccache/bin:$SYSROOT/usr/bin:$PATH"
    export WORKROOT="$HOME/pkgs"

    # COMPILERS="clang clang-lto clang-minsize-lto gcc"
    case "$CARCH" in
        # https://github.com/llvm/llvm-project/issues/150913
        ppc64le)
            COMPILERS="gcc"
            ;;
        *)
            if [[ "$ARCH" == "riscv64" ]]; then
                # This is so slow natively, stick to the basics for now
                COMPILERS="clang"
            else
                COMPILERS="clang clang-minsize-lto gcc"
            fi
            ;;
    esac

    INSTALL_ROOT="/opt/static-libs"

    for COMPILER in $COMPILERS; do
        INSTALL_DIR="$INSTALL_ROOT/$COMPILER/$TARGET"
        INSTALL_DIR_OPENSSL="$INSTALL_ROOT/$COMPILER-openssl/$TARGET"
        INSTALL_DIR_LIBRESSL="$INSTALL_ROOT/$COMPILER-libressl/$TARGET"
        WORKSUBDIR="$COMPILER/$TARGET"
        WORKDIR="$WORKROOT/$WORKSUBDIR"

        export TARGET_FLAGS="--sysroot=$SYSROOT"
        export SIZE_CFLAGS="$TARGET_FLAGS $COMMON_CFLAGS -isystem $INSTALL_DIR/include"
        export SIZE_CXXFLAGS="$TARGET_FLAGS $COMMON_CXXFLAGS -isystem $INSTALL_DIR/include"
        export PERF_CFLAGS="$TARGET_FLAGS $COMMON_CFLAGS -isystem $INSTALL_DIR/include"
        export PERF_CXXFLAGS="$TARGET_FLAGS $COMMON_CXXFLAGS -isystem $INSTALL_DIR/include"
        export COMP_LDFLAGS="$TARGET_FLAGS $COMMON_LDFLAGS -L$INSTALL_DIR/lib"

        case "$CARCH" in
            *)
                export COMP_LDFLAGS="-fuse-ld=mold $COMP_LDFLAGS"
                ;;
        esac

        case "$COMPILER" in
            clang*)
                export CC="$TARGET-clang"
                export CXX="$TARGET-clang++"
                ;;
            gcc*)
                export CC="$TARGET-gcc"
                export CXX="$TARGET-g++"
                ;;
            *)
                echo "Unknown compiler: $COMPILER"
                exit 1
                ;;
        esac

        case "-$COMPILER-" in
            *-minsize-*)
                export SIZE_CFLAGS="$SIZE_CFLAGS -Os"
                export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -Os"
                ;;
        esac

        case "$COMPILER" in
            *-lto)
                export SIZE_CFLAGS="$SIZE_CFLAGS -flto"
                export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -flto"
                export PERF_CFLAGS="$PERF_CFLAGS -flto"
                export PERF_CXXFLAGS="$PERF_CXXFLAGS -flto"
                export COMP_LDFLAGS="$COMP_LDFLAGS -flto"
                ;;
        esac

        export MESON_CROSS_FILE="--cross-file=/tmp/meson-$CARCH-$COMPILER.txt"

        cat <<EOF > /tmp/meson-$CARCH-$COMPILER.txt
[binaries]
c = '$CC'
cpp = '$CXX'
ld = '$CC'
ar = '$TARGET-ar'
strip = '$TARGET-strip'

[host_machine]
system = 'linux'
cpu_family = '$CARCH'
cpu = '$CARCH'
endian = '$endian'
EOF

        cd "$WORKROOT"
        mkdir -p "$WORKSUBDIR"

        if use_lib libucontext; then
            opt_size
            cd "$WORKDIR"
            mkdir libucontext-${LIBUCONTEXT_VERSION}
            tar xf ${WORKROOT}/${LIBUCONTEXT_TARBALL} --strip-components=1 -C libucontext-${LIBUCONTEXT_VERSION}
            cd libucontext-${LIBUCONTEXT_VERSION}
            make -j$(nproc) ${LIBUCONTEXT_MAKE_ARGS}
            make install ${LIBUCONTEXT_MAKE_ARGS} DESTDIR="$INSTALL_DIR" prefix=""
        fi

        if use_lib libunwind; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${LIBUNWIND_TARBALL}
            cd libunwind-${LIBUNWIND_VERSION}
            fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/master/main/libunwind/Remove-the-useless-endina.h-for-loongarch64.patch - | patch -p1
            fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/master/main/libunwind/fix-libunwind-pc-in.patch - | patch -p1
            LDFLAGS="$LDFLAGS -lucontext" CFLAGS="$CFLAGS -fno-stack-protector" \
                ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-cxx-exceptions --disable-tests --disable-shared
            make -j$(nproc)
            make install
        fi

        if use_lib nlohmann; then
            mkdir -p "$INSTALL_DIR/include/nlohmann"
            cp "$HOME/pkgs/json.hpp" "$INSTALL_DIR/include/nlohmann/json.hpp"
        fi

        if use_lib date; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${DATE_TARBALL}
            cd date-${DATE_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib utfcpp; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${UTFCPP_TARBALL}
            cd utfcpp-${UTFCPP_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
            ninja
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
                     -DBOOST_IOSTREAMS_ENABLE_ZLIB=OFF -DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF \
                     -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF -DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF \
                     -DBOOST_CHARCONV_QUADMATH_FOUND_EXITCODE=0 \
                     -DBOOST_EXCLUDE_LIBRARIES=stacktrace ${BOOST_CMAKE_ARGS} \
                     -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib jemalloc; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${JEMALLOC_TARBALL}
            cd jemalloc-${JEMALLOC_VERSION}
            fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/musl-exception-specification-errors.patch - | patch -p1
            fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/pkgconf.patch - | patch -p1
            ./autogen.sh ${TRIPLETS}
            mkdir build-minimal
            cd build-minimal
            ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-minimal/$TARGET" --localstatedir=/var --sysconfdir=/etc --with-lg-hugepage=21 \
                         --disable-stats --disable-prof --enable-static --disable-shared --disable-log --disable-debug
            make -j$(nproc)
            make install
            cd ..
            mkdir build-full
            cd build-full
            ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-full/$TARGET" --localstatedir=/var --sysconfdir=/etc --with-lg-hugepage=21 \
                         --enable-stats --enable-prof --enable-static --disable-shared --disable-log --disable-debug
            make -j$(nproc)
            make install
        fi

        if use_lib mimalloc; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${MIMALLOC_TARBALL}
            cd mimalloc-${MIMALLOC_VERSION}
            mkdir build
            cd build
            cmake .. -DMI_LIBC_MUSL=ON -DMI_BUILD_SHARED=OFF -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF -DMI_OPT_ARCH=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib double-conversion; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${DOUBLE_CONVERSION_TARBALL}
            cd double-conversion-${DOUBLE_CONVERSION_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib fmt; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${FMT_TARBALL}
            cd fmt-${FMT_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF ${CMAKE_ARGS}
            ninja
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
            make -j$(nproc)
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
            ninja
            ninja install
        fi

        if use_lib glog; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${GLOG_TARBALL}
            cd glog-${GLOG_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib benchmark; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${BENCHMARK_TARBALL}
            cd benchmark-${BENCHMARK_VERSION}
            mkdir build
            cd build
            cmake .. -DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_WERROR=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
            ninja
            ninja install
        fi

        if use_lib xxhash; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${XXHASH_TARBALL}
            cd xxHash-${XXHASH_VERSION}
            make -j$(nproc)
            make install PREFIX="$INSTALL_DIR"
        fi

        if use_lib bzip2; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${BZIP2_TARBALL}
            cd bzip2-${BZIP2_VERSION}
            make -j$(nproc)
            make PREFIX="$INSTALL_DIR" install
        fi

        if use_lib brotli; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${BROTLI_TARBALL}
            cd brotli-${BROTLI_VERSION}
            mkdir build
            cd build
            cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBROTLI_BUILD_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF ${CMAKE_ARGS}
            ninja
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
            ninja
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
            make -j$(nproc)
            make install
        fi

        if use_lib zstd; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${ZSTD_TARBALL}
            cd zstd-${ZSTD_VERSION}
            mkdir meson-build
            cd meson-build
            meson setup ../build/meson --default-library=static --prefix="$INSTALL_DIR" $MESON_CROSS_FILE
            meson configure -D bin_programs=false -D bin_contrib=false -D zlib=disabled -D lzma=disabled -D lz4=disabled
            meson setup --reconfigure ../build/meson
            ninja
            ninja install
        fi

        if use_lib openssl; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${OPENSSL_TARBALL}
            cd openssl-${OPENSSL_VERSION}
            ./Configure ${OPENSSL_TARGET_ARGS} --prefix="$INSTALL_DIR_OPENSSL" --libdir=lib \
                    threads no-fips no-shared no-pic no-dso no-aria no-async no-atexit \
                    no-autoload-config no-blake2 no-bf no-camellia no-cast no-chacha no-cmac no-cms no-cmp no-comp no-ct no-des \
                    no-dgram no-dh no-dsa no-ec no-engine no-filenames no-idea no-ktls no-md4 no-multiblock \
                    no-nextprotoneg no-ocsp no-ocb no-poly1305 no-psk no-rc2 no-rc4 no-seed no-siphash no-siv no-sm3 no-sm4 \
                    no-srp no-srtp no-ssl3-method no-ssl-trace no-tfo no-ts no-ui-console no-whirlpool no-fips-securitychecks \
                    no-tests no-docs

            make -j$(nproc) build_libs
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
            ninja
            ninja install
        fi

        if use_lib libevent; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${LIBEVENT_TARBALL}
            cd libevent-${LIBEVENT_VERSION}-stable
            fetch.sh https://github.com/libevent/libevent/commit/883630f76cbf512003b81de25cd96cb75c6cf0f9.diff - | patch -p1
            mkdir build
            cd build
            cmake .. -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS} \
                     -DEVENT__DISABLE_DEBUG_MODE=ON -DEVENT__DISABLE_THREAD_SUPPORT=ON -DEVENT__DISABLE_OPENSSL=ON \
                     -DEVENT__DISABLE_MBEDTLS=ON -DEVENT__DISABLE_BENCHMARK=ON -DEVENT__DISABLE_TESTS=ON \
                     -DEVENT__DISABLE_REGRESS=ON -DEVENT__DISABLE_SAMPLES=ON -DEVENT__LIBRARY_TYPE=STATIC
            ninja
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
                export LDFLAGS="-L$prefix/lib $LDFLAGS -latomic"
                cd "$WORKDIR"
                rm -rf libarchive-${LIBARCHIVE_VERSION}
                tar xf ${WORKROOT}/${LIBARCHIVE_TARBALL}
                cd libarchive-${LIBARCHIVE_VERSION}
                # TODO: once DwarFS supports ACLs / xattrs, we need to update this
                ./configure ${TRIPLETS} --prefix="$prefix" \
                            --without-iconv --without-xml2 --without-expat \
                            --without-bz2lib --without-zlib \
                            --disable-shared --disable-acl --disable-xattr \
                            --disable-bsdtar --disable-bsdcat --disable-bsdcpio \
                            --disable-bsdunzip
                make -j$(nproc)
                make install
            done
        fi

        if use_lib file; then
            opt_size
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${FILE_TARBALL}
            cd file-${FILE_VERSION}
            ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
            make -j$(nproc)
            make install
        fi

        if use_lib flac; then
            opt_perf
            cd "$WORKDIR"
            tar xf ${WORKROOT}/${FLAC_TARBALL}
            cd flac-${FLAC_VERSION}
            ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no \
                        --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
            make -j$(nproc)
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
                ninja
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
                         -DCPPTRACE_UNWIND_WITH_LIBUNWIND=ON -DCPPTRACE_GET_SYMBOLS_WITH_LIBDWARF=ON \
                         ${CMAKE_ARGS}
                ninja
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
            ninja
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
            ninja
            ninja install
        fi
    done
done

cd "$HOME"
rm -rf pkgs
rm -f /tmp/meson-*.txt

#!/bin/bash

set -ex

ARCH="$(uname -m)"

GCC="gcc"
CLANG="clang-20"
PKGS="$1"
TARGET_ARCH="$2"
COMPILER="$3"

source "$(dirname "$0")/static-libs-versions.sh"

echo "Using $GCC and $CLANG"

if [[ "$PKGS" == ":none" ]]; then
    echo "No libraries to build"
    exit 0
elif [[ "$PKGS" == ":all" ]]; then
    PKGS="benchmark,boost,brotli,cpptrace,double-conversion,flac,fmt,fuse,fuse3,glog,jemalloc,libarchive,libdwarf,libevent,libucontext,libunwind,libressl,lz4,mimalloc,nlohmann,openssl,parallel-hashmap,range-v3,utfcpp,xxhash,xz,zstd"
fi

export COMMON_CFLAGS="-ffunction-sections -fdata-sections -fmerge-all-constants"
export COMMON_CXXFLAGS="$COMMON_CFLAGS"
export COMMON_LDFLAGS="-static-libgcc"

use_lib() {
    local lib="$1"
    case "$CARCH" in
        ppc64*|arm)
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
    s390x|powerpc|powerpc64)
        endian="big"
        ;;
esac

export SYSROOT="/opt/cross/O2"
export PATH="$SYSROOT/usr/lib/ccache/bin:$SYSROOT/usr/bin:/usr/lib/ninja-build/bin:$PATH"
export WORKROOT="$HOME/pkgs"

INSTALL_ROOT="/opt/static-libs"
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
    gcc*)
        export SIZE_CFLAGS="$SIZE_CFLAGS -flto -ffat-lto-objects"
        export SIZE_CXXFLAGS="$SIZE_CXXFLAGS -flto -ffat-lto-objects"
        export PERF_CFLAGS="$PERF_CFLAGS -flto -ffat-lto-objects"
        export PERF_CXXFLAGS="$PERF_CXXFLAGS -flto -ffat-lto-objects"
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

CORES="$(nproc)"
MAKE_PARALLEL="make -j$CORES -l$CORES --output-sync=target"
NINJA_PARALLEL="ninja -j$CORES -l$CORES"

cd "$WORKROOT"
mkdir -p "$WORKSUBDIR"

if use_lib libucontext; then
    opt_size
    cd "$WORKDIR"
    mkdir libucontext-${LIBUCONTEXT_VERSION}
    tar xf ${WORKROOT}/${LIBUCONTEXT_TARBALL} --strip-components=1 -C libucontext-${LIBUCONTEXT_VERSION}
    cd libucontext-${LIBUCONTEXT_VERSION}
    $MAKE_PARALLEL ${LIBUCONTEXT_MAKE_ARGS}
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
    $MAKE_PARALLEL
    make install
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

if use_lib boost; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${BOOST_TARBALL}
    cd boost-${BOOST_VERSION}
    mkdir build
    cd build
    cmake .. -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF -DBUILD_SHARED_LIBS=OFF \
             -DBOOST_CHARCONV_QUADMATH_FOUND_EXITCODE=0 \
             -DBOOST_EXCLUDE_LIBRARIES='cobalt;contract;coroutine;fiber;graph;graph_parallel;iostreams;json;locale;log;log_setup;nowide;prg_exec_monitor;serialization;stacktrace;timer;type_erasure;unit_test_framework;url;wave;wserialization' \
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
    fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/musl-exception-specification-errors.patch - | patch -p1
    fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/pkgconf.patch - | patch -p1
    ./autogen.sh ${TRIPLETS}
    mkdir build-minimal
    cd build-minimal
    ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-minimal/$TARGET" --localstatedir=/var \
                 --sysconfdir=/etc --with-lg-page=$MAX_LG_PAGE_SIZE --with-lg-hugepage=21 \
                 --disable-stats --disable-prof --enable-static --disable-shared --disable-log --disable-debug
    $MAKE_PARALLEL
    make install
    cd ..
    mkdir build-full
    cd build-full
    ../configure ${TRIPLETS} --prefix="$INSTALL_ROOT/$COMPILER-jemalloc-full/$TARGET" --localstatedir=/var \
                 --sysconfdir=/etc --with-lg-page=$MAX_LG_PAGE_SIZE --with-lg-hugepage=21 \
                 --enable-stats --enable-prof --enable-static --disable-shared --disable-log --disable-debug
    $MAKE_PARALLEL
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
    $NINJA_PARALLEL
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
    $NINJA_PARALLEL
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

if use_lib glog; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${GLOG_TARBALL}
    cd glog-${GLOG_VERSION}
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF ${CMAKE_ARGS}
    $NINJA_PARALLEL
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

if use_lib bzip2; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${BZIP2_TARBALL}
    cd bzip2-${BZIP2_VERSION}
    $MAKE_PARALLEL
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
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${ZSTD_TARBALL}
    cd zstd-${ZSTD_VERSION}
    mkdir meson-build
    cd meson-build
    meson setup ../build/meson --default-library=static --prefix="$INSTALL_DIR" $MESON_CROSS_FILE
    meson configure -D bin_programs=false -D bin_contrib=false -D zlib=disabled -D lzma=disabled -D lz4=disabled
    meson setup --reconfigure ../build/meson
    $NINJA_PARALLEL
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
        $MAKE_PARALLEL
        make install
    done
fi

if use_lib file; then
    opt_size
    cd "$WORKDIR"
    tar xf ${WORKROOT}/${FILE_TARBALL}
    cd file-${FILE_VERSION}
    ./configure ${TRIPLETS} --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
    $MAKE_PARALLEL
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
                 -DCPPTRACE_UNWIND_WITH_LIBUNWIND=ON -DCPPTRACE_GET_SYMBOLS_WITH_LIBDWARF=ON \
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

cd "$HOME"
rm -rf pkgs
rm -f /tmp/meson-*.txt

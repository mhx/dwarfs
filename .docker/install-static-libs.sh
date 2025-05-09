#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

GCC="${1:-gcc}"
CLANG="${2:-clang}"
PKGS="${3:-:none}"

ARCH="$(uname -m)"

FILE_VERSION=5.46
FILE_SHA512=a6cb7325c49fd4af159b7555bdd38149e48a5097207acbe5e36deb5b7493ad6ea94d703da6e0edece5bb32959581741f4213707e5cb0528cd46d75a97a5242dc
BZIP2_VERSION=1.0.8

LIBARCHIVE_VERSION=3.7.9                 # 2025-03-30
FLAC_VERSION=1.5.0                       # 2025-02-11
LIBUNWIND_VERSION=1.8.1                  # 2024-02-22
BENCHMARK_VERSION=1.9.3                  # 2025-05-07
BOOST_VERSION=1.88.0                     # 2025-04-11
OPENSSL_VERSION=3.5.0                    # 2025-04-08
LIBRESSL_VERSION=4.1.0                   # 2025-04-30
CPPTRACE_VERSION=0.8.3                   # 2025-04-08
DOUBLE_CONVERSION_VERSION=3.3.1          # 2025-02-14
FMT_VERSION=11.2.0                       # 2025-05-03
GLOG_VERSION=0.7.1                       # 2024-06-08
XXHASH_VERSION=0.8.3                     # 2024-12-30
LZ4_VERSION=1.10.0                       # 2024-07-22
BROTLI_VERSION=1.1.0                     # 2023-08-31
ZSTD_VERSION=1.5.7                       # 2025-02-19
LIBFUSE_VERSION=3.17.2                   # 2025-04-23
MIMALLOC_VERSION=2.1.7                   # 2024-05-21
JEMALLOC_VERSION=5.3.0                   # 2022-05-02
XZ_VERSION=5.8.1                         # 2025-04-03

echo "Using $GCC and $CLANG"

if [[ "$PKGS" == ":ubuntu" ]]; then
    PKGS="file,bzip2,libarchive,flac,libunwind,benchmark,openssl,cpptrace"
    COMPILERS="clang gcc"
elif [[ "$PKGS" == ":alpine" ]]; then
    PKGS="benchmark,boost,brotli,cpptrace,double-conversion,flac,fmt,fuse,glog,jemalloc,libarchive,libunwind,libressl,lz4,mimalloc,openssl,xxhash,xz,zstd"
    export COMMON_CFLAGS="-ffunction-sections -fdata-sections -fmerge-all-constants"
    export COMMON_CXXFLAGS="$COMMON_CFLAGS"
    # COMPILERS="clang clang-lto clang-minsize-lto gcc"
    COMPILERS="clang clang-minsize-lto gcc"
elif [[ "$PKGS" == ":none" ]]; then
    echo "No libraries to build"
    exit 0
fi

FILE_TARBALL="file-${FILE_VERSION}.tar.gz"
BZIP2_TARBALL="bzip2-${BZIP2_VERSION}.tar.gz"
BOOST_TARBALL="boost-${BOOST_VERSION}.tar.xz"
LIBARCHIVE_TARBALL="libarchive-${LIBARCHIVE_VERSION}.tar.xz"
FLAC_TARBALL="flac-${FLAC_VERSION}.tar.xz"
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
MIMALLOC_TARBALL="mimalloc-${MIMALLOC_VERSION}.tar.gz"
JEMALLOC_TARBALL="jemalloc-${JEMALLOC_VERSION}.tar.bz2"
XZ_TARBALL="xz-${XZ_VERSION}.tar.xz"

use_lib() {
    local lib="$1"
    if [[ ",$PKGS," == *",$lib,"* ]]; then
        return 0
    else
        return 1
    fi
}

if use_lib file; then
    RETRY=0
    while true; do
        rm -f "$FILE_TARBALL"
        curl -o "$FILE_TARBALL" "ftp://ftp.astron.com/pub/file/$FILE_TARBALL"
        if echo "${FILE_SHA512}  $FILE_TARBALL" | sha512sum -c; then
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
        wget -O "$tarball" "$url"
    fi
}

fetch_lib bzip2 https://sourceware.org/pub/bzip2/${BZIP2_TARBALL}
fetch_lib boost https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz ${BOOST_TARBALL}
fetch_lib libarchive https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/${LIBARCHIVE_TARBALL}
fetch_lib flac https://github.com/xiph/flac/releases/download/${FLAC_VERSION}/${FLAC_TARBALL}
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
fetch_lib mimalloc https://github.com/microsoft/mimalloc/archive/refs/tags/v${MIMALLOC_VERSION}.tar.gz ${MIMALLOC_TARBALL}
fetch_lib jemalloc https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/${JEMALLOC_TARBALL}
fetch_lib xz https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/${XZ_TARBALL}

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

    if [[ $LDFLAGS =~ ^[[:space:]]*$ ]]; then
        echo "unsetting LDFLAGS"
        unset LDFLAGS
    else
        echo "setting LDFLAGS: $LDFLAGS"
    fi
}

opt_size() {
    export CFLAGS="$SIZE_CFLAGS"
    export CXXFLAGS="$SIZE_CXXFLAGS"
    # export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=MinSizeRel"
    export CMAKE_ARGS=
    set_build_flags
}

opt_perf() {
    export CFLAGS="$PERF_CFLAGS"
    export CXXFLAGS="$PERF_CXXFLAGS"
    # export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
    export CMAKE_ARGS=
    set_build_flags
}

for COMPILER in $COMPILERS; do
    export SIZE_CFLAGS="$COMMON_CFLAGS"
    export SIZE_CXXFLAGS="$COMMON_CXXFLAGS"
    export PERF_CFLAGS="$COMMON_CFLAGS"
    export PERF_CXXFLAGS="$COMMON_CXXFLAGS"
    export LDFLAGS="$COMMON_LDFLAGS"

    case "$COMPILER" in
        clang*)
            export CC="$CLANG"
            export CXX="${CLANG/clang/clang++}"
            ;;
        gcc*)
            export CC="$GCC"
            export CXX="${GCC/gcc/g++}"
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
            export LDFLAGS="$LDFLAGS -flto"
            ;;
    esac

    cd "$HOME/pkgs"
    mkdir $COMPILER

    INSTALL_DIR=/opt/static-libs/$COMPILER

    if use_lib libunwind; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBUNWIND_TARBALL}
        cd libunwind-${LIBUNWIND_VERSION}
        curl https://gitlab.alpinelinux.org/alpine/aports/-/raw/1f17e0b0fb8eae34ffbe82649edffcd476f5d960/main/libunwind/fix-libunwind-pc-in.patch | patch -p1
        # This fixes https://github.com/libunwind/libunwind/issues/702
        curl https://github.com/libunwind/libunwind/commit/a69d0f14c9e6c46e82ba6e02fcdedb2eb63b7f7f.diff | patch -p1
        LDFLAGS="$LDFLAGS -lucontext" CFLAGS="$CFLAGS -fno-stack-protector" ./configure \
            --prefix="$INSTALL_DIR" --enable-cxx-exceptions --disable-tests
        make -j$(nproc)
        make install
    fi

    if use_lib boost; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BOOST_TARBALL}
        cd boost-${BOOST_VERSION}
        mkdir build
        cd build
        cmake .. -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF -DBUILD_SHARED_LIBS=OFF \
                 -DBOOST_IOSTREAMS_ENABLE_ZLIB=OFF -DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF \
                 -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF -DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF \
                 -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib jemalloc; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${JEMALLOC_TARBALL}
        cd jemalloc-${JEMALLOC_VERSION}
        curl https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/musl-exception-specification-errors.patch | patch -p1
        curl https://gitlab.alpinelinux.org/alpine/aports/-/raw/abc0b4170e42e2a7d835e4490ecbae49e6f3d137/main/jemalloc/pkgconf.patch | patch -p1
        ./autogen.sh
        ./configure --prefix="$INSTALL_DIR" --localstatedir=/var --sysconfdir=/etc --with-lg-hugepage=21 --disable-stats --disable-prof --enable-static --disable-shared --disable-log --disable-debug
        make -j$(nproc)
        make install
    fi

    if use_lib mimalloc; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${MIMALLOC_TARBALL}
        cd mimalloc-${MIMALLOC_VERSION}
        mkdir build
        cd build
        cmake .. -DMI_LIBC_MUSL=ON -DMI_BUILD_SHARED=OFF -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF -DMI_OPT_ARCH=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib double-conversion; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${DOUBLE_CONVERSION_TARBALL}
        cd double-conversion-${DOUBLE_CONVERSION_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib fmt; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FMT_TARBALL}
        cd fmt-${FMT_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib fuse; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBFUSE_TARBALL}
        cd fuse-${LIBFUSE_VERSION}
        mkdir build
        cd build
        meson setup .. --default-library=static --prefix="$INSTALL_DIR"
        meson configure -D utils=false -D tests=false -D examples=false
        meson setup --reconfigure ..
        ninja
        ninja install
    fi

    if use_lib glog; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${GLOG_TARBALL}
        cd glog-${GLOG_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib benchmark; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BENCHMARK_TARBALL}
        cd benchmark-${BENCHMARK_VERSION}
        mkdir build
        cd build
        cmake .. -DBENCHMARK_ENABLE_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib xxhash; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${XXHASH_TARBALL}
        cd xxHash-${XXHASH_VERSION}
        make -j$(nproc)
        make install PREFIX="$INSTALL_DIR"
    fi

    if use_lib bzip2; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BZIP2_TARBALL}
        cd bzip2-${BZIP2_VERSION}
        make -j$(nproc)
        make PREFIX="$INSTALL_DIR" install
    fi

    if use_lib brotli; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${BROTLI_TARBALL}
        cd brotli-${BROTLI_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib lz4; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LZ4_TARBALL}
        cd lz4-${LZ4_VERSION}/build/cmake
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi

    if use_lib xz; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${XZ_TARBALL}
        cd xz-${XZ_VERSION}
        ./configure --prefix="$INSTALL_DIR" --localstatedir=/var --sysconfdir=/etc --disable-rpath --disable-werror --disable-doc --disable-shared --disable-nls
        make -j$(nproc)
        make install
    fi

    if use_lib zstd; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${ZSTD_TARBALL}
        cd zstd-${ZSTD_VERSION}
        mkdir meson-build
        cd meson-build
        meson setup ../build/meson --default-library=static --prefix="$INSTALL_DIR"
        meson configure -D zlib=disabled -D lzma=disabled -D lz4=disabled
        meson setup --reconfigure ../build/meson
        ninja
        ninja install
    fi

    if use_lib openssl; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${OPENSSL_TARBALL}
        cd openssl-${OPENSSL_VERSION}
        ./Configure --prefix="$INSTALL_DIR-openssl" --libdir=lib threads no-fips no-shared no-pic no-dso no-aria no-async no-atexit \
                no-autoload-config no-blake2 no-bf no-camellia no-cast no-chacha no-cmac no-cms no-cmp no-comp no-ct no-des \
                no-dgram no-dh no-dsa no-ec no-engine no-filenames no-idea no-ktls no-md4 no-multiblock \
                no-nextprotoneg no-ocsp no-ocb no-poly1305 no-psk no-rc2 no-rc4 no-seed no-siphash no-siv no-sm3 no-sm4 \
                no-srp no-srtp no-ssl3-method no-ssl-trace no-tfo no-ts no-ui-console no-whirlpool no-fips-securitychecks \
                no-tests no-docs

        make -j$(nproc)
        make install_sw
    fi

    if use_lib libressl; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBRESSL_TARBALL}
        cd libressl-${LIBRESSL_VERSION}
        ./configure --prefix="$INSTALL_DIR-libressl" --enable-static --disable-shared --disable-tests
        make -j$(nproc)
        make install
    fi

    if use_lib libarchive; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${LIBARCHIVE_TARBALL}
        cd libarchive-${LIBARCHIVE_VERSION}
        # TODO: once DwarFS supports ACLs / xattrs, we need to update this
        ./configure --prefix="$INSTALL_DIR" --without-iconv --without-xml2 --without-expat --without-openssl \
                                            --without-bz2lib --without-zlib --disable-acl --disable-xattr
        make -j$(nproc)
        make install
    fi

    if use_lib file; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FILE_TARBALL}
        cd file-${FILE_VERSION}
        ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no
        make -j$(nproc)
        make install
    fi

    if use_lib flac; then
        opt_perf
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${FLAC_TARBALL}
        cd flac-${FLAC_VERSION}
        ./configure --prefix="$INSTALL_DIR" --enable-static=yes --enable-shared=no --disable-doxygen-docs --disable-ogg --disable-programs --disable-examples
        make -j$(nproc)
        make install
    fi

    if use_lib cpptrace; then
        opt_size
        cd "$HOME/pkgs/$COMPILER"
        tar xf ../${CPPTRACE_TARBALL}
        cd cpptrace-${CPPTRACE_VERSION}
        mkdir build
        cd build
        cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ${CMAKE_ARGS}
        make -j$(nproc)
        make install
    fi
done

cd "$HOME"
rm -rf pkgs

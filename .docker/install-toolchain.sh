#!/bin/bash

set -ex

TASK="$1"
OPT="${2:-s}"

if [ -z "$TARGETPLATFORM" ]; then
    exit 0
fi

BINUTILS_VERSION=2.44
GCC_VERSION=14.2.0
MUSL_VERSION=1.2.5
LINUX_VERSION=6.15.7

BINUTILS_TARBALL=binutils-${BINUTILS_VERSION}.tar.xz
GCC_TARBALL=gcc-${GCC_VERSION}.tar.xz
MUSL_TARBALL=musl-${MUSL_VERSION}.tar.gz
LINUX_TARBALL=linux-${LINUX_VERSION}.tar.xz

ARCH="$(uname -m)"

ORIGPATH="$PATH"
TARGET="$(xx-info triple)"

CARCH="$(xx-info march)"
case "$CARCH" in
    aarch64*) CARCH="arm64" ;;
    arm*) CARCH="arm" ;;
    mips*) CARCH="mips" ;;
    s390*) CARCH="s390" ;;
    ppc*) CARCH="powerpc" ;;
    riscv*) CARCH="riscv" ;;
    loongarch*) CARCH="loongarch" ;;
esac

CFLAGS="-O${OPT} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
CXXFLAGS="-O${OPT} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
LDFLAGS="-Wl,--gc-sections"

SYSROOT="/opt/static-libs/libstdc++-O${OPT}/${TARGET}"
PREFIX="$SYSROOT/usr"
PATH="$PREFIX/bin:$ORIGPATH"

case "$TASK" in
    prepare)
        cd "$HOME"
        mkdir pkgs
        cd pkgs

        wget https://mirror.netcologne.de/gnu/binutils/${BINUTILS_TARBALL}
        wget https://ftp.gwdg.de/pub/misc/gcc/releases/gcc-${GCC_VERSION}/${GCC_TARBALL}
        wget https://www.musl-libc.org/releases/${MUSL_TARBALL}
        wget https://cdn.kernel.org/pub/linux/kernel/v6.x/${LINUX_TARBALL}

        tar xf ${BINUTILS_TARBALL}
        tar xf ${GCC_TARBALL}
        tar xf ${LINUX_TARBALL}

        cd gcc-${GCC_VERSION}

        for p in 0001-posix_memalign.patch \
                 0002-gcc-poison-system-directories.patch \
                 0003-specs-turn-on-Wl-z-now-by-default.patch \
                 0004-Turn-on-D_FORTIFY_SOURCE-2-by-default-for-C-C-ObjC-O.patch \
                 0005-On-linux-targets-pass-as-needed-by-default-to-the-li.patch \
                 0006-Enable-Wformat-and-Wformat-security-by-default.patch \
                 0007-Enable-Wtrampolines-by-default.patch \
                 0008-Disable-ssp-on-nostdlib-nodefaultlibs-and-ffreestand.patch \
                 0009-Ensure-that-msgfmt-doesn-t-encounter-problems-during.patch \
                 0010-Don-t-declare-asprintf-if-defined-as-a-macro.patch \
                 0011-libiberty-copy-PIC-objects-during-build-process.patch \
                 0012-libgcc_s.patch \
                 0013-nopie.patch \
                 0014-ada-fix-shared-linking.patch \
                 0015-build-fix-CXXFLAGS_FOR_BUILD-passing.patch \
                 0016-add-fortify-headers-paths.patch \
                 0017-Alpine-musl-package-provides-libssp_nonshared.a.-We-.patch \
                 0018-DP-Use-push-state-pop-state-for-gold-as-well-when-li.patch \
                 0019-aarch64-disable-multilib-support.patch \
                 0020-s390x-disable-multilib-support.patch \
                 0021-ppc64-le-disable-multilib-support.patch \
                 0022-x86_64-disable-multilib-support.patch \
                 0023-riscv-disable-multilib-support.patch \
                 0024-always-build-libgcc_eh.a.patch \
                 0025-ada-libgnarl-compatibility-for-musl.patch \
                 0026-ada-musl-support-fixes.patch \
                 0027-configure-Add-enable-autolink-libatomic-use-in-LINK_.patch \
                 0028-configure-fix-detection-of-atomic-builtins-in-libato.patch \
                 0029-libstdc-do-not-throw-exceptions-for-non-C-locales-on.patch \
                 0030-gdc-unconditionally-link-libgphobos-against-libucont.patch \
                 0031-druntime-link-against-libucontext-on-all-platforms.patch \
                 0032-libgnat-time_t-is-always-64-bit-on-musl-libc.patch \
                 0033-libphobos-do-not-use-LFS64-symbols.patch \
                 0034-libgo-fix-lfs64-use.patch \
                 0035-loongarch-disable-multilib-support.patch \
                 0036-libphobos-add-riscv64-and-loongarch64-support.patch \
                 fix-arm64.patch \
                 ppc64le-quadmath.patch \
                 riscv64-improve-build-time.patch; do
            curl https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.21-stable/main/gcc/$p | patch -p1
        done
        curl https://gcc.gnu.org/pipermail/gcc-patches/attachments/20250220/c6211b02/attachment.bin | patch -p1
        ;;

    build-stage1)
        cd "${HOME}"/pkgs

        mkdir binutils-${BINUTILS_VERSION}-build-O${OPT}
        cd binutils-${BINUTILS_VERSION}-build-O${OPT}

        "$HOME"/pkgs/binutils-${BINUTILS_VERSION}/configure \
            --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT --disable-nls --disable-werror
        make -j"$(nproc)"
        make install

        cd "$HOME"/pkgs/linux-${LINUX_VERSION}
        make ARCH="$CARCH" INSTALL_HDR_PATH=$PREFIX headers_install

        cd "$HOME"/pkgs
        mkdir gcc-${GCC_VERSION}-build-O${OPT}-stage1
        cd gcc-${GCC_VERSION}-build-O${OPT}-stage1
        "$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
            --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT --with-newlib --without-headers \
            --disable-nls --disable-shared --disable-multilib --disable-decimal-float --disable-threads \
            --disable-libatomic --disable-libgomp --disable-libquadmath --disable-libssp --disable-libvtv \
            --disable-libstdcxx --enable-languages=c,c++
        make -j"$(nproc)" all-gcc
        make -j"$(nproc)" all-target-libgcc
        make install-gcc
        make install-target-libgcc

        cd "$HOME"/pkgs
        rm -rf musl-${MUSL_VERSION}
        tar xf ${MUSL_TARBALL}
        cd musl-${MUSL_VERSION}
        ./configure --prefix=$PREFIX --target=$TARGET CC=$TARGET-gcc
        make install-headers
        make -j"$(nproc)"
        make install
        ;;

    build-final)
        cd "$HOME"/pkgs
        mkdir gcc-${GCC_VERSION}-build-O${OPT}-final
        cd gcc-${GCC_VERSION}-build-O${OPT}-final

        case "$ARCH" in
            aarch64)
                _arch_config="--with-arch=armv8-a --with-abi=lp64"
                ;;
        esac

        "$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
            --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT \
            --libdir=$PREFIX/lib --includedir=$PREFIX/include \
            --disable-shared --enable-tls --disable-libstdcxx-pch --disable-multilib --disable-nls --disable-werror --disable-symvers \
            --enable-threads --enable-__cxa_atexit --enable-languages=c,c++ --enable-link-serialization=2 --enable-linker-build-id \
            --enable-libssp --disable-libsanitizer --with-system-zlib --enable-checking=release --disable-cet --disable-fixed-point \
            --enable-libstdcxx-time=yes --enable-default-pie --enable-default-ssp --with-linker-hash-style=gnu ${_arch_config}
        make -j"$(nproc)"
        make install
        ;;

    cleanup)
        cd "$HOME"
        rm -rf pkgs
        ;;
esac



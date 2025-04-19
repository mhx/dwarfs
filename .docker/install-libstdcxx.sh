#!/bin/bash

set -ex

cd "$HOME"
mkdir pkgs
cd pkgs

GCC_VERSION=14.2.0

ARCH="$(uname -m)"

wget https://ftp.gwdg.de/pub/misc/gcc/releases/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz
tar xf gcc-${GCC_VERSION}.tar.xz
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

# for opt in s 2; do
for opt in s; do
    cd "${HOME}"/pkgs
    mkdir gcc-${GCC_VERSION}-build-O${opt}
    cd gcc-${GCC_VERSION}-build-O${opt}

    export CFLAGS="-O${opt} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
    export CXXFLAGS="-O${opt} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
    export LDFLAGS="-Wl,--gc-sections"
    INSTALLDIR=/opt/static-libs/libstdc++-O${opt}

    case "$ARCH" in
        aarch64)
            _arch_config="--with-arch=armv8-a --with-abi=lp64"
            ;;
    esac

    "$HOME"/pkgs/gcc-${GCC_VERSION}/configure --prefix=${INSTALLDIR} --libdir=${INSTALLDIR}/lib \
        --disable-shared --enable-tls --disable-libstdcxx-pch --disable-multilib --disable-nls --disable-werror --disable-symvers \
        --enable-threads --enable-__cxa_atexit --enable-languages=c,c++ --enable-link-serialization=2 --enable-linker-build-id \
        --disable-libssp --disable-libsanitizer --with-system-zlib --enable-checking=release --disable-cet --disable-fixed-point \
        --enable-default-pie --enable-default-ssp --with-linker-hash-style=gnu ${_arch_config}
    make -j"$(nproc)"
    make install
done

cd "$HOME"
rm -rf pkgs

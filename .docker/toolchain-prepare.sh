#!/bin/bash

set -ex

source "$(dirname "$0")/toolchain-versions.sh"

BINUTILS_TARBALL=binutils-${BINUTILS_VERSION}.tar.xz
GCC_TARBALL=gcc-${GCC_VERSION}.tar.xz
MUSL_TARBALL=musl-${MUSL_VERSION}.tar.gz
LINUX_TARBALL=linux-${LINUX_VERSION}.tar.xz

cd "$HOME"
mkdir pkgs
cd pkgs

fetch.sh https://mirror.netcologne.de/gnu/binutils/${BINUTILS_TARBALL}
fetch.sh https://ftp.gwdg.de/pub/misc/gcc/releases/gcc-${GCC_VERSION}/${GCC_TARBALL}
fetch.sh https://www.musl-libc.org/releases/${MUSL_TARBALL}
fetch.sh https://cdn.kernel.org/pub/linux/kernel/v6.x/${LINUX_TARBALL}

tar xf ${BINUTILS_TARBALL}
tar xf ${GCC_TARBALL}
tar xf ${LINUX_TARBALL}

cd gcc-${GCC_VERSION}

for p in 0001-posix_memalign.patch \
         0002-gcc-poison-system-directories.patch \
         0003-specs-turn-on-Wl-z-now-by-default.patch \
         0005-On-linux-targets-pass-as-needed-by-default-to-the-li.patch \
         0006-Enable-Wformat-and-Wformat-security-by-default.patch \
         0007-Enable-Wtrampolines-by-default.patch \
         0008-gcc-disable-SSP-on-ffreestanding-nostdlib-and-nodefa.patch \
         0009-gcc-params-set-default-ssp-buffer-size-to-4.patch \
         0010-Ensure-that-msgfmt-doesn-t-encounter-problems-during.patch \
         0011-Don-t-declare-asprintf-if-defined-as-a-macro.patch \
         0012-libiberty-copy-PIC-objects-during-build-process.patch \
         0013-libgcc_s.patch \
         0014-nopie.patch \
         0015-ada-fix-shared-linking.patch \
         0016-build-fix-CXXFLAGS_FOR_BUILD-passing.patch \
         0017-add-fortify-headers-paths.patch \
         0018-Alpine-musl-package-provides-libssp_nonshared.a.-We-.patch \
         0019-DP-Use-push-state-pop-state-for-gold-as-well-when-li.patch \
         0020-aarch64-disable-multilib-support.patch \
         0021-s390x-disable-multilib-support.patch \
         0022-ppc64-le-disable-multilib-support.patch \
         0023-x86_64-disable-multilib-support.patch \
         0024-riscv-disable-multilib-support.patch \
         0025-always-build-libgcc_eh.a.patch \
         0026-ada-libgnarl-remove-use-of-glibc-specific-pthread_rw.patch \
         0027-ada-libgnarl-use-posix_openpt-instead-of-glibc-speci.patch \
         0028-ada-libgnarl-adaint-fix-sched.h-inclusion-for-musl.patch \
         0029-configure-Add-enable-autolink-libatomic-use-in-LINK_.patch \
         0030-configure-fix-detection-of-atomic-builtins-in-libato.patch \
         0031-libstdc-do-not-throw-exceptions-for-non-C-locales-on.patch \
         0032-gdc-unconditionally-link-libgphobos-against-libucont.patch \
         0033-druntime-link-against-libucontext-on-all-platforms.patch \
         0034-libgnat-time_t-is-always-64-bit-on-musl-libc.patch \
         0035-libphobos-do-not-use-LFS64-symbols.patch \
         0036-libgo-fix-lfs64-use.patch \
         0037-loongarch-disable-multilib-support.patch \
         0038-static-PIE-ensure-static-reaches-the-linker.patch \
         0039-except-Don-t-use-the-cached-value-of-the-gcc_except_.patch \
         0040-ada-libgnat-use-stub-symbolic-module-name-functions.patch \
         0041-ada-libgnat-recognize-linux-musleabi-and-linux-muslg.patch; do
    fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/master/main/gcc/$p - | patch -p1
done

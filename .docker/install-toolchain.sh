#!/bin/bash

set -ex

ARCH="$(uname -m)"
OPTIMIZE_STR="${1:-2}"
TARGET_ARCH_STR="${2:-$ARCH}"

BINUTILS_VERSION=2.44
GCC_VERSION=14.2.0
MUSL_VERSION=1.2.5
LINUX_VERSION=6.15.7

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
    fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.21-stable/main/gcc/$p - | patch -p1
done
fetch.sh https://gcc.gnu.org/pipermail/gcc-patches/attachments/20250220/c6211b02/attachment.bin - | patch -p1

ORIGPATH="$PATH"

export CC="/usr/bin/ccache /usr/bin/gcc"
export CXX="/usr/bin/ccache /usr/bin/g++"
export LD="/usr/bin/ld"

for target_arch in ${TARGET_ARCH_STR//,/ }; do
    for OPT in ${OPTIMIZE_STR//,/ }; do
        echo "==========================================================="
        echo "Building for target architecture: $target_arch (-O${OPT})"
        echo "==========================================================="

        export TARGETARCH="$target_arch"

        TARGET="${TARGETARCH}-alpine-linux-musl"

        CARCH="$TARGETARCH"
        case "$TARGETARCH" in
            aarch64*) CARCH="arm64" ;;
            arm*) CARCH="arm" ;;
            mips*) CARCH="mips" ;;
            s390*) CARCH="s390" ;;
            ppc*) CARCH="powerpc" ;;
            riscv*) CARCH="riscv" ;;
            loongarch*) CARCH="loongarch" ;;
        esac

        case "$TARGETARCH" in
            i386)
                GCC_CONFIGURE_ARGS="--with-arch=i486 --with-tune=generic"
                ;;
            aarch64*)
                GCC_CONFIGURE_ARGS="--with-arch=armv8-a --with-abi=lp64"
                ;;
            arm*)
                GCC_CONFIGURE_ARGS="--with-arch=armv6 --with-fpu=vfp --with-float=hard"
                TARGET="arm-linux-musleabihf"
                ;;
            s390x)
                GCC_CONFIGURE_ARGS="--with-arch=z10"
                ;;
            *)
                GCC_CONFIGURE_ARGS=""
                ;;
        esac

        export CFLAGS="-O${OPT} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
        export CXXFLAGS="$CFLAGS"
        export LDFLAGS="-Wl,--gc-sections"

        SYSROOT="/opt/cross/O${OPT}"
        PREFIX="$SYSROOT/usr"
        PATH="$PREFIX/bin:$ORIGPATH"

        GCC_NODOCS="MAKEINFO=/bin/true gcc_cv_prog_makeinfo_modern=no HELP2MAN=/bin/true TEXI2POD=/bin/true POD2MAN=/bin/true"

        # Stage 1
        cd "${HOME}"/pkgs

        mkdir binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPT}
        cd binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPT}

        "$HOME"/pkgs/binutils-${BINUTILS_VERSION}/configure \
            --target=$TARGET --program-prefix="$TARGET-" --prefix=$PREFIX --with-sysroot=$SYSROOT \
            --disable-nls --disable-werror
        make -j"$(nproc)"
        make install

        cd "$HOME"/pkgs/linux-${LINUX_VERSION}
        make ARCH="$CARCH" INSTALL_HDR_PATH=$PREFIX/$TARGET headers_install

        if [[ "$TARGETARCH" != "$ARCH" ]]; then
            cd "$HOME"/pkgs
            mkdir gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPT}-stage1
            cd gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPT}-stage1
            "$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
                --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT --with-newlib --without-headers \
                --disable-nls --disable-shared --disable-multilib --disable-decimal-float --disable-threads \
                --disable-libatomic --disable-libgomp --disable-libquadmath --disable-libssp --disable-libvtv \
                --disable-libstdcxx --enable-languages=c,c++ ${GCC_NODOCS}
            make -j"$(nproc)" all-gcc ${GCC_NODOCS}
            make -j"$(nproc)" all-target-libgcc ${GCC_NODOCS}
            make install-gcc
            make install-target-libgcc
            MUSL_CC="${TARGET}-gcc"
        else
            MUSL_CC="gcc"
        fi

        cd "$HOME"/pkgs
        rm -rf musl-${MUSL_VERSION}
        tar xf ${MUSL_TARBALL}
        cd musl-${MUSL_VERSION}
        fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.22-stable/main/musl/__stack_chk_fail_local.c
        ${MUSL_CC} $CFLAGS -c __stack_chk_fail_local.c -o __stack_chk_fail_local.o
        ${TARGET}-ar r libssp_nonshared.a __stack_chk_fail_local.o
        ./configure --prefix=$PREFIX/$TARGET --target=$TARGET CC=$MUSL_CC
        make install-headers
        make -j"$(nproc)"
        make install
        cp libssp_nonshared.a $PREFIX/$TARGET/lib/

        if [[ "$TARGETARCH" == "$ARCH" ]]; then
            # Fix for aarch64 build: rsync musl headers to native sysroot
            rsync -av $PREFIX/$TARGET/include/ $PREFIX/include/
            rsync -av $PREFIX/$TARGET/lib/ $PREFIX/lib/
        fi

        # Stage 2
        cd "$HOME"/pkgs
        mkdir gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPT}-final
        cd gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPT}-final

        "$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
            --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT ${GCC_CONFIGURE_ARGS} \
            --with-gmp=/usr --with-mpfr=/usr --with-mpc=/usr \
            --disable-shared --enable-tls --disable-libstdcxx-pch --disable-multilib --disable-nls --disable-werror --disable-symvers \
            --enable-threads --enable-__cxa_atexit --enable-languages=c,c++ --enable-link-serialization=2 --enable-linker-build-id \
            --enable-libssp --disable-libsanitizer --enable-checking=release --disable-cet --disable-fixed-point \
            --enable-libstdcxx-time=yes --enable-default-pie --enable-default-ssp --with-linker-hash-style=gnu ${GCC_NODOCS}
        make -j"$(nproc)" ${GCC_NODOCS}
        make install

        # Directory for ccache symlinks
        mkdir -p $PREFIX/lib/ccache/bin

        # Symbolic links for clang
        for clang_binary in /usr/bin/clang{++,}{-[1-9]*,}; do
            name=$(basename $clang_binary)
            ln -s $clang_binary $PREFIX/bin/$TARGET-$name
            ln -s /usr/bin/ccache $PREFIX/lib/ccache/bin/$TARGET-$name
        done

        # Also provide ccache symlinks for gcc
        for gcc_binary in $PREFIX/bin/$TARGET-{c++,g++,gcc,gcc-[1-9]*}; do
            name=$(basename $gcc_binary)
            ln -s /usr/bin/ccache $PREFIX/lib/ccache/bin/$name
        done
    done
done

cd "$HOME"
rm -rf pkgs

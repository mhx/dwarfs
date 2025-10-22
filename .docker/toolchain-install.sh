#!/bin/bash

set -ex

ARCH="$(uname -m)"
OPTIMIZE="$1"
TARGETARCH="$2"

source "$(dirname "$0")/toolchain-versions.sh"

MUSL_TARBALL=musl-${MUSL_VERSION}.tar.gz

ORIGPATH="$PATH"

export CC="/usr/bin/ccache /usr/bin/gcc"
export CXX="/usr/bin/ccache /usr/bin/g++"
export LD="/usr/bin/ld"

MAKE_PARALLEL="make -j$(nproc) -l$(nproc) --output-sync=target"

echo "==========================================================="
echo "Building for target architecture: $TARGETARCH (-O${OPTIMIZE})"
echo "==========================================================="

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
    ppc64)
        GCC_CONFIGURE_ARGS="--with-abi=elfv2"
        ;;
    s390x)
        GCC_CONFIGURE_ARGS="--with-arch=z10"
        ;;
    *)
        GCC_CONFIGURE_ARGS=""
        ;;
esac

export CFLAGS="-O${OPTIMIZE} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-Wl,--gc-sections"

SYSROOT="/opt/cross/O${OPTIMIZE}"
PREFIX="$SYSROOT/usr"
PATH="$PREFIX/bin:$ORIGPATH"

GCC_NODOCS="MAKEINFO=/bin/true gcc_cv_prog_makeinfo_modern=no HELP2MAN=/bin/true TEXI2POD=/bin/true POD2MAN=/bin/true"

# Stage 1
cd "${HOME}"/pkgs

mkdir binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}
cd binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}

"$HOME"/pkgs/binutils-${BINUTILS_VERSION}/configure \
    --target=$TARGET --program-prefix="$TARGET-" --prefix=$PREFIX --with-sysroot=$SYSROOT \
    --disable-nls --disable-werror
$MAKE_PARALLEL
make install

cd "$HOME"/pkgs/linux-${LINUX_VERSION}
make ARCH="$CARCH" INSTALL_HDR_PATH=$PREFIX/$TARGET headers_install

if [[ "$TARGETARCH" != "$ARCH" ]]; then
    cd "$HOME"/pkgs
    mkdir gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}-stage1
    cd gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}-stage1
    "$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
        --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT ${GCC_CONFIGURE_ARGS} --with-newlib --without-headers \
        --disable-nls --disable-shared --disable-multilib --disable-decimal-float --disable-threads \
        --disable-libatomic --disable-libgomp --disable-libquadmath --disable-libssp --disable-libvtv \
        --disable-libstdcxx --enable-languages=c ${GCC_NODOCS}
    $MAKE_PARALLEL all-gcc ${GCC_NODOCS}
    $MAKE_PARALLEL all-target-libgcc ${GCC_NODOCS}
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
patch -p0 <<'PATCH'
--- src/thread/pthread_cond_timedwait.c.orig	2025-10-22 11:57:35.500780438 +0200
+++ src/thread/pthread_cond_timedwait.c	2025-10-22 12:21:49.620780188 +0200
@@ -49,8 +49,9 @@
 {
 	a_store(l, 0);
 	if (w) __wake(l, 1, 1);
-	else __syscall(SYS_futex, l, FUTEX_REQUEUE|FUTEX_PRIVATE, 0, 1, r) != -ENOSYS
-		|| __syscall(SYS_futex, l, FUTEX_REQUEUE, 0, 1, r);
+	else if (__syscall(SYS_futex, l, FUTEX_REQUEUE|FUTEX_PRIVATE, 0, 1, r) < 0
+		&& __syscall(SYS_futex, l, FUTEX_REQUEUE, 0, 1, r) < 0) __wake(l, 1, 1);
+		// __wake() as fallback for Linuxulator, which doesn't support FUTEX_REQUEUE
 }
 
 enum {
PATCH
fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.22-stable/main/musl/__stack_chk_fail_local.c
${MUSL_CC} $CFLAGS -c __stack_chk_fail_local.c -o __stack_chk_fail_local.o
${TARGET}-ar r libssp_nonshared.a __stack_chk_fail_local.o
./configure --prefix=$PREFIX/$TARGET --target=$TARGET CC=$MUSL_CC
make install-headers
$MAKE_PARALLEL
make install
cp libssp_nonshared.a $PREFIX/$TARGET/lib/

if [[ "$TARGETARCH" == "$ARCH" ]]; then
    # Fix for aarch64 build: rsync musl headers to native sysroot
    rsync -av $PREFIX/$TARGET/include/ $PREFIX/include/
    rsync -av $PREFIX/$TARGET/lib/ $PREFIX/lib/
fi

# Stage 2
cd "$HOME"/pkgs
mkdir gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}-final
cd gcc-${GCC_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}-final

"$HOME"/pkgs/gcc-${GCC_VERSION}/configure \
    --target=$TARGET --prefix=$PREFIX --with-sysroot=$SYSROOT ${GCC_CONFIGURE_ARGS} \
    --with-gmp=/usr --with-mpfr=/usr --with-mpc=/usr \
    --disable-shared --enable-tls --disable-libstdcxx-pch --disable-multilib --disable-nls --disable-werror --disable-symvers \
    --enable-threads --enable-__cxa_atexit --enable-languages=c,c++ --enable-link-serialization=2 --enable-linker-build-id \
    --enable-libssp --disable-libsanitizer --enable-checking=release --disable-cet --disable-fixed-point \
    --enable-libstdcxx-time=yes --enable-default-pie --enable-default-ssp --with-linker-hash-style=gnu ${GCC_NODOCS}
$MAKE_PARALLEL ${GCC_NODOCS}
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

cd "$HOME"
rm -rf pkgs

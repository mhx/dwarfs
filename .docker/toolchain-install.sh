#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -ex

ARCH="$(uname -m)"
OPTIMIZE="$1"
TARGETARCH="$2"

SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPTDIR/toolchain-versions.sh"

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

if [[ "$TARGETARCH" == arm* ]]; then
    TARGET="arm-linux-musleabihf"
fi

export CFLAGS="-O${OPTIMIZE} -ffunction-sections -fdata-sections -fmerge-all-constants -fPIC"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-Wl,--gc-sections"

TOOLCHAIN="/opt/cross/O${OPTIMIZE}"
PREFIX="$TOOLCHAIN/usr"
SYSROOT="$TOOLCHAIN/sysroots/$TARGET"
RESOURCE_DIR="$TOOLCHAIN/clang/$TARGET"
PATH="$PREFIX/bin:$ORIGPATH"

# Build the host-side binutils with the system compiler.
cd "${HOME}"/pkgs

mkdir binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}
cd binutils-${BINUTILS_VERSION}-build-${TARGETARCH}-O${OPTIMIZE}

"$HOME"/pkgs/binutils-${BINUTILS_VERSION}/configure \
    --target=$TARGET --program-prefix="$TARGET-" --prefix=$PREFIX --with-sysroot=$SYSROOT \
    --disable-nls --disable-werror
$MAKE_PARALLEL
make install

cd "$HOME"/pkgs/linux-${LINUX_VERSION}
make ARCH="$CARCH" INSTALL_HDR_PATH="$SYSROOT/usr" headers_install

# Clang infers the target from its executable name. Keep the existing
# target-prefixed names and ccache integration for the library builds.
mkdir -p "$PREFIX/lib/ccache/bin" "$RESOURCE_DIR"

for clang_binary in /usr/bin/clang{++,}{-[1-9]*,}; do
    name=$(basename "$clang_binary")
    ln -s "$clang_binary" "$PREFIX/bin/$TARGET-$name"
    ln -s /usr/bin/ccache "$PREFIX/lib/ccache/bin/$TARGET-$name"
done

cat > "$RESOURCE_DIR/target.cfg" <<EOF
--target=$TARGET
--sysroot=$SYSROOT
--gcc-toolchain=$TOOLCHAIN
-resource-dir=$RESOURCE_DIR
--start-no-unused-arguments
-rtlib=compiler-rt
-unwindlib=none
-fuse-ld=lld
--end-no-unused-arguments
EOF

case "$TARGETARCH" in
    arm) printf '%s\n' -march=armv6 -mfpu=vfp -mfloat-abi=hard >> "$RESOURCE_DIR/target.cfg" ;;
    ppc64*) printf '%s\n' -mabi=elfv2 -mlong-double-64 >> "$RESOURCE_DIR/target.cfg" ;;
esac

# Do not inherit host compiler settings in either bootstrap build.
unset CC CXX LD CXXFLAGS
MUSL_CC="$TARGET-clang --no-default-config --config=$RESOURCE_DIR/target.cfg"

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

# musl's configure uses compile-only and -nostdlib linker probes, so
# we can install its headers before the compiler runtime exists. LIBCC
# is set explicitly to prevent probing for the host's GCC runtime.
configure_musl() {
    ./configure --prefix=/usr --syslibdir=/lib --target="$TARGET" \
        --disable-wrapper CC="$MUSL_CC" LIBCC="$1"
}

configure_musl -lclang_rt.builtins
make DESTDIR="$SYSROOT" install-headers

# Build compiler-rt against the newly installed musl and Linux headers.
# The helper must leave the musl source tree intact.
env -u CFLAGS -u LDFLAGS bash "$SCRIPTDIR/compiler-rt-install.sh" "$TARGETARCH" "$OPTIMIZE"

BUILTINS="$($MUSL_CC -print-libgcc-file-name)"
[[ "$BUILTINS" == "$RESOURCE_DIR/"* ]]
test -f "$BUILTINS"
configure_musl "$BUILTINS"
$MAKE_PARALLEL
make DESTDIR="$SYSROOT" install

fetch.sh https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.22-stable/main/musl/__stack_chk_fail_local.c
$MUSL_CC $CFLAGS -c __stack_chk_fail_local.c -o __stack_chk_fail_local.o
${TARGET}-ar r "$SYSROOT/usr/lib/libssp_nonshared.a" __stack_chk_fail_local.o

# Make the dynamic loader symlink relocatable within the sysroot.
for loader in "$SYSROOT"/lib/ld-musl-*.so.1; do
    if [[ -L "$loader" ]]; then
        ln -sfn ../usr/lib/libc.so "$loader"
    fi
done

# Check that the driver selects our libc and startup objects.
for file in crt1.o crti.o crtn.o libc.a; do
    path="$($MUSL_CC -print-file-name="$file")"
    [[ "$path" == "$SYSROOT/"* ]]
    test -f "$path"
done

cat > "$HOME/pkgs/musl-test.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <linux/limits.h>

static volatile uint64_t dividend = 123456789012345ULL;
static volatile uint64_t divisor = 12345;
static void *worker(void *arg) {
    return dividend / divisor == 10000549940ULL ? arg : 0;
}
int main(void) {
    pthread_t thread;
    void *result;
    int value;
    if (pthread_create(&thread, 0, worker, &value) ||
        pthread_join(thread, &result) || result != &value)
        return 1;
    puts("musl/compiler-rt: OK");
    return 0;
}
EOF

$MUSL_CC -O2 -static -pthread -Wl,-Map,"$HOME/pkgs/musl-test.map" \
    "$HOME/pkgs/musl-test.c" -o "$HOME/pkgs/musl-test"
test -s "$HOME/pkgs/musl-test.map"

if grep -E 'lib(gcc(_eh)?|stdc\+\+|supc\+\+|atomic)\.a|(^|[/[:space:]])crt(begin|end)[ST]?\.o' \
        "$HOME/pkgs/musl-test.map"; then
    echo "Unexpected GCC runtime in musl test" >&2
    exit 1
fi

if [[ "$TARGETARCH" == arm && "$TARGETARCH" != "$ARCH" ]]; then
    QEMU_CPU=arm1176 qemu-arm "$HOME/pkgs/musl-test"
elif [[ "$TARGETARCH" != "$ARCH" ]]; then
    "qemu-$TARGETARCH" "$HOME/pkgs/musl-test"
else
    "$HOME/pkgs/musl-test"
fi

cd "$HOME"
rm -rf pkgs

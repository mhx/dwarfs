#!/bin/bash

if [ "$#" -gt 1 ]; then
  echo "usage: $0 [<directory>]"
  exit 1
fi

set -e
shopt -s extglob nullglob

DIR="$(realpath "${1:-$(pwd)}")"
RELDIR="$DIR/release"

# TODO: maybe allow for stuff like RCn for release candidates
SOURCE_TARBALL=$(basename $(eval echo "$DIR/dwarfs-+([0-9]).+([0-9]).+([0-9])?(-+([0-9])-g+([[:xdigit:]])).tar.zst"))
VERSION=$(echo "$SOURCE_TARBALL" | sed -E 's/dwarfs-(.+).tar.zst/\1/')

SCRIPTDIR=$(dirname "$(realpath "$0")")
SFXPACKER="$SCRIPTDIR/../sfx/pack.py"

echo "Source: $SOURCE_TARBALL"
echo "Version: $VERSION"

mkdir -p "$RELDIR"

recompress_tarball() {
  local src="$1"
  local dst="$2"

  local fullsrc="$DIR/$src.tar.zst"
  local fulldst="$RELDIR/$dst.tar.xz"

  if [[ "$src" == "$dst" ]]; then
    zstd -dc "$fullsrc" | xz -9e > "$fulldst"
  else
    mkdir -p "$RELDIR/$dst"
    tar xf "$fullsrc" -C "$RELDIR/$dst" --strip-components=1
    tar cf - -C "$RELDIR" $dst | xz -9e > "$fulldst"
    rm -rf "$RELDIR/$dst"
  fi
}

release_tarball() {
  local src="$1"
  local dst="$2"

  if [[ -z "$dst" ]]; then
    dst="$src"
  fi

  local fulldst="$RELDIR/$dst.tar.xz"

  if [ -f "$fulldst" ]; then
    echo "$fulldst already exists, skipping"
  else
    echo "Creating $fulldst"
    recompress_tarball "$src" "$dst" &
  fi
}

recompress_7z() {
  local src="$1"
  local dst="$2"

  local fullsrc="$DIR/$src.7z"
  local fulldst="$RELDIR/$dst.7z"
  local curcwd=$(pwd)

  cd "$RELDIR"
  7zz x "$fullsrc" -bsp0 -bso0
  if [[ "$src" != "$dst" ]]; then
    mv "$src" "$dst"
  fi
  rm -rf "$dst/include"
  rm -rf "$dst/lib"
  7zz a -t7z "$dst.7z" "$dst" -mx=9 -m0=lzma2 -md=64m -mfb=273 -mmt=on -bsp0 -bso0
  rm -rf "$dst"
  cd "$curcwd"
}

release_7z() {
  local src="$1"
  local dst="$2"

  if [[ -z "$dst" ]]; then
    dst="$src"
  fi

  local fulldst="$RELDIR/$dst.7z"

  if [ -f "$fulldst" ]; then
    echo "$fulldst already exists, skipping"
  else
    echo "Creating $fulldst"
    recompress_7z "$src" "$dst" &
  fi
}

sfx_to_upx() {
  local src="$1"
  local dst="$2"

  $SFXPACKER --decompress --input "$src" --output "$dst"
  upx -9 --best -qqq "$dst"
}

release_binary() {
  local src="$1"
  local dst="$2"

  if [[ -z "$dst" ]]; then
    dst="$src"
  fi

  local fulldst="$RELDIR/$dst"
  if [ -f "$fulldst" ]; then
    echo "$fulldst already exists, skipping"
  else
    echo "Creating $fulldst"
    local fullsrc="$DIR/$src"
    cp "$fullsrc" "$fulldst"
  fi
}

release_upx_binary() {
  local src="$1"
  local dst="$2"

  if [[ -z "$dst" ]]; then
    dst="$src"
  fi

  local fulldst="$RELDIR/$dst"
  if [ -f "$fulldst" ]; then
    echo "$fulldst already exists, skipping"
  else
    echo "Creating $fulldst"
    local fullsrc="$DIR/$src"
    sfx_to_upx "$fullsrc" "$fulldst" &
  fi
}

show_shasums() {
  local curcwd=$(pwd)
  cd "$RELDIR"
  echo "--------------------------------------------------"
  sha256sum *
  cd "$curcwd"
}

release_tarball "dwarfs-$VERSION" "dwarfs-$VERSION"

release_7z "dwarfs-$VERSION-Windows-AMD64"
release_binary "dwarfs-universal-$VERSION-Windows-AMD64.exe"

for ARCH in aarch64 arm i386 loongarch64 ppc64 ppc64le riscv64 s390x x86_64 ; do
  # The tarball is chosen purely for speed, not size
  # The universal binary is chosen for size, without compromising too much on speed
  # The fuse-extract binary is chosen for smallest size

  case "$ARCH" in
    loongarch64)
      _tarball_config="clang-minsize-musl-lto"
      _universal_config="clang-minsize-musl-lto"
      _fuse_extract_config="clang-minsize-musl-minimal-lto"
      ;;

    riscv64)
      _tarball_config="clang-minsize-musl-libressl-lto"
      _universal_config="clang-minsize-musl-libressl-lto"
      _fuse_extract_config="clang-minsize-musl-minimal-libressl-lto"
      ;;

    ppc64)
      _tarball_config="gcc-musl-libressl"
      _universal_config="gcc-musl-libressl"
      _fuse_extract_config="gcc-musl-minimal-libressl"
      ;;

    ppc64le)
      _tarball_config="gcc-musl-lto"
      _universal_config="gcc-musl-libressl-lto"
      _fuse_extract_config="gcc-musl-minimal-libressl-lto"
      ;;

    s390x)
      _tarball_config="gcc-musl-lto"
      _universal_config="gcc-musl-lto"
      _fuse_extract_config="gcc-musl-minimal-lto"
      ;;

    *)
      _tarball_config="clang-minsize-musl-lto"
      _universal_config="clang-minsize-musl-libressl-lto"
      _fuse_extract_config="clang-minsize-musl-minimal-libressl-lto"
      ;;
  esac

  case "$ARCH" in
    aarch64|x86_64)
      # native builds
      ;;

    *)
      _tarball_config="${_tarball_config}-cross-x86_64"
      _universal_config="${_universal_config}-cross-x86_64"
      _fuse_extract_config="${_fuse_extract_config}-cross-x86_64"
      ;;
  esac

  VA="$VERSION-Linux-$ARCH"
  release_tarball "dwarfs-$VA-${_tarball_config}" "dwarfs-$VA"

  case "$ARCH" in
    i386|arm)
      # upx-only
      ;;

    *)
      release_binary "dwarfs-universal-$VA-${_universal_config}" "dwarfs-universal-$VA"
      release_binary "dwarfs-fuse-extract-$VA-${_fuse_extract_config}" "dwarfs-fuse-extract-$VA"
      ;;
  esac

  case "$ARCH" in
    i386|arm|x86_64|aarch64)
      # also provide upx versions
      release_upx_binary "dwarfs-universal-$VA-${_universal_config}" "dwarfs-universal-$VA.upx"
      release_upx_binary "dwarfs-fuse-extract-$VA-${_fuse_extract_config}" "dwarfs-fuse-extract-$VA.upx"
      ;;

    *)
      ;;
  esac
done

jobs -l
wait

show_shasums

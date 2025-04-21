#!/bin/bash

if [ "$#" -gt 1 ]; then
  echo "usage: $0 [<directory>]"
  exit 1
fi

set -e
shopt -s extglob

DIR="$(realpath "${1:-$(pwd)}")"
RELDIR="$DIR/release"

# TODO: maybe allow for stuff like RCn for release candidates
SOURCE_TARBALL=$(basename $(eval echo "$DIR/dwarfs-+([0-9.]).tar.zst"))
VERSION=$(echo "$SOURCE_TARBALL" | sed -E 's/dwarfs-(.+).tar.zst/\1/')

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

recompress_upx() {
  local src="$1"
  local dst="$2"

  local fullsrc="$DIR/$src"
  local tmp="$RELDIR/$dst.tmp"
  local fulldst="$RELDIR/$dst"

  upx -d -qqq -o "$tmp" "$fullsrc"
  upx -9 --best --brute -qqq -o "$fulldst" "$tmp"
  rm -f "$tmp"
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
    ### TODO: maybe enable this in the future
    # recompress_upx "$fullsrc" "$fulldst" &
    cp "$fullsrc" "$fulldst"
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

for ARCH in aarch64 x86_64; do
  VA="$VERSION-Linux-$ARCH"
  release_tarball "dwarfs-$VA-clang-minsize-musl-lto" "dwarfs-$VA"
  release_binary "dwarfs-universal-$VA-clang-minsize-musl-libressl-lto" "dwarfs-universal-$VA"
  release_binary "dwarfs-fuse-extract-$VA-clang-minsize-musl-minimal-lto" "dwarfs-fuse-extract-$VA"
  release_binary "dwarfs-fuse-extract-$VA-clang-minsize-musl-minimal-mimalloc-lto" "dwarfs-fuse-extract-mimalloc-$VA"
done

jobs -l
wait

show_shasums

# WHICH_LINUX={clang,clang-reldbg-stacktrace}
# 
# [ -d "$RELDIR" ] || mkdir "$RELDIR"
# 
# cp_unless_exists() {
#   local filename="$(basename "$1")"
#   if [ -f "$RELDIR/$filename" ]; then
#     echo "$RELDIR/$filename already exists, skipping"
#   else
#     cp "$1" "$RELDIR"
#   fi
# }
# 
# for pkg in "$DIR"/dwarfs-+([0-9.]).tar.zst $(eval echo "$DIR/dwarfs-*-Linux-*-$WHICH_LINUX.tar.zst"); do
#   OUTPUT="$RELDIR"/$(basename "$pkg" .zst).xz
#   if [ -f "$OUTPUT" ]; then
#     echo "$OUTPUT already exists, skipping"
#     continue
#   fi
#   zstd -dc "$pkg" | xz -9e > "$OUTPUT" &
# done
# 
# cp_unless_exists $DIR/dwarfs-*-Windows-AMD64.7z
# 
# for ub in $(eval echo "$DIR/dwarfs-universal-*-Linux-*-$WHICH_LINUX"); do
#   cp_unless_exists "$ub"
# done
# cp_unless_exists "$DIR"/dwarfs-universal-*-Windows-AMD64.exe
# 
# chmod 644 "$RELDIR"/*.{xz,7z,exe}
# chmod 755 "$RELDIR"/dwarfs-universal-*-Linux-*
# 
# # XXX: this recompression makes the executables much slower to start up
# # for exe in "$DIR"/dwarfs-universal-*; do
# #   upx -d -qqq -o "$RELDIR"/$(basename "$exe").tmp "$exe" && upx -9 -qqq -o "$RELDIR"/$(basename "$exe") "$RELDIR"/$(basename "$exe").tmp &
# # done
# 
# jobs -l
# 
# wait
# 
# # rm "$RELDIR"/*.tmp

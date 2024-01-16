#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <directory>"
  exit 1
fi

set -e

DIR="$1"
RELDIR="$DIR/release"

[ -d "$RELDIR" ] || mkdir "$RELDIR"

cp "$DIR"/*.7z "$RELDIR"

for pkg in "$DIR"/dwarfs-*.tar.zst; do
  zstd -dc "$pkg" | xz -9e > "$RELDIR"/$(basename "$pkg" .zst).xz &
done

for exe in "$DIR"/dwarfs-universal-*; do
  upx -d -qqq -o "$RELDIR"/$(basename "$exe").tmp "$exe" && upx -9 -qqq -o "$RELDIR"/$(basename "$exe") "$RELDIR"/$(basename "$exe").tmp &
done

jobs -l

wait

rm "$RELDIR"/*.tmp

#!/bin/bash

set -ex

URL=$1
FILE=${2:-$(basename "$URL")}
SHA512SUM="${3}"

URLHASH=$(echo -n "$URL" | sha256sum | awk '{print $1}')
CACHEDIR="$HOME/.pkgcache"
CACHEFILE="$CACHEDIR/$URLHASH"

if [ ! -f "$CACHEFILE" ]; then
    rm -f "$CACHEFILE.tmp"
    curl --retry 5 -L "$URL" > "$CACHEFILE.tmp"
    if [ -n "$SHA512SUM" ]; then
        echo "$SHA512SUM  $CACHEFILE.tmp" | sha512sum -c -
        if [ $? -ne 0 ]; then
            echo "Checksum verification failed for $URL"
            rm -f "$CACHEFILE.tmp"
            exit 1
        fi
    fi
    mv -f "$CACHEFILE.tmp" "$CACHEFILE"
fi

if [ "$FILE" == "-" ]; then
    cat "$CACHEFILE"
else
    cp "$CACHEFILE" "$FILE"
fi

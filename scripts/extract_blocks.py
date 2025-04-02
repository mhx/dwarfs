#!/bin/env python3

"""
A very rudimentary DwarFS image parser

This currently doesn't do very much apart from extracting the block data
(*without* headers) and writing the blocks to individual files.

Note that this *cannot* currently handle DwarFS image offsets.

The main idea behind this script is to allow experimenting with different
compression algorithms without having to have full-fledged integration
into DwarFS. Essentially, this allows you to individually compress the
blocks externally and see which algorithms/configurations work best.
"""

import sys

from struct import unpack

if len(sys.argv) != 3:
    print(f"USAGE: {__file__} <image> <basename>")
    raise SystemExit(1)


sectypes = {
    0: "block",
    7: "schema",
    8: "metadata",
    9: "index",
    10: "history",
}

compalgs = {
    0: "none",
    1: "lzma",
    2: "zstd",
    3: "lz4",
    4: "lz4hc",
}


_, image, basename = sys.argv

with open(image, "rb") as image:
    while True:
        header = image.read(0x40)
        if len(header) < 0x40:
            break
        ident, major, minor, sha512, xxh3, secno, sectype, compalg, blocklen = unpack(
            "<6sBB32s8sLHHQ", header
        )
        if ident != b"DWARFS":
            print("error: expected dwarfs header")
            raise SystemExit(1)
        if sectype not in sectypes:
            print(f"error: unexpected section type ({sectype})")
            raise SystemExit(1)
        if compalg not in compalgs:
            print(f"error: unexpected compression algorithm ({compalg})")
            raise SystemExit(1)
        print(
            f"{ident.decode('ascii')} v{major}.{minor} [{secno}] {sectypes[sectype]} ({compalgs[compalg]}) {blocklen} bytes"
        )
        block = image.read(blocklen)
        with open(f"{basename}{sectypes[sectype]}{secno}", "wb") as out:
            out.write(block)

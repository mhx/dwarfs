#!/usr/bin/env python3

"""
Create a self-extracting, zstd-compressed binary
"""

import argparse
import lz4.block
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import xxhash
import zstd

TRAILER_MAGIC = b"SQUEEZE!"
TRAILER_SIZE = 32


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stub", required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--lz4", action="store_true")
    ap.add_argument("--level", type=int)
    args = ap.parse_args()

    if args.level is None:
        args.level = 12 if args.lz4 else 19

    if not os.path.isfile(args.stub):
        ap.error(f"--stub not found: {args.stub}")
    if not os.path.isfile(args.input):
        ap.error(f"--input not found: {args.input}")

    with open(args.input, "rb") as f:
        payload = f.read()

    u_size = len(payload)
    u_xxh64 = xxhash.xxh64(payload).intdigest()

    if args.lz4:
        compressed = lz4.block.compress(
            payload, mode="high_compression", compression=args.level, store_size=False
        )
    else:
        compressed = zstd.compress(payload, args.level)

    c_size = len(compressed)

    with open(args.stub, "rb") as fstub, open(args.output, "wb") as fout:
        shutil.copyfileobj(fstub, fout)
        fout.write(compressed)
        trailer = TRAILER_MAGIC + struct.pack("<QQQ", u_size, c_size, u_xxh64)
        assert len(trailer) == TRAILER_SIZE
        fout.write(trailer)

    os.chmod(args.output, os.stat(args.output).st_mode | 0o111)
    print(
        f"OK: wrote {args.output} (orig {u_size} bytes, xxh64=0x{u_xxh64:016x}, payload {c_size} bytes)"
    )


if __name__ == "__main__":
    main()

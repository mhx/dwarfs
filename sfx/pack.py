#!/usr/bin/env python3

"""
Create a self-extracting, compressed binary
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


class Trailer(object):
    TRAILER_MAGIC = b"SQUEEZE!"
    TRAILER_SIZE = 32

    def __init__(self, u_size, c_size, u_xxh64):
        self.u_size = u_size
        self.c_size = c_size
        self.u_xxh64 = u_xxh64
        self.stub_size = None

    def pack(self):
        packed = self.TRAILER_MAGIC + struct.pack(
            "<QQQ", self.u_size, self.c_size, self.u_xxh64
        )
        if len(packed) != self.TRAILER_SIZE:
            raise ValueError(
                f"Trailer size mismatch: expected {self.TRAILER_SIZE}, got {len(packed)}"
            )
        return packed

    @classmethod
    def unpack(cls, data):
        if not data.startswith(cls.TRAILER_MAGIC):
            raise ValueError("Invalid trailer magic")
        return cls(*struct.unpack("<QQQ", data[len(cls.TRAILER_MAGIC) :]))

    def write(self, file):
        file.write(self.pack())

    @classmethod
    def read(cls, file):
        file.seek(-cls.TRAILER_SIZE, os.SEEK_END)
        trailer_data = file.read(cls.TRAILER_SIZE)
        if len(trailer_data) != cls.TRAILER_SIZE:
            raise ValueError("Trailer size mismatch")
        trailer = cls.unpack(trailer_data)
        trailer.stub_size = file.tell() - cls.TRAILER_SIZE - trailer.c_size
        return trailer


class Compressor(object):
    def __init__(self, algorithm, level):
        self.algorithm = algorithm
        self.level = level

    def compress(self, payload):
        if self.algorithm == "lz4":
            return lz4.block.compress(
                payload,
                mode="high_compression",
                compression=self.level,
                store_size=False,
            )
        elif self.algorithm == "zstd":
            return zstd.compress(payload, self.level)
        else:
            raise ValueError(f"Unsupported compression algorithm: {self.algorithm}")


def decompress_lz4(compressed, uncompressed_size):
    return lz4.block.decompress(compressed, uncompressed_size=uncompressed_size)


def decompress_zstd(compressed, uncompressed_size):
    return zstd.decompress(compressed)


def decompress(payload, u_size, u_xxh64, debug=False, quiet=False):
    decompressed = None

    for decompress_fn in (decompress_lz4, decompress_zstd):
        try:
            decompressed = decompress_fn(payload, u_size)
            if len(decompressed) != u_size:
                raise RuntimeError(
                    f"size mismatch (expected {u_size}, got {len(decompressed)})"
                )
            payload_xxh64 = xxhash.xxh64(decompressed).intdigest()
            if payload_xxh64 != u_xxh64:
                raise RuntimeError(
                    f"checksum error (expected 0x{u_xxh64:016x}, got {payload_xxh64:016x})"
                )
            if not quiet:
                sys.stderr.write(
                    f"decompressed {len(decompressed)} bytes using {decompress_fn.__name__}\n"
                )
            break
        except Exception as e:
            if debug:
                sys.stderr.write(
                    f"decompression failed with {decompress_fn.__name__}: {e}\n"
                )
            decompressed = None

    if decompressed is None:
        raise RuntimeError(f"failed to decompress payload")

    return decompressed


def decompose(filename, quiet=False, debug=False):
    with open(filename, "rb") as f:
        trailer = Trailer.read(f)
        f.seek(0, os.SEEK_SET)
        stub = f.read(trailer.stub_size)
        c_payload = f.read(trailer.c_size)
        payload = decompress(
            c_payload,
            u_size=trailer.u_size,
            u_xxh64=trailer.u_xxh64,
            debug=debug,
            quiet=quiet,
        )
        return stub, payload, trailer


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stub")
    ap.add_argument("--input", required=True)
    ap.add_argument("--output")
    ap.add_argument("--lz4", action="store_true")
    ap.add_argument("--level", type=int)
    ap.add_argument("--test", action="store_true")
    ap.add_argument("--decompress", action="store_true")
    ap.add_argument("--recompress", action="store_true")
    ap.add_argument("--debug", action="store_true", help="Enable debug output")
    ap.add_argument("--quiet", action="store_true", help="Suppress output messages")
    args = ap.parse_args()

    if args.level is None:
        args.level = 12 if args.lz4 else 19

    compressor = Compressor("lz4" if args.lz4 else "zstd", args.level)

    if not os.path.isfile(args.input):
        ap.error(f"--input not found: {args.input}")

    if args.test:
        if args.stub:
            ap.error("--stub is unused for testing")
        if args.output:
            ap.error("--output is unused for testing")

        try:
            stub, payload, trailer = decompose(
                args.input, quiet=args.quiet, debug=args.debug
            )
            if not args.quiet:
                sys.stderr.write(
                    f"OK: {args.input} (stub {len(stub)} bytes, payload {len(payload)} bytes, trailer u_size={trailer.u_size}, c_size={trailer.c_size}, u_xxh64=0x{trailer.u_xxh64:016x})\n"
                )
        except Exception as e:
            sys.stderr.write(f"error: {e}\n")
            sys.exit(1)
    elif args.decompress:
        stub, payload, trailer = decompose(
            args.input, quiet=args.quiet, debug=args.debug
        )

        if args.stub:
            with open(args.stub, "wb") as f:
                f.write(stub)
            if not args.quiet:
                sys.stderr.write(f"OK: wrote stub to {args.stub} ({len(stub)} bytes)\n")

        if args.output:
            with open(args.output, "wb") as f:
                f.write(payload)

            os.chmod(args.output, os.stat(args.output).st_mode | 0o111)

            if not args.quiet:
                sys.stderr.write(
                    f"OK: wrote payload to {args.output} ({len(payload)} bytes)\n"
                )
    else:
        if not args.output:
            ap.error("--output is required")

        if args.recompress:
            if args.stub:
                ap.error("--stub is unused for recompression")

            stub, payload, trailer = decompose(
                args.input, quiet=args.quiet, debug=args.debug
            )

            c_payload = compressor.compress(payload)

            trailer.c_size = len(c_payload)
        else:
            if not args.stub:
                ap.error("--stub is required for compression")
            if not os.path.isfile(args.stub):
                ap.error(f"--stub not found: {args.stub}")

            with open(args.stub, "rb") as f:
                stub = f.read()

            with open(args.input, "rb") as f:
                payload = f.read()

            c_payload = compressor.compress(payload)

            trailer = Trailer(
                u_size=len(payload),
                c_size=len(c_payload),
                u_xxh64=xxhash.xxh64(payload).intdigest(),
            )

        with open(args.output, "wb") as f:
            f.write(stub)
            f.write(c_payload)
            trailer.write(f)

        os.chmod(args.output, os.stat(args.output).st_mode | 0o111)

        if not args.quiet:
            sys.stderr.write(
                f"OK: wrote {args.output} (orig {trailer.u_size} bytes, xxh64=0x{trailer.u_xxh64:016x}, payload {trailer.c_size} bytes)\n"
            )


if __name__ == "__main__":
    main()

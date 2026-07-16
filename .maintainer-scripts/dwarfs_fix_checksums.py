#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

"""
dwarfs_fix_checksums.py - recompute the integrity hashes in DwarFS section headers.

A DwarFS (v2.2+) image is a sequence of sections, optionally preceded by an
executable/script header. Every section starts with a 64-byte section_header_v2
that carries two integrity hashes over the rest of the section:

    offset  size  field
    0x00     6    magic "DWARFS"          ) not covered by either hash
    0x06     1    major version           )
    0x07     1    minor version           )
    0x08    32    SHA-512/256             <- covers bytes [0x28 : end-of-section]
    0x28     8    XXH3-64 (little-endian) <- covers bytes [0x30 : end-of-section]
    0x30     4    section number  (LE)
    0x34     2    section type    (LE)
    0x36     2    compression     (LE)
    0x38     8    section length  (LE)   = number of data bytes that follow
    0x40   len    compressed section data

Note that the SHA field's coverage region *includes* the XXH3 field, so the XXH3
hash must be written first, then the SHA computed over the updated bytes.

This tool rewrites ONLY those two hash fields in every section, leaving all other
bytes byte-for-byte identical. Intended use: after fuzzing DwarFS with checksum
verification disabled, crash artifacts have broken hashes; this makes them pass
the integrity gate again so they can be kept as test cases.

Requires: the `xxhash` package (pip install xxhash) and a Python build whose
hashlib exposes 'sha512_256' (standard with any modern OpenSSL).
"""

import argparse
import hashlib
import struct
import sys

try:
    import xxhash
except ImportError:
    sys.exit("error: this script needs the 'xxhash' package -> pip install xxhash")

if "sha512_256" not in hashlib.algorithms_available:
    sys.exit("error: your Python/OpenSSL build does not expose SHA-512/256")

MAGIC = b"DWARFS"

# section_header_v2 field offsets (little-endian throughout; 64-byte header)
HDR_SIZE = 0x40
OFF_MAJOR = 0x06
OFF_MINOR = 0x07
OFF_SHA = 0x08  # 32 bytes
OFF_XXH = 0x28  # 8 bytes
OFF_NUMBER = 0x30  # u32
OFF_TYPE = 0x34  # u16
OFF_COMPR = 0x36  # u16
OFF_LENGTH = 0x38  # u64

# where each hash's coverage begins (both run to the end of the section data)
SHA_COVERAGE_START = 0x28  # includes the XXH3 field
XXH_COVERAGE_START = 0x30

SECTION_TYPE_NAME = {
    0: "BLOCK",
    7: "METADATA_V2_SCHEMA",
    8: "METADATA_V2",
    9: "SECTION_INDEX",
    10: "HISTORY",
}


def sha512_256(data) -> bytes:
    h = hashlib.new("sha512_256")
    h.update(data)
    return h.digest()  # 32 bytes, stored verbatim


def xxh3_64_le(data) -> bytes:
    # DwarFS uses unseeded XXH3_64bits, i.e. seed 0, stored little-endian on disk.
    return struct.pack("<Q", xxhash.xxh3_64(data, seed=0).intdigest())


def log(msg, quiet=False):
    if not quiet:
        print(msg, file=sys.stderr)


def find_image_start(buf, forced):
    """Return the offset where the DwarFS section stream begins, or None."""
    if forced is not None:
        return forced
    if buf[: len(MAGIC)] == MAGIC:
        return 0
    return buf.find(MAGIC) if buf.find(MAGIC) >= 0 else None


def repair(
    buf,
    image_start,
    *,
    check_only=False,
    allow_truncated=False,
    force=False,
    quiet=False,
):
    """Repair every v2 section in `buf` starting at `image_start`.

    Mutates `buf` in place (unless check_only). Returns (n_sections, n_changed).
    Raises ValueError on an unrecoverable structural problem.
    """
    n = len(buf)
    major = buf[image_start + OFF_MAJOR]
    minor = buf[image_start + OFF_MINOR]
    log(f"image starts at offset 0x{image_start:x}, version {major}.{minor}", quiet)

    if minor <= 1 and not force:
        log(
            "this looks like the pre-2.2 format, which has no per-section "
            "checksums -- nothing to repair.",
            quiet,
        )
        log(
            "(the version byte is NOT integrity-protected; if you believe it "
            "was mutated and this really is a v2 image, re-run with --force.)",
            quiet,
        )
        return (0, 0)

    pos = image_start
    n_sections = n_changed = 0
    while pos < n:
        remaining = n - pos
        if remaining < HDR_SIZE:
            log(
                f"warning: {remaining} trailing byte(s) at 0x{pos:x} are too "
                f"short to be a section header; left unchanged.",
                quiet,
            )
            break

        if buf[pos : pos + len(MAGIC)] != MAGIC:
            # Magic isn't integrity-protected and may have been mutated; we still
            # advance via the length field, so just note it and continue.
            log(
                f"warning: section at 0x{pos:x} has no 'DWARFS' magic "
                f"(mutated?); continuing via length field.",
                quiet,
            )

        number = struct.unpack_from("<I", buf, pos + OFF_NUMBER)[0]
        stype = struct.unpack_from("<H", buf, pos + OFF_TYPE)[0]
        compr = struct.unpack_from("<H", buf, pos + OFF_COMPR)[0]
        length = struct.unpack_from("<Q", buf, pos + OFF_LENGTH)[0]

        sec_end = pos + HDR_SIZE + length
        if sec_end > n:
            over = sec_end - n
            if allow_truncated:
                log(
                    f"warning: section #{number} at 0x{pos:x} claims length "
                    f"{length} but runs {over} byte(s) past EOF; hashing only "
                    f"the bytes present (--allow-truncated).",
                    quiet,
                )
                sec_end = n
            else:
                raise ValueError(
                    f"section #{number} at 0x{pos:x} claims length {length}, "
                    f"which runs {over} byte(s) past end of file. Such an image "
                    f"cannot carry a coherent checksum (verification reads "
                    f"'length' bytes before hashing). Pass --allow-truncated to "
                    f"hash the available bytes anyway, or --offset to correct the "
                    f"detected start."
                )

        old_xxh = bytes(buf[pos + OFF_XXH : pos + OFF_XXH + 8])
        old_sha = bytes(buf[pos + OFF_SHA : pos + OFF_SHA + 32])

        # XXH3 first (it lies inside the SHA coverage region), then SHA.
        new_xxh = xxh3_64_le(buf[pos + XXH_COVERAGE_START : sec_end])
        if not check_only:
            buf[pos + OFF_XXH : pos + OFF_XXH + 8] = new_xxh
        new_sha = sha512_256(
            (buf[pos + SHA_COVERAGE_START : sec_end])
            if not check_only
            # in check mode, splice the freshly computed xxh in so the SHA input
            # matches what a real writer would have hashed
            else bytes(buf[pos + SHA_COVERAGE_START : pos + OFF_XXH])
            + new_xxh
            + bytes(buf[pos + OFF_NUMBER : sec_end])
        )
        if not check_only:
            buf[pos + OFF_SHA : pos + OFF_SHA + 32] = new_sha

        changed = (old_xxh != new_xxh) or (old_sha != new_sha)
        n_changed += changed
        n_sections += 1
        tname = SECTION_TYPE_NAME.get(stype, f"type={stype}")
        status = "OK" if not changed else ("BAD" if check_only else "fixed")
        log(
            f"  section #{number:<4} {tname:<18} compr={compr} "
            f"len={length:<10} @0x{pos:<8x} [{status}]",
            quiet,
        )

        pos = sec_end

    return (n_sections, n_changed)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Recompute SHA-512/256 and XXH3-64 integrity hashes in "
        "every section header of a DwarFS v2 image."
    )
    ap.add_argument("input", help="input DwarFS image")
    ap.add_argument("-o", "--output", help="output path (default: <input>.fixed)")
    ap.add_argument("--in-place", action="store_true", help="overwrite the input file")
    ap.add_argument(
        "-n",
        "--check",
        action="store_true",
        help="report which sections have bad hashes; write nothing",
    )
    ap.add_argument(
        "--offset",
        type=lambda s: int(s, 0),
        default=None,
        help="force the byte offset where the section stream starts "
        "(decimal or 0x-hex); skips magic auto-detection",
    )
    ap.add_argument(
        "--allow-truncated",
        action="store_true",
        help="if a section's length runs past EOF, hash the bytes "
        "that are present instead of aborting",
    )
    ap.add_argument(
        "--force",
        action="store_true",
        help="repair as v2 even if the version byte is <= 2.1",
    )
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)

    with open(args.input, "rb") as f:
        buf = bytearray(f.read())

    start = find_image_start(buf, args.offset)
    if start is None:
        sys.exit(
            "error: no 'DWARFS' magic found; not a DwarFS image? "
            "(use --offset to force a start position)"
        )

    try:
        n_sections, n_changed = repair(
            buf,
            start,
            check_only=args.check,
            allow_truncated=args.allow_truncated,
            force=args.force,
            quiet=args.quiet,
        )
    except ValueError as e:
        sys.exit(f"error: {e}")

    log(
        f"{n_sections} section(s) processed, {n_changed} with hashes "
        f"{'that need fixing' if args.check else 'rewritten'}.",
        args.quiet,
    )

    if args.check:
        # exit non-zero if anything is wrong, so it's usable in scripts/CI
        return 1 if n_changed else 0

    out = args.input if args.in_place else (args.output or args.input + ".fixed")
    with open(out, "wb") as f:
        f.write(buf)
    log(f"wrote {out}", args.quiet)
    return 0


if __name__ == "__main__":
    sys.exit(main())

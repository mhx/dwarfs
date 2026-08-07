#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

import argparse
import hashlib
import math
import os
import random
import shutil
import socket
import stat
import sys
from pathlib import Path

DEFAULT_WORD_FILE = Path("/usr/share/dict/words")


def fraction(s):
    v = float(s)
    if not 0.0 <= v <= 1.0:
        raise argparse.ArgumentTypeError("must be between 0 and 1")
    return v


def nonnegative_int(s):
    v = int(s)
    if v < 0:
        raise argparse.ArgumentTypeError("must be >= 0")
    return v


def at_least_two(s):
    v = int(s)
    if v < 2:
        raise argparse.ArgumentTypeError("must be >= 2")
    return v


def positive_float(s):
    v = float(s)
    if v <= 0:
        raise argparse.ArgumentTypeError("must be > 0")
    return v


def load_words(path):
    try:
        lines = Path(path).read_text(errors="ignore").splitlines()
    except OSError:
        lines = []

    words = []
    for word in lines:
        word = "".join(c.lower() for c in word if c.isalnum())[:12]
        if 2 <= len(word) <= 12:
            words.append(word)

    if words:
        return words

    # Fallback for systems without /usr/share/dict/words.
    return [
        "amber", "birch", "cedar", "delta", "ember", "fern", "granite",
        "harbor", "iris", "juniper", "kelp", "linden", "maple", "north",
        "olive", "pine", "quartz", "river", "spruce", "timber", "umber",
        "valley", "willow", "zephyr",
    ]


class NameMaker:
    def __init__(self, rng, words):
        self.rng = rng
        self.words = words
        self.counter = 0

    def make(self, parent, suffix=""):
        while True:
            self.counter += 1
            a = self.rng.choice(self.words)
            b = self.rng.choice(self.words)
            p = parent / f"{a}-{b}-{self.counter:06d}{suffix}"
            if not os.path.lexists(p):
                return p


def allocate_counts(total, fractions):
    """Largest-remainder allocation so the counts sum exactly to total."""
    raw = {k: total * v for k, v in fractions.items()}
    counts = {k: math.floor(v) for k, v in raw.items()}

    remaining = total - sum(counts.values())
    order = sorted(
        fractions,
        key=lambda k: (raw[k] - counts[k], k),
        reverse=True,
    )

    for key in order[:remaining]:
        counts[key] += 1

    return counts


def sample_size(rng, average, maximum):
    # Exponential distribution, clipped at maximum. Non-empty files make
    # content identity meaningful.
    return max(1, min(maximum, int(rng.expovariate(1.0 / average))))


def write_new_content(path, size, compressibility, content_id, rng, seen_hashes):
    """
    Create a new bytewise-distinct content class.

    compressibility = 0 -> essentially random
    compressibility = 1 -> small unique marker + repeated 'A' bytes
    """
    for nonce in range(10000):
        marker = hashlib.blake2b(
            f"{content_id}:{nonce}".encode(), digest_size=16
        ).digest()[:min(size, 16)]

        remaining = size - len(marker)
        random_len = int(round(remaining * (1.0 - compressibility)))
        repeated_len = remaining - random_len

        digest = hashlib.sha256()

        with open(path, "wb") as f:
            f.write(marker)
            digest.update(marker)

            repeated_chunk = b"A" * (1024 * 1024)
            left = repeated_len
            while left:
                chunk = repeated_chunk[:min(left, len(repeated_chunk))]
                f.write(chunk)
                digest.update(chunk)
                left -= len(chunk)

            left = random_len
            while left:
                n = min(left, 1024 * 1024)
                chunk = rng.randbytes(n)
                f.write(chunk)
                digest.update(chunk)
                left -= n

        h = digest.digest()
        if h not in seen_hashes:
            seen_hashes.add(h)
            return

    raise RuntimeError(
        f"could not generate distinct contents for {path}; "
        "try using larger file sizes"
    )


def possible_duplicate_counts(limit, max_group_size):
    """
    Return which totals from 0..limit can be expressed as a sum of duplicate
    group sizes in the range 2..max_group_size.
    """
    possible = [False] * (limit + 1)
    possible[0] = True

    for total in range(1, limit + 1):
        possible[total] = any(
            total >= size and possible[total - size]
            for size in range(2, max_group_size + 1)
        )

    return possible


def choose_duplicate_count(base_files, target, max_group_size):
    """
    Choose the feasible duplicate-file count nearest to target.

    Normally target is already feasible. Adjustments matter for cases such as:
      - target == 1
      - odd targets when max_group_size == 2
    """
    possible = possible_duplicate_counts(base_files, max_group_size)
    candidates = [n for n in range(base_files + 1) if possible[n]]

    return min(
        candidates,
        key=lambda n: (abs(n - target), n),
    )


def duplicate_group_sizes(n, max_group_size, rng):
    """
    Randomly partition n duplicate files into groups sized between
    2 and max_group_size, inclusive.

    The result always sums exactly to n.
    """
    possible = possible_duplicate_counts(n, max_group_size)

    if not possible[n]:
        raise ValueError(
            f"{n} duplicate files cannot be divided into groups of "
            f"2..{max_group_size}"
        )

    groups = []
    remaining = n

    while remaining:
        choices = [
            size
            for size in range(2, min(max_group_size, remaining) + 1)
            if possible[remaining - size]
        ]

        size = rng.choice(choices)
        groups.append(size)
        remaining -= size

    # Avoid making the first groups systematically more significant.
    rng.shuffle(groups)
    return groups


def parse_args():
    p = argparse.ArgumentParser(
        description=(
            "Create a Linux directory tree containing files, directories, "
            "hard links, symlinks, and optional special files."
        )
    )

    p.add_argument(
        "-a", "--average-size",
        type=positive_float,
        default=1024.0,
        help="mean exponential file size in bytes (default: 1024)",
    )
    p.add_argument(
        "-m", "--maximum-size",
        type=nonnegative_int,
        help="maximum file size (default: 8 * average size)",
    )
    p.add_argument(
        "-n", "--entries",
        type=nonnegative_int,
        default=100,
        help="total entries below the output directory (default: 100)",
    )

    p.add_argument(
        "--unique-fraction",
        type=fraction,
        default=0.5,
        help="fraction of base regular files having unique content (default: 0.5)",
    )
    p.add_argument(
        "--directory-fraction",
        type=fraction,
        default=0.05,
    )
    p.add_argument(
        "--symlink-fraction",
        type=fraction,
        default=0.05,
    )
    p.add_argument(
        "--hardlink-fraction",
        type=fraction,
        default=0.1,
        help=(
            "fraction of regular-file entries that are additional hard links "
            "(default: 0.1)"
        ),
    )

    p.add_argument("--char-device-fraction", type=fraction, default=0.0)
    p.add_argument("--block-device-fraction", type=fraction, default=0.0)
    p.add_argument("--fifo-fraction", type=fraction, default=0.0)
    p.add_argument("--socket-fraction", type=fraction, default=0.0)

    p.add_argument(
        "-c", "--compressibility",
        type=fraction,
        default=0.9,
        help="0=random, 1=almost entirely repeated bytes (default: 0.9)",
    )

    p.add_argument(
        "--max-duplicate-group-size",
        type=at_least_two,
        default=5,
        help=(
            "maximum number of independent files sharing identical content "
            "(default: 5)"
        ),
    )

    p.add_argument(
        "-o", "--output-dir",
        type=Path,
        default=Path("."),
        help="root of generated tree (default: current directory)",
    )
    p.add_argument(
        "--word-file",
        type=Path,
        default=DEFAULT_WORD_FILE,
        help="word list used for names (default: /usr/share/dict/words)",
    )
    p.add_argument(
        "--seed",
        type=int,
        help="random seed for reproducible trees",
    )

    return p.parse_args()


def main():
    args = parse_args()

    if sys.platform != "linux":
        raise SystemExit("This script is intended for Linux only.")

    maximum = args.maximum_size
    if maximum is None:
        maximum = max(1, int(round(8 * args.average_size)))
    if maximum < 1:
        raise SystemExit("--maximum-size must be at least 1")

    nonregular = {
        "directory": args.directory_fraction,
        "symlink": args.symlink_fraction,
        "char_device": args.char_device_fraction,
        "block_device": args.block_device_fraction,
        "fifo": args.fifo_fraction,
        "socket": args.socket_fraction,
    }

    nonregular_sum = sum(nonregular.values())
    if nonregular_sum > 1.0 + 1e-12:
        raise SystemExit(
            "directory/symlink/device/fifo/socket fractions sum to more than 1"
        )

    fractions = {
        "regular": max(0.0, 1.0 - nonregular_sum),
        **nonregular,
    }

    counts = allocate_counts(args.entries, fractions)

    # Hard links consume regular-file directory entries, but don't require
    # additional file contents/inodes.
    regular_entries = counts["regular"]
    hardlinks = int(regular_entries * args.hardlink_fraction + 0.5)

    # At least one real inode is required before a hard link can exist.
    if regular_entries:
        hardlinks = min(hardlinks, regular_entries - 1)
    else:
        hardlinks = 0

    base_files = regular_entries - hardlinks

    # First calculate the requested unique/duplicate split.
    requested_unique_files = int(
        base_files * args.unique_fraction + 0.5
    )
    requested_duplicate_files = base_files - requested_unique_files

    # Duplicate files have to be partitionable into groups of at least 2.
    # Pick the nearest feasible count when rounding produces an impossible
    # total, such as one duplicate file.
    duplicate_files = choose_duplicate_count(
        base_files,
        requested_duplicate_files,
        args.max_duplicate_group_size,
    )
    unique_files = base_files - duplicate_files

    rng = random.Random(args.seed)
    words = load_words(args.word_file)
    names = NameMaker(rng, words)

    root = args.output_dir.expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)

    # The output root itself is not included in --entries.
    directories = [root]
    created_paths = []

    # Create directories first so other objects can be distributed among them.
    for _ in range(counts["directory"]):
        parent = rng.choice(directories)
        path = names.make(parent)
        path.mkdir(mode=0o755)
        directories.append(path)
        created_paths.append(path)

    def new_path(suffix=""):
        return names.make(rng.choice(directories), suffix=suffix)

    seen_hashes = set()
    base_paths = []
    content_id = 0

    # Files whose contents occur in exactly one independent inode.
    for _ in range(unique_files):
        path = new_path()
        size = sample_size(rng, args.average_size, maximum)

        content_id += 1
        write_new_content(
            path,
            size,
            args.compressibility,
            content_id,
            rng,
            seen_hashes,
        )

        base_paths.append(path)
        created_paths.append(path)

    # Duplicate content: groups of separate inodes with exactly identical
    # bytes. Group sizes are chosen randomly between 2 and the configured
    # maximum.
    duplicate_groups = duplicate_group_sizes(
        duplicate_files,
        args.max_duplicate_group_size,
        rng,
    )

    for group_size in duplicate_groups:
        leader = new_path()
        size = sample_size(rng, args.average_size, maximum)

        content_id += 1
        write_new_content(
            leader,
            size,
            args.compressibility,
            content_id,
            rng,
            seen_hashes,
        )

        base_paths.append(leader)
        created_paths.append(leader)

        for _ in range(group_size - 1):
            follower = new_path()

            # copyfile(), rather than link(), deliberately gives this file a
            # separate inode.
            shutil.copyfile(leader, follower)

            base_paths.append(follower)
            created_paths.append(follower)

    # Hard links may point to unique-content or duplicate-content files.
    for _ in range(hardlinks):
        source = rng.choice(base_paths)
        path = new_path()
        os.link(source, path)
        created_paths.append(path)

    # FIFOs.
    for _ in range(counts["fifo"]):
        path = new_path()
        os.mkfifo(path, 0o600)
        created_paths.append(path)

    # Device nodes. 0:0 is deliberately used as an inert/reserved device
    # number: these are filesystem-test objects, not intended to be used.
    for kind, mode in (
        ("char_device", stat.S_IFCHR),
        ("block_device", stat.S_IFBLK),
    ):
        for _ in range(counts[kind]):
            path = new_path()

            try:
                os.mknod(
                    path,
                    mode | 0o600,
                    os.makedev(0, 0),
                )
            except PermissionError as e:
                raise SystemExit(
                    f"creating {kind.replace('_', ' ')} {path} requires "
                    f"permission to use mknod (usually root/CAP_MKNOD): {e}"
                )

            created_paths.append(path)

    # Filesystem-backed Unix sockets remain present after the socket is closed.
    for _ in range(counts["socket"]):
        # sockaddr_un.sun_path is short on Linux, so avoid deeply nested dirs.
        candidates = [
            d for d in directories
            if len(os.fsencode(str(d))) < 70
        ]

        if not candidates:
            raise SystemExit(
                "output path is too long to create Unix-domain socket entries"
            )

        parent = rng.choice(candidates)
        path = names.make(parent, suffix=".sock")

        if len(os.fsencode(str(path))) >= 108:
            raise SystemExit(f"Unix-domain socket path is too long: {path}")

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.bind(str(path))
        finally:
            sock.close()

        created_paths.append(path)

    # Symlinks are created last so their targets already exist. Targets are
    # relative, so moving the whole generated tree preserves the links.
    link_targets = created_paths[:] if created_paths else [root]

    for _ in range(counts["symlink"]):
        parent = rng.choice(directories)
        path = names.make(parent)
        target = rng.choice(link_targets)

        relative_target = os.path.relpath(target, start=parent)
        os.symlink(relative_target, path)

        created_paths.append(path)

    print(f"root:              {root}")
    print(f"total entries:     {args.entries}")
    print(f"directories:       {counts['directory']}")
    print(
        f"regular entries:   {regular_entries} "
        f"({base_files} base + {hardlinks} hard links)"
    )
    print(f"  unique bases:    {unique_files}")
    print(f"  duplicate bases: {duplicate_files}")
    print(f"  duplicate groups:{duplicate_groups}")
    print(f"symlinks:          {counts['symlink']}")
    print(f"char devices:      {counts['char_device']}")
    print(f"block devices:     {counts['block_device']}")
    print(f"fifos:             {counts['fifo']}")
    print(f"sockets:           {counts['socket']}")


if __name__ == "__main__":
    main()

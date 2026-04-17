#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

import argparse
import json
import pathlib
import re
import subprocess
import sys

FROM_RE = re.compile(
    r'^(?P<prefix>\s*FROM\s+)'
    r'(?P<ref>\S+?)(?:@sha256:[0-9a-f]{64})?'
    r'(?P<suffix>(?:\s+AS\s+base)?(?:\s+#.*)?\s*)$',
    re.IGNORECASE | re.MULTILINE,
)

ARG_RE = re.compile(r'^\s*ARG\s+([A-Za-z_][A-Za-z0-9_]*)=(.*)\s*$')
VAR_RE = re.compile(r'\$(\w+)|\$\{(\w+)\}')


def parse_arg_defaults(text: str) -> dict[str, str]:
    defaults: dict[str, str] = {}
    for line in text.splitlines():
        m = ARG_RE.match(line)
        if m:
            name, value = m.group(1), m.group(2)
            defaults.setdefault(name, value)
    return defaults


def expand_vars(ref: str, defaults: dict[str, str]) -> str:
    missing: set[str] = set()

    def repl(match: re.Match[str]) -> str:
        name = match.group(1) or match.group(2)
        if name in defaults:
            return defaults[name]
        missing.add(name)
        return match.group(0)

    expanded = VAR_RE.sub(repl, ref)
    if missing:
        names = ", ".join(sorted(missing))
        raise ValueError(f"unresolved ARG(s): {names}")
    return expanded


def resolve_digest_remote(image_ref: str) -> str:
    cmd = [
        "docker",
        "buildx",
        "imagetools",
        "inspect",
        image_ref,
        "--format",
        "{{json .Manifest}}",
    ]

    proc = subprocess.run(
        cmd,
        text=True,
        capture_output=True,
    )

    if proc.returncode != 0:
        stderr = proc.stderr.strip() or "(no stderr)"
        raise RuntimeError(
            f"remote lookup failed for {image_ref}\n"
            f"command: {' '.join(cmd)}\n"
            f"docker exited with code {proc.returncode}\n"
            f"stderr: {stderr}"
        )

    try:
        manifest = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"could not parse imagetools JSON for {image_ref}: {exc}\n"
            f"stdout was:\n{proc.stdout}"
        ) from exc

    digest = manifest.get("digest")
    if not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise RuntimeError(
            f"remote manifest for {image_ref} did not contain a valid digest\n"
            f"stdout was:\n{proc.stdout}"
        )

    return digest


def update_file(path: pathlib.Path, dry_run: bool) -> bool:
    text = path.read_text(encoding="utf-8")
    defaults = parse_arg_defaults(text)
    changed = False

    def repl(match: re.Match[str]) -> str:
        nonlocal changed

        original_ref = match.group("ref")
        bare_ref = re.sub(r'@sha256:[0-9a-f]{64}$', '', original_ref)

        resolved_ref = expand_vars(bare_ref, defaults)
        digest = resolve_digest_remote(resolved_ref)

        new_ref = f"{bare_ref}@{digest}"
        new_line = f"{match.group('prefix')}{new_ref}{match.group('suffix')}"

        if new_line != match.group(0):
            changed = True
            print(f"{path}: {bare_ref} -> {new_ref}")
        else:
            print(f"{path}: unchanged ({new_ref})")

        return new_line

    new_text, count = FROM_RE.subn(repl, text)

    if count == 0:
        print(f"{path}: no 'FROM ... AS base' line found", file=sys.stderr)
        return False

    if changed and not dry_run:
        path.write_text(new_text, encoding="utf-8")

    return changed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pin FROM ... AS base lines to current remote registry digests."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="show what would change, but do not rewrite files",
    )
    parser.add_argument(
        "files",
        nargs="+",
        help="Dockerfiles or generator files to update",
    )
    args = parser.parse_args()

    any_changed = False
    any_error = False

    for name in args.files:
        path = pathlib.Path(name)
        try:
            changed = update_file(path, dry_run=args.dry_run)
            any_changed = any_changed or changed
        except Exception as exc:
            any_error = True
            print(f"{path}: ERROR: {exc}", file=sys.stderr)

    if any_error:
        return 2
    return 0 if any_changed else 1


if __name__ == "__main__":
    sys.exit(main())

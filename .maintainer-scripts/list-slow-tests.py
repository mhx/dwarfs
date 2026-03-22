#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

def build_full_name(testcase: ET.Element) -> str:
    name = (testcase.get("name") or "").strip()
    classname = (testcase.get("classname") or "").strip()

    if name and classname and name != classname:
        return f"{classname}.{name}"
    if name:
        return name
    if classname:
        return classname
    return "<unnamed>"


def parse_testcases(xml_path: Path) -> list[tuple[str, float]]:
    try:
        tree = ET.parse(xml_path)
    except FileNotFoundError:
        print(f"error: file not found: {xml_path}", file=sys.stderr)
        sys.exit(1)
    except ET.ParseError as exc:
        print(f"error: failed to parse XML from {xml_path}: {exc}", file=sys.stderr)
        sys.exit(1)

    results: list[tuple[str, float]] = []

    for testcase in tree.iterfind(".//testcase"):
        full_name = build_full_name(testcase)

        try:
            time_s = float(testcase.get("time", "0") or 0.0)
        except ValueError:
            time_s = 0.0

        results.append((full_name, time_s))

    return results


def parse_all_testcases(xml_paths: list[Path]) -> list[tuple[str, float]]:
    results: list[tuple[str, float]] = []

    for xml_path in xml_paths:
        results.extend(parse_testcases(xml_path))

    return results


def group_name(full_name: str) -> str:
    parts = full_name.split("/")
    return "/".join(parts[:2]) if parts else full_name


def aggregate_tests(
    tests: list[tuple[str, float]],
    do_group: bool,
) -> list[tuple[str, float, int]]:
    totals: dict[str, float] = defaultdict(float)
    counts: dict[str, int] = defaultdict(int)

    for full_name, time_s in tests:
        key = group_name(full_name) if do_group else full_name
        totals[key] += time_s
        counts[key] += 1

    return [(name, totals[name], counts[name]) for name in totals]


def print_results(
    entries: list[tuple[str, float, int]],
    limit: int,
    grouped: bool,
) -> None:
    entries = sorted(entries, key=lambda item: item[1], reverse=True)
    total_time = sum(time_s for _name, time_s, _count in entries)

    if limit > 0:
        entries = entries[:limit]

    if grouped:
        print(f"{'time':>10}  {'% total':>7}  {'count':>5}  name")
        for name, total_time_s, count in entries:
            percent = (100.0 * total_time_s / total_time) if total_time > 0 else 0.0
            print(f"{total_time_s:10.3f}s  {percent:6.2f}%  {count:5d}  {name}")
    else:
        print(f"{'time':>10}  {'% total':>7}  name")
        for name, time_s, _count in entries:
            percent = (100.0 * time_s / total_time) if total_time > 0 else 0.0
            print(f"{time_s:10.3f}s  {percent:6.2f}%  {name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Print the longest-running CTest tests from one or more JUnit XML "
            "files. If no XML files are specified, ctest.xml is used."
        )
    )
    parser.add_argument(
        "xml_files",
        nargs="*",
        default=["ctest.xml"],
        help="JUnit XML files produced by CTest (default: ctest.xml)",
    )
    parser.add_argument(
        "-n",
        "--top",
        type=int,
        default=20,
        help="number of entries to print; use 0 for all (default: %(default)s)",
    )
    parser.add_argument(
        "--group",
        action="store_true",
        help=(
            "group tests by the first up to two '/'-separated parts of the full "
            "test name and aggregate times"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    xml_paths = [Path(path) for path in args.xml_files]

    tests = parse_all_testcases(xml_paths)
    entries = aggregate_tests(tests, do_group=args.group)

    print_results(entries, limit=args.top, grouped=args.group)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

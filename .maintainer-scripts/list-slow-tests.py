#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


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


def parse_testcases(xml_path: Path) -> List[Tuple[str, float]]:
    try:
        tree = ET.parse(xml_path)
    except FileNotFoundError:
        print(f"error: file not found: {xml_path}", file=sys.stderr)
        sys.exit(1)
    except ET.ParseError as exc:
        print(f"error: failed to parse XML from {xml_path}: {exc}", file=sys.stderr)
        sys.exit(1)

    results: List[Tuple[str, float]] = []

    for testcase in tree.iterfind(".//testcase"):
        full_name = build_full_name(testcase)

        try:
            time_s = float(testcase.get("time", "0") or 0.0)
        except ValueError:
            time_s = 0.0

        results.append((full_name, time_s))

    return results


def parse_all_testcases(xml_paths: Sequence[Path]) -> List[Tuple[Path, str, float]]:
    results: List[Tuple[Path, str, float]] = []

    for xml_path in xml_paths:
        for full_name, time_s in parse_testcases(xml_path):
            results.append((xml_path, full_name, time_s))

    return results


def group_name(full_name: str) -> str:
    parts = full_name.split("/")
    return "/".join(parts[:2]) if parts else full_name


def make_key(full_name: str, do_group: bool) -> str:
    return group_name(full_name) if do_group else full_name


def aggregate_tests(
    tests: Sequence[Tuple[Path, str, float]],
    do_group: bool,
) -> List[Tuple[str, float, int]]:
    totals: Dict[str, float] = defaultdict(float)
    counts: Dict[str, int] = defaultdict(int)

    for _xml_path, full_name, time_s in tests:
        key = make_key(full_name, do_group)
        totals[key] += time_s
        counts[key] += 1

    return [(name, totals[name], counts[name]) for name in totals]


def aggregate_test_by_file(
    tests: Sequence[Tuple[Path, str, float]],
    target_name: str,
    do_group: bool,
) -> List[Tuple[Path, float, int]]:
    totals: Dict[Path, float] = defaultdict(float)
    counts: Dict[Path, int] = defaultdict(int)

    for xml_path, full_name, time_s in tests:
        key = make_key(full_name, do_group)
        if key != target_name:
            continue

        totals[xml_path] += time_s
        counts[xml_path] += 1

    return [(xml_path, totals[xml_path], counts[xml_path]) for xml_path in totals]


def all_files_in_same_directory(xml_paths: Sequence[Path]) -> bool:
    if not xml_paths:
        return True

    parents = {path.resolve(strict=False).parent for path in xml_paths}
    return len(parents) == 1


def format_xml_path(xml_path: Path, strip_directory: bool) -> str:
    return xml_path.name if strip_directory else str(xml_path)


def print_overall_results(
    entries: Sequence[Tuple[str, float, int]],
    limit: int,
    grouped: bool,
) -> None:
    sorted_entries = sorted(entries, key=lambda item: item[1], reverse=True)
    total_time = sum(time_s for _name, time_s, _count in sorted_entries)

    if limit > 0:
        sorted_entries = sorted_entries[:limit]

    if grouped:
        print(f"{'time':>11}  {'% total':>7}  {'count':>5}  name")
        for name, total_time_s, count in sorted_entries:
            percent = (100.0 * total_time_s / total_time) if total_time > 0 else 0.0
            print(f"{total_time_s:10.3f}s  {percent:6.2f}%  {count:5d}  {name}")
    else:
        print(f"{'time':>10}  {'% total':>7}  name")
        for name, time_s, _count in sorted_entries:
            percent = (100.0 * time_s / total_time) if total_time > 0 else 0.0
            print(f"{time_s:10.3f}s  {percent:6.2f}%  {name}")


def print_test_by_file_results(
    entries: Sequence[Tuple[Path, float, int]],
    limit: int,
    grouped: bool,
    xml_paths: Sequence[Path],
    target_name: str,
) -> int:
    if not entries:
        kind = "group" if grouped else "test"
        print(f"error: no matching {kind} found: {target_name}", file=sys.stderr)
        return 1

    sorted_entries = sorted(entries, key=lambda item: item[1], reverse=True)
    total_time = sum(time_s for _xml_path, time_s, _count in sorted_entries)

    if limit > 0:
        sorted_entries = sorted_entries[:limit]

    strip_directory = all_files_in_same_directory(xml_paths)

    if grouped:
        print(f"{'time':>11}  {'% total':>7}  {'count':>5}  file")
        for xml_path, time_s, count in sorted_entries:
            percent = (100.0 * time_s / total_time) if total_time > 0 else 0.0
            file_name = format_xml_path(xml_path, strip_directory)
            print(f"{time_s:10.3f}s  {percent:6.2f}%  {count:5d}  {file_name}")
    else:
        print(f"{'time':>10}  {'% total':>7}  file")
        for xml_path, time_s, _count in sorted_entries:
            percent = (100.0 * time_s / total_time) if total_time > 0 else 0.0
            file_name = format_xml_path(xml_path, strip_directory)
            print(f"{time_s:10.3f}s  {percent:6.2f}%  {file_name}")

    return 0


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
    parser.add_argument(
        "--test",
        metavar="NAME",
        help=(
            "show the top XML input files for the given test name; with --group, "
            "NAME is interpreted as a group name"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    xml_paths = [Path(path) for path in args.xml_files]

    tests = parse_all_testcases(xml_paths)

    if args.test is not None:
        entries = aggregate_test_by_file(tests, target_name=args.test, do_group=args.group)
        return print_test_by_file_results(
            entries,
            limit=args.top,
            grouped=args.group,
            xml_paths=xml_paths,
            target_name=args.test,
        )

    entries = aggregate_tests(tests, do_group=args.group)
    print_overall_results(entries, limit=args.top, grouped=args.group)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

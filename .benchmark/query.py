#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

import argparse
import os
import json
import re

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.container as mplc

from packaging.version import Version
from tinydb import TinyDB
from tqdm import tqdm

TABLE_NAME = "benchmarks"


def import_data(db_path, *args):
    """
    Import all JSON files in 'json_dir' into the TinyDB database at 'db_path'.
    """
    db = TinyDB(db_path)
    table = db.table(TABLE_NAME)

    files = []

    for directory in args:
        files.extend(
            [
                os.path.join(directory, f)
                for f in os.listdir(directory)
                if f.endswith(".json")
            ]
        )

    dbtimes = {doc["time"] for doc in table.all()}
    skipped = 0

    for path in tqdm(files, desc="Importing JSON files", unit="file"):
        with open(path, "r") as f:
            data = json.load(f)
            if data["time"] in dbtimes:
                skipped += 1
                continue
            if "exit_codes" in data:
                del data["times"]
                del data["exit_codes"]
            table.insert(data)

    print(f"Skipped {skipped} files that were already in the database.")


def resolve_baseline(columns, meta, version, baseline):
    """
    Pick the column (a version_object) to normalize against.

    - If 'baseline' is None, use the smallest version (deterministic).
    - If '--version' was not given, 'baseline' is a version string
      (optionally "version+commit").
    - If '--version' was given, 'baseline' is a config string.
    """
    cols = list(columns)
    if baseline is None:
        # Deterministic default: the smallest version.
        return min(cols)

    if version is None:
        # 'baseline' is a version, optionally with a "+commit" suffix.
        target = Version(baseline)
        matches = [c for c in cols if c == target]
        if not matches:
            # Fall back to matching the release, ignoring any commit suffix.
            matches = [c for c in cols if c.base_version == target.base_version]
        if not matches:
            available = ", ".join(sorted(str(c) for c in cols))
            raise SystemExit(
                f"Baseline version '{baseline}' not found. Available: {available}"
            )
        return min(matches)

    # 'baseline' is a config (a single version has been selected).
    matches = [c for c in cols if meta.at[c, "config"] == baseline]
    if not matches:
        available = ", ".join(sorted(meta.at[c, "config"] for c in cols))
        raise SystemExit(
            f"Baseline config '{baseline}' not found. Available: {available}"
        )
    return min(matches)


def walltime_chart(db_path, arch, binary_type, version, exclude, normalize, baseline):
    db = TinyDB(db_path)
    table = db.table(TABLE_NAME)

    # Release configurations before 0.12.0
    release_configs_old = {
        k: {None, "clang"} for k in ("standalone", "universal", "fuse-extract")
    }

    release_configs_by_version = {
        "0.12.0": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-lto"},
        },
        "0.12.1": {
            "standalone": {"clang-minsize-musl-mimalloc-lto"},
            "universal": {"clang-minsize-musl-mimalloc-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-mimalloc-lto"},
        },
        "0.12.2": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-lto"},
        },
        "0.12.3": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-libressl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-lto"},
        },
        "0.12.4": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-libressl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-lto"},
        },
        "0.15.3": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-libressl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-libressl-lto"},
        },
        "0.15.4": {
            "standalone": {"clang-minsize-musl-lto"},
            "universal": {"clang-minsize-musl-libressl-lto"},
            "fuse-extract": {"clang-minsize-musl-minimal-libressl-lto"},
        },
    }

    exclude_re = re.compile(exclude) if exclude else None

    def include_config(qconfig, qversion):
        s = release_configs_by_version.get(qversion, release_configs_old).get(
            binary_type
        )
        # print(f"include_config: {qconfig} {qversion} {s}")
        return qconfig in s

    commit = None
    if version is not None and "+" in version:
        version, commit = version.split("+", 1)

    def query(doc):
        if doc["arch"] != arch:
            return False
        if doc["type"] != binary_type:
            return False
        if "mean" not in doc:
            return False
        if "stddev" not in doc:
            return False
        if exclude_re and exclude_re.search(doc["name"]):
            return False
        if version is None:
            if not include_config(doc["config"], doc["version"]):
                return False
        else:
            if doc["version"] != version:
                return False
            elif commit is not None:
                if doc["commit"] is None or doc["commit"] != commit:
                    return False
        return True

    rows = table.search(lambda doc: query(doc))

    # Sort and create a DataFrame with the relevant columns.
    # rows.sort(key=lambda r: (r.get("name"), r.get("config")))
    # df = pd.DataFrame(rows, columns=["name", "config", "mean", "stddev"])
    # rows.sort(key=lambda r: (r.get("name"), r.get("commit_time")))
    df = pd.DataFrame(
        rows, columns=["name", "version", "commit", "config", "mean", "stddev"]
    )
    for row in df.itertuples():
        suffix = []
        if row.commit:
            suffix.append(row.commit)
        if version is not None:
            suffix.append(row.config.replace("-", "_"))
        v = row.version
        if suffix:
            v += "+" + "_".join(suffix)
        df.at[row.Index, "version_object"] = Version(v)
    # df["version_object"] = df["version"].apply(Version)

    # Lookup used to resolve a --baseline argument (version or config) to the
    # corresponding version_object column.
    meta = df.drop_duplicates("version_object").set_index("version_object")

    # Pivot the DataFrame so that "name" becomes the row index and different "config"
    # values become separate columns for the "mean" and "stddev" values.
    df_pivot = df.pivot(
        index="name", columns=["version_object"], values=["mean", "stddev"]
    )

    # Extract the DataFrame for means and stddevs.
    mean_df = df_pivot["mean"]
    stddev_df = df_pivot["stddev"]

    # === Normalization ===

    # Divide every benchmark's times by its value in the baseline column, so the
    # baseline sits at 1.0 and other bars read directly as relative factors
    # (e.g. 1.10 == a 10% regression). Each benchmark is normalized against its
    # own baseline, putting all benchmarks on a comparable linear scale.
    baseline_col = None
    if normalize:
        baseline_col = resolve_baseline(mean_df.columns, meta, version, baseline)
        base_mean = mean_df[baseline_col].copy()

        missing = base_mean.isna()
        if missing.any():
            names = ", ".join(mean_df.index[missing])
            print(
                f"Warning: no baseline ({baseline_col}) measurement for: {names}. "
                "These benchmarks will be blank."
            )

        # Scale stddevs by the same per-benchmark factor (baseline treated as an
        # exact reference).
        stddev_df = stddev_df.div(base_mean, axis=0)
        mean_df = mean_df.div(base_mean, axis=0)

    # === Plotting ===

    # Plot the normalized horizontal grouped bar chart.
    ax = mean_df.plot(
        kind="bar",
        yerr=stddev_df,
        capsize=3,
        figsize=(10, 10),
        log=not normalize,
        width=0.8,
    )

    for container in ax.containers:
        if isinstance(container, mplc.BarContainer):
            pass
        if isinstance(container, mplc.ErrorbarContainer):
            # Set the error bar color to match the bar color.
            for errbar in container.lines[1]:
                errbar.set_alpha(0.25)
            for errbar in container.lines[2]:
                errbar.set_alpha(0.25)

    # Customize titles, labels, and legend.
    ax.set_title(f"Benchmark results for arch={arch}, type={binary_type}")
    # Rotate x-axis labels for better readability.
    ax.set_xticklabels(ax.get_xticklabels(), rotation=20, ha="right")
    if normalize:
        ax.set_ylabel(f"Benchmark Time (normalized to {baseline_col})")
    else:
        ax.set_ylabel("Benchmark Time")
    ax.set_xlabel("Benchmark Name")
    # Add grid lines in the background
    ax.set_axisbelow(True)
    if normalize:
        ax.grid(axis="y", linestyle="-", alpha=0.5, which="major")
        # Reference line at the baseline (1.0).
        ax.axhline(1.0, color="black", linewidth=0.8, alpha=0.6)
        ax.set_ylim(bottom=0.0)
    else:
        ax.grid(axis="y", linestyle="-", alpha=0.5, which="both")
        # Set y-axis range (1ms to 100s)
        ax.set_ylim(0.001, 100.0)

    # Maximize the space for the figure.
    plt.subplots_adjust(left=0.06, right=0.99, top=0.97, bottom=0.12)

    if version is None:
        ax.legend(title="Version", ncol=8)
    else:
        ax.legend(title="Version/Config", ncol=4)

    for boundary in np.arange(len(df)) - 0.5:
        plt.axvline(x=boundary, color="grey", linestyle="-", linewidth=0.5)

    # plt.tight_layout()
    plt.show()


# Interesting queries:


def main():
    parser = argparse.ArgumentParser(
        description="Import JSON benchmarks into TinyDB and generate charts."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Sub-command: import
    import_parser = subparsers.add_parser(
        "import", help="Import JSON files into TinyDB."
    )
    import_parser.add_argument("db_path", help="TinyDB database file.")
    import_parser.add_argument(
        "json_dirs", nargs="+", help="Directories containing JSON files to import."
    )

    # Sub-command: bar
    bar_parser = subparsers.add_parser(
        "walltime", help="Wallclock benchmark time by version."
    )
    bar_parser.add_argument("db_path", help="TinyDB database file.")
    bar_parser.add_argument(
        "--arch", default="x86_64", help="Architecture to filter by."
    )
    bar_parser.add_argument(
        "--binary", default="standalone", help="Binary type to filter by."
    )
    bar_parser.add_argument("--version", default=None, help="Version to filter by.")
    bar_parser.add_argument(
        "--exclude",
        type=str,
        default=None,
        help="Regex matching the names of the benchmarks to exclude.",
    )
    bar_parser.add_argument(
        "--normalize",
        action="store_true",
        help="Normalize each benchmark's times to a baseline and plot on a "
        "linear scale (so a 10%% regression reads as 1.10).",
    )
    bar_parser.add_argument(
        "--baseline",
        type=str,
        default=None,
        help="Baseline to normalize against: a version (default) or a config "
        "if --version is given. Defaults to the smallest version.",
    )

    args = parser.parse_args()

    if args.command == "import":
        import_data(args.db_path, *args.json_dirs)
    elif args.command == "walltime":
        if args.baseline is not None and not args.normalize:
            print("Note: --baseline has no effect without --normalize.")
        walltime_chart(
            args.db_path,
            args.arch,
            args.binary,
            args.version,
            args.exclude,
            args.normalize,
            args.baseline,
        )


if __name__ == "__main__":
    main()

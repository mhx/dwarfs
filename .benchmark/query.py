#!/usr/bin/env python3

import argparse
import os
import json
import re

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.container as mplc

from packaging.version import Version
from tinydb import TinyDB, Query
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


def walltime_chart(db_path, arch, binary_type, version, exclude):
    db = TinyDB(db_path)
    table = db.table(TABLE_NAME)
    Q = Query()

    release_configs = {
        None,
        "clang",
        "clang-minsize-musl-lto",
    }

    exclude_re = re.compile(exclude) if exclude else None

    # Fetch rows that match the arch, binary_type etc.
    rows = table.search(
        (Q.arch == arch)
        & (Q.type == binary_type)
        & (Q.mean.exists())
        & (Q.stddev.exists())
        & (
            Q.config.test(lambda x: x in release_configs)
            if version is None
            else Q.version == version
        )
        & (Q.name.test(lambda x: exclude_re is None or not exclude_re.search(x)))
    )

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

    # Pivot the DataFrame so that "name" becomes the row index and different "config"
    # values become separate columns for the "mean" and "stddev" values.
    df_pivot = df.pivot(
        index="name", columns=["version_object"], values=["mean", "stddev"]
    )

    # Extract the DataFrame for means and stddevs.
    mean_df = df_pivot["mean"]
    stddev_df = df_pivot["stddev"]

    # === Plotting ===

    # Plot the normalized horizontal grouped bar chart.
    ax = mean_df.plot(
        kind="bar", yerr=stddev_df, capsize=3, figsize=(10, 10), log=True, width=0.8
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
    ax.set_ylabel("Benchmark Time")
    ax.set_xlabel("Benchmark Name")
    # Add grid lines in the background
    ax.set_axisbelow(True)
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

    args = parser.parse_args()

    if args.command == "import":
        import_data(args.db_path, *args.json_dirs)
    elif args.command == "walltime":
        walltime_chart(args.db_path, args.arch, args.binary, args.version, args.exclude)


if __name__ == "__main__":
    main()
